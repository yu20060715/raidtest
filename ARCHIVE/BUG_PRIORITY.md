# BUG PRIORITY — RAIDTEST v3 (RC4)

Each confirmed issue assigned P0–P3 based on **data-loss/crash/security risk under normal operation**.

**P0** = Must fix before release  
**P1** = Should fix  
**P2** = Nice to fix  
**P3** = Ignore (false positive, theoretical-only, or intentional design)

---

## P0 — Must Fix Before Release

Bugs causing **data loss, guaranteed crash, or exploitable security breach** under normal use.

---

### P0-01: g_state completely unsynchronized (C5)

| Field | Value |
|---|---|
| **Why** | All 24 `raid_*` functions read/write `g_state` via `S()` macro. Zero calls to `gs_lock()` in `raid_service.c`. FUSE callbacks and GUI workers call these concurrently. Every state machine transition is a data race. |
| **Impact** | State corruption, lost updates, torn reads, inconsistent states. All thread safety is illusory. |
| **Probability** | **Certain** — occurs on any concurrent access (FUSE read+write, cache flush+CLI). |
| **Difficulty** | Medium — wrap each function body with `gs_lock()/gs_unlock()`. Must audit for re-entrancy. 1-2 days. |
| **Confidence** | **High** — zero CS calls confirmed by inspection. |

**Files**: `src/raid_service.c` (all 24 functions), `src/common.h:15-17`

---

### P0-02: Stack buffer overflow — parent_dir_exists (C2)

| Field | Value |
|---|---|
| **Why** | `char parent[256]; strncpy(parent, path, plen); parent[plen] = 0;` — `plen` (from user path) unchecked. Paths >255 chars with directory separator write past buffer. |
| **Impact** | Stack corruption. Exploitable for arbitrary code execution. |
| **Probability** | **Medium** — requires crafted path >255 chars. Trivial for any attacker. |
| **Difficulty** | **Low** — one bounds check: `if (plen >= sizeof(parent)) return false;`. 5 minutes. |
| **Confidence** | **High** — no bounds check exists. |

**File**: `src/fuse_bridge.c:99-103`

---

### P0-03: Stack buffer overflow — raid_rename (C3)

| Field | Value |
|---|---|
| **Why** | `wchar_t newpath[256]; wcscpy(newpath, wdst); wcscat(newpath, L"/"); wcscat(newpath, wsurf);` — all unbounded. Deep directory rename overflows. |
| **Impact** | Stack corruption. Same exploit class as P0-02. |
| **Probability** | **Medium** — requires rename with deep path. |
| **Difficulty** | **Low** — compute required length before concatenation; return error if >255. 15 minutes. |
| **Confidence** | **High** — no length checks exist. |

**File**: `src/fuse_bridge.c:269-274`

---

### P0-04: swscanf buffer overflow — config_load (C9)

| Field | Value |
|---|---|
| **Why** | `char lang[8]; swscanf(value, L"%[^\"]", lang);` — unbounded `%[^\"]` reads until `"`. Any language string >7 chars overflows `lang[8]`. |
| **Impact** | Stack buffer overflow. Exploitable via malicious config.json. |
| **Probability** | **Low** — requires attacker-controlled config file. |
| **Difficulty** | **Low** — add field width: `"%7[^\"]"`. 2 minutes. |
| **Confidence** | **High** — fixed-size buffer with unbounded format. |

**File**: `src/config.c:86`

---

### P0-05: flush_buffer overwrite race — data corruption (H2)

| Field | Value |
|---|---|
| **Why** | `memcpy(cache->flush_buffer, ...)` in `cache_flush_all` (ram_cache.c:128). Both `flush_thread` and FUSE backpressure path call `cache_flush_all` concurrently using the same `flush_buffer`. Second call overwrites data the first is writing to disk. |
| **Impact** | **Persistent data corruption**: wrong data written to disk. Verified by reading back. |
| **Probability** | **High** — backpressure fires when cache >90% dirty, which is the normal time the flush thread is also active. |
| **Difficulty** | Medium — add `InterlockedExchange` flag or heap-allocate per-call buffer. Half day. |
| **Confidence** | **High** — shared buffer without re-entrancy guard. |

