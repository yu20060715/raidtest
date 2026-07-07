# RAIDV3 Learning Guide

**Purpose**: Understand how the Windows RAID0/RAID1 system works end-to-end.
**Prerequisites**: C, Windows API, basic filesystem concepts.

---

## 1. System Architecture (7-Layer Model)

```
┌─────────────────────────────────────────────────┐
│  7. GUI  (gui.cpp/ImGui/DX11)                  │
│  6. CLI  (cmd_handler.c)                        │
├─────────────────────────────────────────────────┤
│  5. Service Layer  (raid_service.c)             │
├─────────────────────────────────────────────────┤
│  4. FUSE Bridge   (fuse_bridge.c / WinFsp)     │
├─────────────────────────────────────────────────┤
│  3. RAID Engines  (stripe_engine.c,             │
│                    mirror_engine.c)              │
├─────────────────────────────────────────────────┤
│  2. Cache+Journal (ram_cache.c, journal.c)      │
├─────────────────────────────────────────────────┤
│  1. Storage Layer (storage_common.c, pool_io.c, │
│                    volume_manager.c, superblock.c)│
├─────────────────────────────────────────────────┤
│  0. Devices       (device_manager.c,             │
│                    Windows volume handles)       │
└─────────────────────────────────────────────────┘
```

Data flows **down** on write (FUSE → engine → cache → journal → disk)
and **up** on read (disk → cache → engine → FUSE).

---

## 2. Startup Sequence (`main.c` → `gui.cpp`)

```
WinMain (main.c:5)
  │
  ├─ Command-line parse ───┐
  │                        │
  │  --wizard              ├─→ cmd_handler.c (CLI mode)
  │  --quick-setup         │     cmd_process()
  │  --doctor              │     calls raid_XXXX functions
  │  (no args)             │
  │                        │
  │  GUI mode ─────────────┘
  │  gui.cpp WinMain:
  │  1. gui_init()         → CreateWindow, ImGui init
  │  2. gui_run_loop()     → message pump (PeekMessage)
  │  3. On WM_CREATE →
  │       worker thread:
  │         raid_init_wizard() or raid_init()
  │         volume_load() → fuse_mount_volume()
  │  4. On WM_CLOSE →
  │       raid_cleanup() → cleanup_all()
```

Key point: The GUI runs the FUSE filesystem **inside the same process**. The
message worker thread owns the FUSE dispatch loop. All RAID operations are
initiated from this single thread as responses to GUI control messages.

---

## 3. Module Deep Dives

### 3.1 Storage Common (`storage_common.c`)

**Purpose**: Synchronous sector-level read/write to a physical disk via
Windows `\\.\PhysicalDriveN` handles.

```
stripe_read_raw(disk_handle, lba, buffer, sectors)
  → SetFilePointerEx (seek)
  → ReadFile (with OVERLAPPED for synchronous wait)
  → async_io_wait()

stripe_write_raw(disk_handle, lba, buffer, sectors)
  → SetFilePointerEx (seek)
  → WriteFile (same OVERLAPPED pattern)
```

**Critical detail**: OVERLAPPED is stack-allocated BUT each function waits
synchronously via `GetOverlappedResult(TRUE)`. The stack frame survives the
wait, so the kernel doesn't write to freed memory. This is safe only as long
as the wait is synchronous — if you remove the wait, the kernel will
write to freed stack memory (use-after-free).

**Async I/O wrapper** (`async_io.c/h`):
- `async_io_submit()` — issues overlapped I/O
- `async_io_wait()` — `GetOverlappedResult(..., bWait=TRUE)`
- Currently always used synchronously. The infrastructure exists for true
  async but is not exercised.

### 3.2 Pool I/O (`pool_io.c`)

**Purpose**: Manage stripe pool files — files on RAID member disks that
collectively store the RAID volume data.

```
stripe_pool file structure:
┌─────────────────────┐
│ pool_header         │  (superblock v4)
│   magic=0xDEADBEEF  │
│   version=4         │
│   block_size        │
│   num_slabs         │ (in pool file = number of slabs)
│   slab_size         │
├─────────────────────┤
│ pool_slab[0]        │  ← data slabs (typically 64 MB)
│ pool_slab[1]        │
│ ...                 │
└─────────────────────┘
```

Each disk has ONE pool file (`stripe_pool.dat`). The RAID volume is the
logical concatenation of all pool files across all disks.