**File**: `src/ram_cache.c:128` (called from `ram_cache.c:231` and `fuse_bridge.c:381,447,459`)

---

### P0-06: vol->disks[i] NULL dereference — 25+ sites (H3)

| Field | Value |
|---|---|
| **Why** | `vol->disks[i]` accessed without NULL check in EVERY engine module. Volume with partially populated disk array (failed expand, partial init) guarantees AV. |
| **Impact** | Process termination. Any partial state triggers instant crash. |
| **Probability** | **High** — occurs on any failed `init`, failed `expand`, or corrupt superblock load. |
| **Difficulty** | Medium — 25+ sites to fix. Macro or helper can reduce repetition. 1 day. |
| **Confidence** | **High** — pattern verified across stripe_engine.c, mirror_engine.c, journal.c, superblock.c, cleanup.c. |

**Files**: `src/stripe_engine.c`, `src/mirror_engine.c`, `src/journal.c`, `src/superblock.c`, `src/cleanup.c`

---

### P0-07: Journal writes unsynchronized — silent data loss (H5)

| Field | Value |
|---|---|
| **Why** | All journal functions open file with `CreateFile(..., FILE_SHARE_READ)` only. Concurrent calls get sharing violation and silently fail. Journal entries dropped. |
| **Impact** | **Silent data loss.** On crash, journal recovery replays only partial data. Write-back cache data lost. |
| **Probability** | **High** — flush_thread and FUSE backpressure path both call journal_data during normal operation with >90% dirty cache. |
| **Difficulty** | Medium — add CRITICAL_SECTION for journal. Watch lock ordering (journal lock after cache->lock). Half day. |
| **Confidence** | **High** — no synchronization primitive exists in journal.c. |

**File**: `src/journal.c:35-78`

---

### P0-08: Non-atomic pointer swap during rebuild (H12)

| Field | Value |
|---|---|
| **Why** | `vol->disks[replace_idx] = replacement;` is a plain pointer write. Concurrent I/O thread reads torn pointer or sees new pointer with old `healthy` state. Old disk handle may still be in use. |
| **Impact** | **Data corruption during rebuild.** Reads may use closed handle, read from wrong disk, or see half-updated state. |
| **Probability** | **Medium** — occurs when rebuild runs concurrently with FUSE I/O (normal operation). |
| **Difficulty** | Medium — `InterlockedExchangePointer` + reference counting for old disk handle. 1 day. |
| **Confidence** | **Medium** — pointer write is aligned on x64; the ordering issue is real but timing-dependent. |

**File**: `src/mirror_engine.c:169`

---

### P0-09: Journal 64→32 bit truncation — silent corruption >4 GB (H13)

| Field | Value |
|---|---|
| **Why** | `ReadFile(h, raw, (DWORD)file_size.QuadPart, &read, NULL)` — truncates file size to 32 bits. Files >4GB read only first 4GB; remaining bytes are uninitialized heap. |
| **Impact** | **Silent corruption** during recovery for journal files >4GB. Recovery replays partial data + garbage. |
| **Probability** | **Low** — requires journal to grow >4GB (many writes without flush commit). Cache flush batches reduce this. |
| **Difficulty** | Low — use chunked read or keep journal bounded. 2 hours. |
| **Confidence** | **High** — explicit DWORD cast. |

**File**: `src/journal.c:109`

---

### P0-10: offset + length wraparound — bounds check bypass (M5)

| Field | Value |
|---|---|
| **Why** | `if (virtual_offset + length > vol->virtual_total_bytes)` — unsigned wraparound: sum > UINT64_MAX passes the check, giving read/write beyond volume bounds. |
| **Impact** | **Read/write beyond volume.** Access to uninitialized pool data or out-of-bounds disk areas. |
| **Probability** | **Low** — requires `offset + length > 2^64`. Only at extremely large offsets. |
| **Difficulty** | **Low** — use subtract-before-add pattern. 15 minutes. |
| **Confidence** | **High** — four sites confirmed in mirror_engine.c. |

**File**: `src/mirror_engine.c:35,38,103,106`

---

### P0-11: Destructive cleanup runs on mounted volume (M14)

| Field | Value |
|---|---|
| **Why** | `cleanup_scan_all_drives()` deletes `RAIDTEST\` directory and pool files from every FIXED drive letter A-Z. No check if a RAIDTEST volume is currently mounted. |
| **Impact** | **Deletes active volume data.** Next I/O to mounted drive fails with file-not-found errors. |
| **Probability** | **Medium** — user running `cleanup` command while volume is mounted. The `cleanup` command is in normal CLI flow. |
| **Difficulty** | **Low** — skip drives that match current `mount_point`. 15 minutes. |
| **Confidence** | **High** — no mount state check in cleanup_scan_all_drives. |

**File**: `src/cleanup.c:74-112`

---

### P0-12: NULL deref in cache_flush_thread (C1)

| Field | Value |
|---|---|
| **Why** | `STRIPE_VOLUME* vol = (STRIPE_VOLUME*)arg; RAM_CACHE* cache = &vol->cache;` — no NULL check on `arg`. |
| **Impact** | **Process termination** if flush thread created with NULL argument. |
| **Probability** | **Low** — current callers always pass valid pointer. But defensive failure is guaranteed if any code path passes NULL. |
| **Difficulty** | **Low** — add `if (!arg) return 1;` as first line. 2 minutes. |
| **Confidence** | **High** — code is unconditional. |

**File**: `src/ram_cache.c:210-212`

---

### P0-13: Double-checked locking — file_table_lock_init (C4)

| Field | Value |
|---|---|
| **Why** | `if (!g_file_table_lock_init) { InitializeCriticalSection(&g_file_table_lock); g_file_table_lock_init = true; }` — no atomic flag. Two concurrent FUSE callbacks both enter. |
| **Impact** | **Critical section corruption.** Deadlock or corruption on concurrent FUSE operations. |
| **Probability** | **High** — occurs on any concurrent FUSE call (open+readdir from separate WinFsp workers). |
| **Difficulty** | **Low** — move init to `fuse_init()` (single-thread context). 10 minutes. |
| **Confidence** | **High** — classic double-checked locking anti-pattern. |

**File**: `src/fuse_bridge.c:39-43`

---

### P0-14: DeleteCriticalSection while FUSE callbacks active (H6)

| Field | Value |
|---|---|
| **Why** | `fuse_unmount_volume` destroys `g_file_table_lock` CS after `WaitForSingleObject(fuse_thread, 5000)`. WinFsp worker threads can outlive the FUSE loop thread. Late callback enters destroyed CS. |
| **Impact** | **Crash or deadlock on unmount during active I/O.** |
| **Probability** | **Medium** — occurs on unmount while read/write operations in flight. |
| **Difficulty** | **Hard** — add reference counting to file table. Quiesce all workers before destroy. 1-2 days. |
| **Confidence** | **High** — no quiesce exists before deletion. |

**File**: `src/fuse_bridge.c:585-588`

---

### P0-15: device_get() NULL not checked — 8 sites (H4)

| Field | Value |
|---|---|
| **Why** | `device_get(index)` returns NULL on invalid index. 8 call sites in `raid_service.c` immediately dereference the result without check. |
| **Impact** | **Process crash** on invalid disk index. |
| **Probability** | **Medium** — occurs with invalid CLI arguments or after cleanup. |
| **Difficulty** | **Low** — add NULL check at each site. 30 minutes. |
| **Confidence** | **High** — 8 sites confirmed by inspection. |

**File**: `src/raid_service.c:106,184,208,227,252,267,456,540`

---

## P1 — Should Fix

Significant bugs causing **incorrect behavior** but not immediate data loss under normal use.

---

### P1-01: Non-atomic bytes_written/bytes_read (H1)

| Field | Value |
|---|---|
| **Why** | `volatile uint64_t` does not make `+=` atomic. Concurrent threads lose increments. |
| **Impact** | Incorrect I/O statistics. NO data corruption (informational only). |
| **Probability** | **High** — every concurrent write loses updates. |
| **Difficulty** | **Low** — replace with `InterlockedExchangeAdd64`. 1 hour. |
| **Confidence** | **High** — volatile misconception is well-documented. |

**Files**: `src/stripe_engine.c:418,495,613`, `src/mirror_engine.c`, `src/fuse_bridge.c`

---

### P1-02: config_save disk_count overread (H7)

| Field | Value |
|---|---|
| **Why** | Loop `for (i = 0; i < cfg->disk_count; i++)` without checking `disk_count <= MAX_DISKS`. |
| **Impact** | Out-of-bounds read. Leaks stack data into saved config file. |
| **Probability** | **Low** — requires corrupted config. |
| **Difficulty** | **Low** — add bound check before loop. 5 minutes. |
| **Confidence** | **High** — no array bounds guard. |

**File**: `src/config.c:48`

---

### P1-03: NULL crash in planner_calculate (H8)

| Field | Value |
|---|---|
| **Why** | `memset(out, 0, sizeof(*out))` BEFORE NULL check on `out`. |
| **Impact** | **Crash** on NULL output pointer. Feature is informational only (no data loss). |
| **Probability** | **Low** — callers always pass `&result`. |
| **Difficulty** | **Low** — move NULL check before memset. 2 minutes. |
| **Confidence** | **High** — wrong order of operations. |

**File**: `src/planner_engine.c:4`

---

### P1-04: Profiler slot 0 ambiguity (H9)

| Field | Value |
|---|---|
| **Why** | `find_free_slot` returns -1 when full. Callers use slot 0 as fallback without claiming it. |
| **Impact** | Incorrect latency metrics. No data corruption. |
| **Probability** | **Medium** — occurs under high I/O concurrency (all 8 slots in use). |
| **Difficulty** | **Low** — change return convention. 30 minutes. |
| **Confidence** | **Medium** — timing-dependent. |

**File**: `src/profiler.c:26-29,36`

---

### P1-05: IOPS calculation uses wrong counter (H10)

| Field | Value |
|---|---|
| **Why** | IOPS = `read_ops / dt` (cumulative lifetime) instead of delta from last sample. Copy-paste error used `last_read_bytes` instead of `last_read_ops`. |
| **Impact** | IOPS shows lifetime average, not current rate. Misleading performance data. |
| **Probability** | **Certain** — every sample is wrong. |
| **Difficulty** | **Low** — add `last_read_ops`/`last_write_ops` tracking. 30 minutes. |
| **Confidence** | **High** — variable name mismatch confirmed. |

**File**: `src/profiler.c:114-118`

---

### P1-06: Silent path truncation in FUSE bridge (H15)

| Field | Value |
|---|---|
| **Why** | `mbstowcs(wname, name, 256)` truncates output. Return value not checked. |
| **Impact** | Paths >255 chars truncated. Incorrect file lookups. Potential security collision. |
| **Probability** | **Low** — requires paths >255 chars (valid in modern filesystems). |
| **Difficulty** | **Low** — check return value, return error if truncated. 1 hour. |
| **Confidence** | **High** — documented mbstowcs behavior. |

**File**: `src/fuse_bridge.c:53,63,151,160,260-261`

---

### P1-07: 64→32 truncation of remaining bytes (M2)

| Field | Value |
|---|---|
| **Why** | `remaining = (uint32_t)(vol->virtual_total_bytes - virtual_offset)` — truncates for volumes >4GB. |
| **Impact** | Incorrect transfer size at large offsets. Mapped I/O may read/write less than requested. |
| **Probability** | **Low** — requires volume >4GB and offset near end. |
| **Difficulty** | **Low** — keep `remaining` as `uint64_t`. 15 minutes. |
| **Confidence** | **High** — explicit truncation cast. |

**File**: `src/stripe_engine.c:332`

---

### P1-08: healthy_count underflow (M6)

| Field | Value |
|---|---|
| **Why** | `InterlockedDecrement(&vol->healthy_count)` without guard. Multiple concurrent failures wrap counter. |
| **Impact** | `healthy_count` becomes huge positive. Prevent degraded-mode detection. Volume operates as if all healthy. |
| **Probability** | **Low** — requires concurrent failures on already-failed disk. |
| **Difficulty** | **Low** — use `InterlockedCompareExchange` to gate decrement. 30 minutes. |
| **Confidence** | **Medium** — timing-dependent. |

**File**: `src/mirror_engine.c:59,69,83,91`

---

### P1-09: Use-after-free of subscriber data (M9)

| Field | Value |
|---|---|
| **Why** | Subscriber struct copied under lock. Lock released before callback. Between release and callback, `event_bus_unsubscribe` can free/falsify `userdata`. |
| **Impact** | **Use-after-free** of userdata pointer. Crash or unpredictable behavior in subscriber. |
| **Probability** | **Low** — requires unsubscribe during publish on same event type. |
| **Difficulty** | Medium — reference counting or postpone unsubscription. 1 day. |
| **Confidence** | **Medium** — timing window is small. |

**File**: `src/event_bus.c:68-70`

---

### P1-10: Double init of event_bus CS (M10)

| Field | Value |
|---|---|
| **Why** | `if (g_eb_inited) return; InitializeCriticalSection(&g_eb_cs); g_eb_inited = true;` — double-checked locking without barrier. |
| **Impact** | **CS corruption** on concurrent init. Deadlock. |
| **Probability** | **Low** — `event_bus_init` called once from `raid_init` (single-threaded init). Risk on reload. |
| **Difficulty** | **Low** — move to guaranteed single-thread context. 10 minutes. |
| **Confidence** | **Medium** — init is currently single-thread. Structural issue. |

**File**: `src/event_bus.c:28-32`

---

### P1-11: Dead IOCTL — 4K sector misdetection (M11)

| Field | Value |
|---|---|
| **Why** | `STORAGE_ADAPTER_DESCRIPTOR` read via DeviceIoControl at disk_scanner.c:90-93. Result never used. Sector size hardcoded to 512. |
| **Impact** | 4K-sector disks report wrong sector size. Performance and capacity calculations wrong. |
| **Probability** | **Medium** — 4K-sector drives are common. Misdetected silently. |
| **Difficulty** | **Low** — use `DISK_GEOMETRY` or remove dead code. 1 hour. |
| **Confidence** | **High** — variable declared, populated, never read. |

**File**: `src/disk_scanner.c:90-94`

---

### P1-12: Silent disk skip on realloc failure (M12)

| Field | Value |
|---|---|
| **Why** | `if (!new_disks) { CloseHandle(h); continue; }` — realloc failure silently skips the disk. |
| **Impact** | User creates volume with fewer disks than expected. Missing disk undetected. |
| **Probability** | **Low** — requires memory pressure during disk scan. |
| **Difficulty** | **Low** — return error code on realloc failure. 15 minutes. |
| **Confidence** | **High** — no error return path. |

**File**: `src/disk_scanner.c:43-44`

---

### P1-13: Logger timestamps always disabled (M13)

| Field | Value |
|---|---|
| **Why** | `g_timestamp = false;` — never set to true by any API. No setter in logger.h. |
| **Impact** | All log output lacks timestamps. Debugging and forensics impaired. |
| **Probability** | **Certain** — timestamps never work. |
| **Difficulty** | **Low** — add setter, call from `raid_init`. 15 minutes. |
| **Confidence** | **High** — no setter exists. |

**File**: `src/logger.c:6`

---

### P1-14: Memory leak on cache_init failure (C7)

| Field | Value |
|---|---|
| **Why** | `cache->valid_map` (allocated line 17) not freed when `flush_buffer` VirtualAlloc fails (line 26-29). |
| **Impact** | Small memory leak on init failure. Low likelihood. |
| **Probability** | **Low** — VirtualAlloc rarely fails. |
| **Difficulty** | **Low** — add `free(cache->valid_map)`. 2 minutes. |
| **Confidence** | **High** — missing free confirmed. |

**File**: `src/ram_cache.c:26-29`

---

### P1-15: Thread handle lost on partial failure (M8)

| Field | Value |
|---|---|
| **Why** | `_beginthreadex` succeeds but subsequent init fails. Thread handle not closed in error path. |
| **Impact** | Handle leak on mount failure. |
| **Probability** | **Low** — requires init step to fail after thread creation. |
| **Difficulty** | **Low** — close handle on failure path. 15 minutes. |
| **Confidence** | **Medium** — depends on specific failure ordering. |

**File**: `src/volume_manager.c:120`

---

### P1-16: created_time never set (M7)

| Field | Value |
|---|---|
| **Why** | `vol->created_time = 0;` — initialized to zero, never updated. |
| **Impact** | Volume creation date shows Unix epoch (1970). Cosmetic. |
| **Probability** | **Certain** — every volume shows created_time=0. |
| **Difficulty** | **Low** — add `GetSystemTimeAsFileTime`. 5 minutes. |
| **Confidence** | **High** — no call exists in volume_manager.c. |

**File**: `src/volume_manager.c:13`

---

## P2 — Nice to Fix

Low-impact or theoretical issues. Fix when time permits.

---

### P2-01: Uninitialized seg_idx (M1)

| Field | Value |
|---|---|
| **Why** | `seg_idx = 0` initialized but loop may exit without matching. No defensive guard. |
| **Impact** | Theoretical incorrect LBA mapping. No known trigger. |
| **Probability** | **Very low** — loop invariant guarantees match. |
| **Difficulty** | **Low** — add assertion after loop. 10 minutes. |
| **Confidence** | **Low** — no scenario where mismatch occurs. |

**File**: `src/stripe_engine.c:376-385`

---

### P2-02: Integer overflow in LBA mapping (M3)

| Field | Value |
|---|---|
| **Why** | Triple product `cycle_index * ratio * stripe_unit` can overflow uint64_t at extreme sizes. |
| **Impact** | Wraparound for volumes >10^19 bytes. Theoretical. |
| **Probability** | **Negligible** — requires volumes petabytes in size with MAX_DISKS=4. |
| **Difficulty** | **Low** — add overflow detection. 30 minutes. |
| **Confidence** | **Low** — practical risk is zero. |

**File**: `src/stripe_engine.c:31,150,285`

---

### P2-03: block_count truncation (M4)

| Field | Value |
|---|---|
| **Why** | `(uint32_t)(size_bytes / 65536)` truncates for caches >256TB. VirtualAlloc fails first. |
| **Impact** | None at practical cache sizes. |
| **Probability** | **Negligible** — VirtualAlloc fails before truncation. |
| **Difficulty** | **Low** — change to uint64_t. 5 minutes. |
| **Confidence** | **Low** — theoretical only. |

**File**: `src/ram_cache.c:8`

---

### P2-04: Unsigned wraparound in display calculation (C11)

| Field | Value |
|---|---|
| **Why** | `pool - phys_start - phys_used` in log-only calculation. Underflow gives huge displayed number. |
| **Impact** | Incorrect log output only. No functional effect. |
| **Probability** | **Low** — requires specific volume geometry. |
| **Difficulty** | **Low** — add underflow guard. 5 minutes. |
| **Confidence** | **Low** — display-only, algorithm prevents functional bug. |

**File**: `src/stripe_engine.c:182`

---

### P2-05: snprintf position overflow (M15)

| Field | Value |
|---|---|
| **Why** | `pos += snprintf(buf + pos, sizeof(buf) - pos, ...)` — if `pos > sizeof(buf)`, subtraction wraps. |
| **Impact** | Buffer overflow if field count grows. Safe at MAX_DISKS=4. |
| **Probability** | **Negligible** — current constants keep pos < sizeof(buf). |
| **Difficulty** | **Low** — clamp pos before subtraction. 5 minutes. |
| **Confidence** | **Low** — structural fragility, no overflow at current constants. |

**File**: `src/superblock.c:490`

---

### P2-06: Magic number 65536 (L1)

| Field | Value |
|---|---|
| **Why** | `65536` used directly instead of `MIN_VOLUME_SIZE` constant in `random_test`. |
| **Impact** | Maintainability only. No functional bug. |
| **Probability** | — |
| **Difficulty** | **Low** — replace with named constant. 2 minutes. |
| **Confidence** | **High** — magic number present. |

**File**: `src/stripe_engine.c:674`

---

### P2-07: 47-line DO_BATCH macro (L4)

| Field | Value |
|---|---|
| **Why** | `DO_BATCH` spans 47 lines including `break` and `goto`. Cannot step through with debugger. |
| **Impact** | Maintainability. Impossible to debug. |
| **Probability** | — |
| **Difficulty** | Medium — convert to function. 2 hours. |
| **Confidence** | **High** — macro confirmed at bench_io.c:62-108. |

**File**: `src/bench_io.c:62-108`

---

### P2-08: printf mixed with LOG_* (L3)

| Field | Value |
|---|---|
| **Why** | Some modules use `printf` directly, others use `LOG_INFO`/`LOG_ERROR`. |
| **Impact** | Inconsistent output. Some messages miss log file, timestamps, mutex. |
| **Probability** | — |
| **Difficulty** | **Low** — convert printf calls. 1 hour. |
| **Confidence** | **High** — mixed usage confirmed. |

**Files**: `src/raid_service.c`, `src/disk_scanner.c`, `src/planner_engine.c`

---

## P3 — Ignore

False positives, theoretical-only with no practical trigger, or intentional design.

---

### P3-01: OVERLAPPED use-after-free (C6)

| Field | Value |
|---|---|
| **Why** | Claimed stack OVERLAPPED causes kernel write-after-free. **FALSE POSITIVE** — `GetOverlappedResult(..., TRUE)` blocks until I/O completes. OVERLAPPED is still valid on stack throughout. |
| **Impact** | None — code is safe. |
| **Probability** | **Zero** — cannot occur. |
| **Confidence** | **High** — verified that all paths use synchronous wait. |

**File**: `src/storage_common.c:27-33`, `src/ram_cache.c:143`, `src/stripe_engine.c:437`

---

### P3-02: Thread handle leak (C8)

| Field | Value |
|---|---|
| **Why** | Claimed handle never closed. **FALSE POSITIVE** — `cache_destroy()` closes via `CloseHandle(cache->flush_thread)`. |
| **Impact** | None — handle is properly released. |
| **Probability** | **Zero** — cannot occur. |
| **Confidence** | **High** — verified CloseHandle in cache_destroy. |

**File**: `src/ram_cache.c:41`, `src/raid_service.c:442`

---

### P3-03: Cleanup NULL deref (C10)

| Field | Value |
|---|---|
| **Why** | Claimed wrong variable checked. **FALSE POSITIVE** — code correctly checks `if (last_slash)` before dereference. Bug report referenced non-existent variable name. |
| **Impact** | None — code is correct. |
| **Probability** | **Zero** — cannot occur. |
| **Confidence** | **High** — verified against actual source. |

**File**: `src/cleanup.c:142-143`

---

### P3-04: realloc memory leak (H11)

| Field | Value |
|---|---|
| **Why** | Claimed original pointer lost on realloc failure. **FALSE POSITIVE** — assignment `disks = new_disks` only occurs after NULL check. Original pointer is retained. |
| **Impact** | None — no leak. |
| **Probability** | **Zero** — cannot occur. |
| **Confidence** | **High** — verified control flow. |

**File**: `src/disk_scanner.c:43-45`

---

### P3-05: CS leak on callback exception (H14)

| Field | Value |
|---|---|
| **Why** | Claimed CS never released if callback throws. In C, only `longjmp` can cause this. |
| **Impact** | **Theoretical only.** Practical risk near zero in C without SEH. |
| **Probability** | **Negligible** — no setjmp/longjmp usage in callbacks. |
| **Confidence** | **Medium** — structural issue but no trigger exists. |

**File**: `src/event_bus.c:68-70`

---

### P3-06: Unused include (L2)

| Field | Value |
|---|---|
| **Why** | `#include "storage_common.h"` in stripe_engine.c — functions not called from this file. |
| **Impact** | Slightly slower compilation. No functional effect. |
| **Probability** | — |
| **Confidence** | **High** — grep confirms no calls. |