**Slab allocation** (`pool_alloc_slab`):
1. Find a pool with free slab space
2. Extend the pool file by `slab_size` (if LBA isn't already mapped)
3. Wait for zeroing to complete (`pool_await_zero_slab`)
4. Map the slab's LBA range

**Asynchronous zeroing**: Slab zeroing happens in a background thread.
The `pool_pending_zero` + `pool_zero_progress` tracking allows the
caller to wait for completion or poll progress.

### 3.3 Volume Manager (`volume_manager.c`)

**Purpose**: Load RAID volumes from disk; manage pool file ↔ disk mapping.

**Volume load sequence** (`volume_load`):
1. For each physical disk, open `stripe_pool.dat`
2. Read superblock headers (first 8 KB of pool file)
3. Validate signatures and compute available space
4. Validate disk layout (min 2 disks for RAID0, 2-4 for RAID1)
5. Populate `VOLUME` struct with disk info and pool geometry

**Pool file close** (`pool_file_close`):
- Flushes + invalidates cache entries for the disk
- Calls `CloseHandle` on the file handle
- Frees the file name buffer

### 3.4 Superblock (`superblock.c`)

**Purpose**: On-disk format for volume metadata, stored at LBA 0-15 of each
pool file.

**Superblock v4 structure** (`common.h:178-207`):

```c
typedef struct {
    uint32_t magic;         // 0xDEADBEEF
    uint32_t version;       // 4
    uint32_t block_size;    // e.g. 4096
    uint32_t slab_size;     // e.g. 64 MB
    uint64_t volume_size;   // total addressable bytes
    uint8_t  raid_type;     // 0=RAID0, 1=RAID1
    uint8_t  disk_count;    // 2-4
    uint8_t  disk_index;    // this disk's position (0..disk_count-1)
    uint8_t  bps;           // blocks per stripe (stripe width in blocks)
    uint32_t cache_size_mb; // RAM cache size
    uint32_t flags;         // feature flags
    uint32_t checksum;      // CRC32 over the header
    uint32_t sequence;      // incremented on each write
    ...
} SUPERBLOCK;
```

**Format on disk**:
- LBA 0: superblock v4 header (4096 bytes)
- LBA 1-15: reserved/padding (future expansion)

**Write strategy**:
1. Increment sequence number
2. Check sequence mod... not quite — it's done via `superblock_write_and_check`
   which writes to offset 0, then verifies the write by re-reading
3. Journal WAL entries for superblock changes

**Format function** (`superblock_format_header`):
- Allocates a 4096-byte buffer
- Fills in magic, version, geometry
- Computes CRC32 checksum
- Writes to the pool file at offset 0

### 3.5 Journal (`journal.c`)

**Purpose**: Write-ahead logging for crash recovery. Ensures metadata + data
writes are atomic.

```
JOURNAL_ENTRY format:
┌────────────────┐
│ type (1 byte)  │  BEGIN=1, DATA=2, COMMIT=3
│ disk_index     │
│ lba            │
│ length         │
│ data[...]      │
│ checksum       │  32-bit CRC
└────────────────┘
```

**Write flow**:
```
journal_begin(tx_id, disk_count)
  → open journal file  ─────┐
  → write BEGIN entry       │  g_journal_cs held
  → close journal file  ────┘

journal_data(tx_id, disk_index, lba, data, length)
  → open journal file  ─────┐
  → write DATA entry(s)     │  g_journal_cs held
  → close journal file  ────┘

journal_commit(tx_id)
  → open journal file  ─────┐
  → write COMMIT entry      │  g_journal_cs held
  → close journal file  ────┘
```

**Each entry opens and closes the journal file** (`RAIDTEST\journal.dat`).
This is suboptimal for performance (3 open/close cycles per transaction)
but keeps the locking model simple.

**Recovery** (`journal_replay`):
1. Open journal file for reading
2. Read entries sequentially
3. If BEGIN + DATA + COMMIT found — apply DATA writes to the pool file
4. If BEGIN without COMMIT — discard (incomplete transaction)
5. Truncate journal file after replay

**Known limitation**: The journal doesn't batch transactions. Each DATA
entry writes to the journal file **synchronously**. This is T6 in the backlog.

### 3.6 RAM Cache (`ram_cache.c`)

**Purpose**: Write-back caching layer between RAID engines and disk I/O.

```
Cache entry:
┌────────────────────┐
│ lba: uint64_t      │
│ disk_index: uint8_t│
│ dirty: bool        │
│ data[block_size]   │
│ access_time        │
└────────────────────┘
```

**Architecture**:
- Hash-table indexed by `(lba, disk_index)`
- LRU eviction when cache fills
- Background flush thread writes dirty entries to disk
- Configurable cache size (default 256 MB, set in config)

**Read flow** (`cache_read`):
1. Hash lookup for `(lba, disk_index)`
2. **Hit** → copy data from cache entry, return SUCCESS
3. **Miss** → delegate to engine's read_raw, cache the result

**Write flow** (`cache_write`):
1. Hash lookup for `(lba, disk_index)`
2. **Hit** → update data in-place, mark dirty
3. **Miss** → evict LRU, allocate new entry, copy data, mark dirty

**Flush algorithm** (`cache_flush_all`):
1. Iterate hash table for dirty entries
2. For each dirty entry:
   - Open relevant pool file handle
   - Overlapped WriteFile (synchronous wait)
   - Clear dirty bit
3. Disk I/O is done sequentially per entry (can be slow)

**Thread safety**:
- `g_cache_cs` critical section protects hash table operations
- Flush thread acquires the CS per flush cycle

### 3.7 Stripe Engine (`stripe_engine.c`)

**Purpose**: RAID0 data distribution across disks.

**Algorithm**: Asymmetric striping.

```
Disk 0:  | S0 | S2 | S4 | S6 | ...
Disk 1:  | S1 | S3 | S5 | S7 | ...
```

Where each stripe is `block_size × bps` bytes.

**LBA to (disk, disk_lba) mapping** (`stripe_volume_map_lba`):

```
stripe_size = block_size * bps          // bytes per stripe
stripe_index = lba / stripe_size        // which stripe number
disk_index   = stripe_index % disk_count
stripe_offset = lba % stripe_size       // byte within stripe
disk_lba = (stripe_index / disk_count) * stripe_size + stripe_offset
```

**Read path** (`stripe_volume_read`):
1. `stripe_volume_map_lba` → array of `IO_ENTRY` (disk_index + disk_lba pairs)
2. For each IO_ENTRY:
   a. `cache_read()` — try cache first
   b. On miss: `stripe_read_raw()` → synchronous sector read
   c. Return data to callee

**Write path** (`stripe_volume_write`):
1. Map LBA to IO_ENTRY array
2. For each IO_ENTRY:
   a. `journal_begin()` + `journal_data()` — log to WAL
   b. `cache_write()` — write to cache (dirty)
   c. `journal_commit()` — finalize WAL entry
3. Cache flush will eventually persist to disk

**Key detail**: The stripe engine does NOT write directly to disk on write.
It writes to cache, and cache flushes to disk asynchronously. This is the
write-back behavior.

### 3.8 Mirror Engine (`mirror_engine.c`)

**Purpose**: RAID1 transparent mirroring across 2-4 disks.

**Read path** (`mirror_volume_read`):
1. Check all disks for health (CHECK_DISKS macro)
2. Read from the **first healthy disk** (`vol->disks[i]->healthy`)
3. Update `vol->bytes_read` (stats counter)

**Write path** (`mirror_write_to_all`):
1. For each healthy disk, write the same data
2. If a write fails, set `vol->degraded = true`
3. Journal each write independently

**Rebuild** (`mirror_volume_rebuild`):
1. Copy data from the first healthy disk
2. Write to all degraded/replacement disks
3. Rebuild buffer: up to 64 MB (VirtualAlloc)
4. Uses `restripe_for_disk()` to extract RAID0 stripes

**Degraded mode**:
- When `vol->degraded = true`, reads skip failed disks
- Writes go to all healthy disks + degrade flag
- Rebuild can be triggered via GUI or CLI to restore redundancy

### 3.9 FUSE Bridge (`fuse_bridge.c`)

**Purpose**: Implements WinFsp FUSE callbacks to present the RAID volume
as a mountable drive letter.

**Callbacks registered**:
```c
static FUSE_OPERATIONS raid_fuse_ops[] = {
    .init      = fuse_mount_volume,   // called on mount
    .destroy   = fuse_unmount_volume, // called on unmount
    .getattr   = raid_getattr,        // stat
    .readdir   = raid_readdir,        // ls
    .open      = raid_open,           // open file
    .release   = raid_release,        // close file
    .read      = raid_read,           // read file
    .write     = raid_write,          // write file
    .truncate  = raid_truncate,       // ftruncate
    .rename    = raid_rename,         // rename/move
    .mkdir     = raid_mkdir,          // create dir
    .rmdir     = raid_rmdir,          // remove dir
    .unlink    = raid_unlink,         // delete file
    .statfs    = raid_statfs,         // df
    ...
};
```

**Open file table** (`g_open_files[]`, max `MAX_OPEN_FILES=256`):
- Slot-based allocation (`find_free_slot`)
- Stores `full_path` (wide char), access flags
- Released on `raid_release`

**Directory handling**:
- Directories are **simulated in the volume metadata** (pool file tracks path + entries)
- `raid_mkdir` adds a metadata entry
- `raid_readdir` enumerates entries under the given path
- Path manipulation uses wide chars (`wchar_t`) throughout

**Naming**: All paths start with `/` (POSIX style, WinFsp converts to
Windows paths internally).

**StatFS** (`raid_statfs`):
- Reports volume size, free space, block size
- Uses `vol->volume_size` for total blocks
- Uses `vol->bytes_written` as an approximation for used blocks

### 3.10 Event Bus (`event_bus.c`)

**Purpose**: Simple publish-subscribe for intra-process events.

```
Subscriber list (global): 32 subscribers max
Event types:
  EB_VOLUME_MOUNT   (1)
  EB_VOLUME_UNMOUNT (2)
  EB_DISK_ADD       (3)
  EB_DISK_REMOVE    (4)
  EB_ERROR          (5)
```

**API**:
- `event_bus_subscribe(event_type, callback, user_data)` → returns sub_id
- `event_bus_unsubscribe(sub_id)`
- `event_bus_fire(event_type, data, size)` → notifies all matched subscribers
- `event_bus_init()` → initialize CS (no cleanup)

**Usage**: GUI subscribes to events to update UI; services fire events on
state changes. Currently only used sparsely — most notification is done
via direct function calls.

### 3.11 Device Manager (`device_manager.c`)

**Purpose**: Enumerate physical disks and logical volumes on Windows.

**Discovery flow**:
```c
device_discover()
  → GetLogicalDrives()
  → For each drive letter with DRIVE_FIXED:
      → Query volume GUID via GetVolumeNameForVolumeMountPoint
      → Query for `\\.\PhysicalDriveN` via mount manager
      → Read drive geometry (IOCTL_DISK_GET_DRIVE_GEOMETRY)
      → Populate DISK_INFO struct
```

**Disk info struct**:
```c
typedef struct {
    uint32_t  disk_id;         // PhysicalDrive index
    wchar_t   drive_letter[4]; // "D:\" etc.
    uint64_t  total_size;      // total bytes
    uint64_t  free_size;       // free bytes (approximate)
    bool      selected;        // GUI selection state
    bool      is_new;          // newly added (for wizard)
    HANDLE    raw_handle;      // open handle to \\.\PhysicalDriveN
} DISK_INFO;
```

**Max disks**: 32 (hardcoded `MAX_DISKS`).

### 3.12 GUI (`gui.cpp`)

**Purpose**: Windows desktop GUI using Dear ImGui + DirectX 11.

**Architecture**:

```
WinMain
  ├─ gui_init() → RegisterClass + CreateWindow
  │   ├─ DX11 init (SwapChain, Device, DeviceContext)
  │   └─ ImGui init (ImGui_ImplWin32_Init, ImGui_ImplDX11_Init)
  │
  ├─ gui_run_loop() → PeekMessage loop
  │   ├─ DispatchMessage → WndProc
  │   │   ├─ WM_CREATE → CreateWorkerThread → raid_init_wizard or raid_init
  │   │   ├─ WM_SIZE   → resize DX swap chain
  │   │   ├─ WM_DESTROY → PostQuitMessage
  │   │   └─ WM_COMMAND → button/control events
  │   │
  │   └─ Render frame:
  │       ├─ NewFrame (ImGui)
  │       ├─ GuiRenderFrame() → render all windows
  │       │   ├─ Dashboard (status view)
  │       │   ├─ Quick Setup (wizard for creating RAID volumes)
  │       │   ├─ Performance Dashboard (charts + graphs)
  │       │   ├─ Settings panel
  │       │   └─ Export/Import functionality
  │       └─ Present (DX11 swap chain)
  │
  └─ gui_shutdown() → ImGui + DX11 teardown
      └─ raid_cleanup() → cleanup_all()
```

**GUI Windows** (each rendered in `GuiRenderFrame`):
- **Dashboard** (`W_DASHBOARD`): Shows volume info, disk statistics, health
- **Quick Setup** (`W_QUICK_SETUP`): One-click RAID creation
- **Wizard** (`W_WIZARD`): Step-by-step RAID creation
- **Create** (`W_CREATE`): Manual pool/drive selection
- **Display** (`W_DISPLAY`): Mount/unmount management
- **Performance Dashboard** (`W_PERFORMANCE`): Real-time throughput charts
- **Export** (`W_EXPORT`): Save diagnostics to file
- **Settings** (`W_SETTINGS`): Cache size, language, theme

**Thread model in GUI**:
- **Main thread**: message pump + ImGui rendering
- **Worker thread**: RAID operations, FUSE dispatch, cache flush
- Communication via Windows messages (`PostMessage`) + shared state

### 3.13 CLI Handler (`cmd_handler.c`)

**Purpose**: Headless / command-line mode for scripting and automation.

**Commands**:
```
raid-info                 → display volume info
create-raid <disk_ids>    → create RAID (auto-detect 0 or 1)
delete-raid               → delete volume
mount-volume <letter>     → mount to drive letter
unmount-volume            → unmount
list-disks                → enumerate disks
set-cache <size_mb>       → set cache size
doctor                    → diagnostic + repair
help                      → this help
```

**Parse strategy**: `strtok_s(buf, " ", &ctx)` tokenizes the input line.
`cmd_process` dispatches via `if (strcmp(args[0], "x") == 0)` chain.

**Wizard mode**: `--wizard` `--quick-setup` flags invoke cmd_process
internally via `cmd_wizard_create`:

```
cmd_wizard_create:
  1. Discover disks
  2. For each eligible disk, add to selection
  3. Auto-detect RAID type (0 if 2 disks, 1 if 2-4 disks)
  4. Call raid_create()
  5. Mount via raid_mount()
```

**Doctor mode** (`cmd_doctor`): Runs diagnostics:
- Check superblock integrity on all disks
- Rescan disk geometry
- Validate journal (replay if needed)
- Report volume health

### 3.14 Config (`config.c`)

**Purpose**: Load/save JSON configuration file.

**Config file location**: `<volume_root>\raidtest_config.json`

**Config structure**:
```json
{
    "cache_mb": 256,
    "language": "en",
    "theme": "dark",
    "auto_mount": true,
    "quick_setup": false,
    "last_drive_letter": "R:"
}
```

**Config loading**: JSON parsed manually (no library), saved/loaded via
`save_config` / `load_config`.

### 3.15 Cleanup (`cleanup.c`)

**Purpose**: Ensure clean shutdown — pool files, journal, open handles.

**Cleanup sequence** (`raid_cleanup` → `cleanup_all`):
1. `cleanup_session()`:
   - Unmount FUSE volume
   - Close pool file handles
   - Flush and close journal
   - Free volume state
2. `cleanup_scan_all_drives()`:
   - **Scans ALL fixed drives and deletes pool files**
   - This is aggressive and potentially destructive (B11)

**Known issue**: The all-drive scan should be restricted to only disks
belonging to the current volume.

---

## 4. Data Flow Deep Dives

### 4.1 Read Path (file → user)

```
User read → FUSE → raid_read()
  │
  ├─ fuse_bridge.c: raid_read()
  │   ├─ Validate file_table entry
  │   ├─ Convert FUSE offset/length to volume LBA
  │   └─ Call engine read ───────────────┐
  │                                      │
  ▼                                      ▼
stripe_volume_read() / mirror_volume_read()
  │                                      │
  ├─ stripe_volume_map_lba()             │  (RAID1: read from healthy disk)
  │   → array of (disk, disk_lba)        │
  │                                      │
  ├─ For each IO_ENTRY:                  │
  │   ├─ cache_read(lba, disk_index)     │
  │   │   ├─ Hash lookup                 │
  │   │   ├─ HIT → return cached data    │
  │   │   └─ MISS → stripe_read_raw()    │
  │   │               → ReadFile(OVERLAPPED sync)
  │   │               → cache_insert()   │
  │   └─ Copy data to output buffer      │
  │                                      │
  └─ bytes_read update ──────────────────┘
      InterlockedExchangeAdd64
```

### 4.2 Write Path (user → file)

```
User write → FUSE → raid_write()
  │
  ├─ fuse_bridge.c: raid_write()
  │   ├─ Validate file_table entry
  │   ├─ Convert to volume LBA
  │   └─ Call engine write ─────────────┐
  │                                     │
  ▼                                     ▼
stripe_volume_write() / mirror_write_to_all()
  │                                     │
  ├─ journal_begin(tx_id)               │
  │   → WAL: write BEGIN entry          │
  │                                     │
  ├─ stripe_volume_map_lba()            │
  │   → (disk, disk_lba) pairs          │
  │                                     │
  ├─ For each:                          │
  │   ├─ journal_data(tx_id, disk, lba, data)
  │   │   → WAL: write DATA entry       │
  │   └─ cache_write(lba, disk, data)   │
  │       → mark dirty for flush        │
  │                                     │
  └─ journal_commit(tx_id)              │
      → WAL: write COMMIT entry         │
      → Cache flush thread eventually   │
        writes dirty entries to disk    │
```

### 4.3 Cache Flush Path (background)

```
Cache flush thread (periodic or full): cache_flush_all()
  │
  ├─ Acquire g_cache_cs
  ├─ Iterate all hash table entries
  ├─ For each dirty entry:
  │   ├─ Get disk handle from vol->disks[n]
  │   ├─ WriteFile (access disk through pool_io)
  │   │   OVERLAPPED (stack, sync wait)
  │   └─ Mark entry clean
  └─ Release g_cache_cs
```

### 4.4 Mount Flow

```
raid_mount(drive_letter)
  │
  ├─ Validate drive letter not in use
  ├─ Open pool files for all member disks
  ├─ volume_load(disks, pool_files, ...)
  │   ├─ Read superblocks from each disk
  │   ├─ Validate geometry consistency
  │   └─ Populate VOLUME struct
  │
  ├─ Choose engine (RAID0 → stripe_engine; RAID1 → mirror_engine)
  │
  ├─ fuse_mount_volume(vol, drive_letter)
  │   ├─ FUSE_MOUNT → WinFsp mount
  │   ├─ Register FUSE callbacks → raid_fuse_ops
  │   └─ FspFileSystemSetOperationGuard
  │
  └─ Spawn cache flush background thread
```

---

## 5. WinFsp Integration Notes

**FUSE API version**: WinFsp implements the FUSE 2.8 API (with some 3.0
extensions).

**Mount point**: A drive letter (e.g., `R:\`), not a directory.

**Key differences from Linux FUSE**:
1. `fuse_operations` struct must be initialized with all function pointers
   (NULL for unsupported ops)
2. WinFsp manages its own thread pool for dispatches; your callbacks run
   in WinFsp's thread pool
3. `fuse_context` is available but `private_data` is typically used for the
   volume pointer
4. WinFsp uses `NTSTATUS`, not `int` for error codes
5. Debugging: WinFsp logs to Event Viewer when `FSP_FUSE_ENABLE_DEBUG` is set

**Build requirements**:
```
winfsp-x64.dll   → redistributable
winfsp.fsh       → FUSE library (header: fuse.h, fuse_common.h)
```

**Linking**: `-lwinfsp-x64` links to WinFsp DLL at runtime.

---

## 6. Thread & Lock Model

| Lock | Type | Covers |
|------|------|--------|
| `g_state.cs` | CRITICAL_SECTION | Global state (S macro) — serializes all API calls |
| `g_cache_cs` | CRITICAL_SECTION | Hash table, LRU eviction, flush coordination |
| `g_journal_cs` | CRITICAL_SECTION | Journal file operations (begin/data/commit) |
| `g_eb_cs` | CRITICAL_SECTION | Event bus subscriber list |
| `g_gui.log_lock` | CRITICAL_SECTION | Log access from GUI thread |
| `g_gui.mutex` | CRITICAL_SECTION | Global GUI state mutations |

**Threads**:
```
Main thread (GUI):
  ├─ PeekMessage loop
  ├─ ImGui::Render()
  └─ DX11 Present()

Worker thread:
  ├─ raid_init() / raid_create()
  ├─ FUSE dispatch (via WinFsp — uses its own thread pool)
  └─ cache_flush_all() on timer

WinFsp thread pool:
  ├─ raid_read, raid_write, raid_getattr, ...
  └─ These call raid_service functions which acquire g_state.cs
```

**Lock ordering**:
1. `g_state.cs` (highest)
2. `g_cache_cs`
3. `g_journal_cs`
4. `g_eb_cs`

Deadlocks are prevented by:
- Never holding multiple locks except in well-defined patterns
- No lock ordering violations in current code

---

## 7. Error Handling Conventions

**Return codes** (`common.h:132-148`):
```c
typedef enum {
    RC_OK                 = 0,
    RC_ERR_GENERIC        = -1,
    RC_ERR_NO_MEMORY      = -2,
    RC_ERR_INVALID_PARAM  = -3,
    RC_ERR_NOT_FOUND      = -4,
    RC_ERR_IO             = -5,
    RC_ERR_INVALID_DISK   = -6,
    RC_ERR_NOT_MOUNTED    = -7,
    RC_ERR_ALREADY_EXISTS = -8,
    RC_ERR_TIMEOUT        = -9,
} RC;
```

**Pattern**: Functions return `RC` (int). Callers check `== RC_OK`.
Errors are logged via `DEB(...)` macro to stderr or file.

**Logging**:
- `DEB(fmt, ...)` — debug print (stderr)
- No syslog/EventLog integration yet (T4 tech debt)

---

## 8. Build System

```
build.bat:
  1. cl.exe (MSVC) or gcc (MinGW) compilation
  2. Source files compiled individually
  3. Link against:
     - kernel32.lib, user32.lib, advapi32.lib
     - d3d11.lib, dxgi.lib (DirectX 11)
     - imm32.lib, version.lib (ImGui Win32 support)
     - winfsp-x64.lib (FUSE)
  4. Output: RAIDV3.exe

Build pre-requisites:
  - Visual Studio Build Tools (cl.exe, link.exe) OR MinGW-w64
  - WinFsp SDK installed (winfsp-x64.lib + header in path)
  - DirectX SDK (included with Windows SDK)
```

---

## 9. Test Infrastructure

**Test framework**: Custom test harness based on C macros.

**Test groups** (`src/test/`):
- `test_stripe_engine.c` — stripe mapping, read/write correctness
- `test_mirror_engine.c` — mirror read/write, rebuild
- `test_storage_common.c` — raw I/O
- `test_journal.c` — WAL begin/data/commit/replay
- `test_superblock.c` — format, read, write
- `test_volume_manager.c` — load/create/destroy
- `test_ram_cache.c` — insert/lookup/evict/flush
- `test_cleanup.c` — cleanup sequence
- `test_config.c` — config save/load
- `test_io_engine.c` — async I/O submit/wait

**Test macros** (`test.h`):
- `TEST_ASSERT(cond, msg)` — fatal assertion
- `TEST_CHECK(cond, msg)` — non-fatal check
- `TEST_SECTION(name)` — named test section
- `TEST_RUN(name, fn)` — run a test case

**Running**: `build.bat && .\RAIDV3.exe --test` or individual test binaries.

**Test status**: 39 tests passing (stable).

---

## 10. Common Pitfalls & Gotchas

1. **OVERLAPPED on stack**: Seemingly async I/O that blocks synchronously.
   Don't remove the `GetOverlappedResult(TRUE)` wait without heap-allocating
   the OVERLAPPED.

2. **pool_size_mb from cache_mb**: GUI uses cache_mb config as pool size
   (B10). Pool files are 4 GB instead of 50 GB.

3. **cleanup_scan_all_drives**: Destroys pool files on all fixed drives,
   not just current volume disks (B11).

4. **strtok in GUI**: Thread-unsafe tokenizer used in worker thread (B13).

5. **Non-atomic bytes_read in mirror**: `+=` instead of Interlocked (B12).

6. **Journal per-entry file I/O**: Each journal transaction opens/closes
   the file 3 times (begin, data, commit).

7. **Cache flush sequential**: Dirty entries are flushed one-at-a-time
   (no batching).

8. **FUSE CS leak**: DeleteCriticalSection is skipped on unmount (B5/B7).

9. **g_state.cs held for duration**: All raid_service functions hold the
   global lock, limiting concurrency (T1).

10. **NO bounds checking on cmd_handler tokens**: Input line is fixed
    buffer (8192 bytes) but no overflow protection on strtok_s tokens.