**File**: `src/stripe_engine.c`

---

### P3-07: Redundant healthy_count check (L7)

| Field | Value |
|---|---|
| **Why** | Duplicate `healthy_count` check in `mirror_write_to_all` — at entry and inside loop. TOCTOU but not incorrect. |
| **Impact** | Code clarity only. |
| **Probability** | — |
| **Confidence** | **High** — confirmed in mirror_engine.c. |

**File**: `src/mirror_engine.c:46-48`

---

### P3-08: Ad-hoc JSON parser (L5)

| Field | Value |
|---|---|
| **Why** | Manual `swscanf`/`wcschr` config parsing. Fragile. |
| **Impact** | Maintenance burden. Works for current config format. |
| **Probability** | — |
| **Confidence** | **High** — confirmed in config.c. |

**File**: `src/config.c`

---

### P3-09: Unnecessary metadata_manager abstraction (L6)

| Field | Value |
|---|---|
| **Why** | 5 functions, 3 are single-line delegates to superblock.c. |
| **Impact** | Unnecessary indirection. |
| **Probability** | — |
| **Confidence** | **High** — confirmed in metadata_manager.c. |

**File**: `src/metadata_manager.c`

---

## Priority Summary

| Priority | Count | Effort Estimate |
|---|---|---|
| **P0** Must fix | 15 | ~5-7 days engineering |
| **P1** Should fix | 16 | ~3-4 days engineering |
| **P2** Nice to fix | 8 | ~1 day engineering |
| **P3** Ignore | 9 | 0 (false positives/theoretical/quality) |
| **Total** | 48 | ~10-12 days total |

## P0 Fix Ordering (Recommended Sequence)

| Order | Bug ID | Dependency |
|---|---|---|
| 1 | **P0-13** (C4) — file_table_lock_init | None — quick 10-min fix, enables thread-safe FUSE init |
| 2 | **P0-12** (C1) — cache_flush_thread NULL | None — 2-min fix |
| 3 | **P0-15** (H4) — device_get NULL checks | None — 30-min fix across 8 sites |
| 4 | **P0-01** (C5) — g_state synchronization | Depends on #3 (need NULL-safe device_get first) |
| 5 | **P0-02** (C2) — parent_dir_exists overflow | None — 5-min fix |
| 6 | **P0-03** (C3) — raid_rename overflow | None — 15-min fix |
| 7 | **P0-04** (C9) — config swscanf overflow | None — 2-min fix |
| 8 | **P0-06** (H3) — vol->disks[i] NULL checks | Depends on #4 (g_state sync for thread safety) |
| 9 | **P0-10** (M5) — wraparound bounds check | None — 15-min fix |
| 10 | **P0-07** (H5) — journal synchronization | Depends on #4 (g_state sync for ordering) |
| 11 | **P0-05** (H2) — flush_buffer race | Depends on #10 (journal sync is prerequisite) |
| 12 | **P0-09** (H13) — journal 64→32 truncation | Depends on #10 (journal changes) |
| 13 | **P0-08** (H12) — mirror rebuild atomic swap | Depends on #4 (g_state sync) |
| 14 | **P0-11** (M14) — destructive cleanup | None — 15-min fix |
| 15 | **P0-14** (H6) — DeleteCS while active | Hard — requires reference counting. Do last. |
