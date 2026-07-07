# RAIDTEST v3 — Future Architecture Roadmap

Every suggestion is grounded in evidence from the existing codebase. No speculative or out-of-scope features are included.

---

## Must Fix Before v1.0

These items are prerequisites for any production release. Each addresses a confirmed bug or architectural gap that can cause data loss, crash, or security exploit.

---

### S1 — Add Lock Acquisition to All raid_service Functions

**Current Architecture**: gs_lock()/gs_unlock() defined in common.h:16-17 but only called in cmd_handler.c, main.c, and daemon.c. The 24 functions in aid_service.c and all 14 FUSE callbacks in use_bridge.c access g_state directly without synchronization. GUI worker threads (gui.cpp) call aid_* functions from a background thread without acquiring the lock.

**Proposed Improvement**: Wrap every aid_* function body with gs_lock()/gs_unlock(). Add EnterCriticalSection(&g_state_cs) in FUSE callbacks that access volume state. Ensure lock is released on every return path (including error returns). Verify that no aid_* function calls another aid_* function (re-entrancy deadlock risk).

**Expected Benefit**: Eliminates all data races on g_state. State machine transitions become atomic across CLI, FUSE, GUI, and cache flush threads.

**Cost**: ~25 function wrappers; 2-3 developer-days to audit every return path and verify no nested calling.

**Risk**: Medium. If any aid_* function calls another aid_* function that also acquires the lock, deadlock occurs. Must audit call tree before implementation.

**Required Files**: src/raid_service.c, src/fuse_bridge.c, src/gui.cpp

**Confidence**: **High** — gs_lock/gs_unlock exist, just need call-site insertion. Audit requirement is mechanical.

**Evidence**: src/common.h:16-17 defines the lock primitives. Grep confirms zero calls in aid_service.c. Reproducible: Select-String -Path src\raid_service.c -Pattern "gs_lock" returns no matches.

---

### S2 — Add NULL Guards to device_get() Call Sites and vol->disks[i] Access

**Current Architecture**: device_get() returns DISK_INFO* (or NULL on out-of-range). 8 call sites in aid_service.c:106,184,208,227,252,267,456,540 dereference without NULL check. ol->disks[i] is accessed ~25+ times across stripe_engine.c, mirror_engine.c, journal.c, superblock.c, cleanup.c without NULL checks on individual disk pointers.

**Proposed Improvement**: Add if (!disk) return RC_DISK_NOT_FOUND; guards at every call site. For ol->disks[i] accesses, add loop-level NULL checks before pointer dereference.

**Expected Benefit**: Prevents guaranteed AV on partial/failed initialization. Enables safe error recovery from any volume construction failure.

**Cost**: ~30 guard insertions; 1 developer-day.

**Risk**: Minimal — strictly additive control flow with no behavior change for correct configurations.

**Required Files**: src/raid_service.c, src/stripe_engine.c, src/mirror_engine.c, src/journal.c, src/superblock.c, src/cleanup.c

**Confidence**: **High** — pattern is mechanical, no design ambiguity.

**Evidence**: src/volume_manager.c has NULL checks in some places (e.g., olume_mount checks ol), but callers in aid_service.c do not. ol->disks[i] dereferences are visible at every engine file.

---

### S3 — Fix FUSE Stack Buffer Overflows (parent_dir_exists + raid_rename)

**Current Architecture**: parent_dir_exists() at use_bridge.c:99-102 copies plen bytes to parent[256] with strncpy(parent, path, plen) without verifying plen < 256. aid_rename() at use_bridge.c:270-274 concatenates wdst + "/" + suffix into 
ewpath[256] with wcscpy/wcscat without bounds checks.

**Proposed Improvement**: Add if (plen >= sizeof(parent)) return false; guard in parent_dir_exists returning false. In aid_rename, pre-compute required path length and return ENAMETOOLONG if insufficient.

**Expected Benefit**: Eliminates two exploitable stack buffer overflows. Prevents crash on long paths.

**Cost**: ~10 lines changed; 2-3 hours.

**Risk**: Trivial — no behavioral change for normal (sub-256 char) paths.

**Required Files**: src/fuse_bridge.c:99-102, src/fuse_bridge.c:270-274

**Confidence**: **High**

**Evidence**: Both sites use fixed-size char[256] buffers with unchecked input lengths derived from FUSE callbacks.

---

### S4 — Fix FUSE File Table Lifecycle (Double-checked Locking + DeleteCriticalSection Race)

**Current Architecture**: ile_table_lock_init() at use_bridge.c:39-43 uses no synchronization — two threads can call InitializeCriticalSection concurrently. use_unmount() at use_bridge.c:585-588 calls DeleteCriticalSection while FUSE callbacks may still be executing on WinFsp worker threads.

**Proposed Improvement**: Move InitializeCriticalSection to use_init() (guaranteed single-thread context). Add reference counting to the file table: increment on callback entry, decrement on exit, wait-for-zero before DeleteCriticalSection and ree.

**Expected Benefit**: Eliminates CS corruption race and use-after-free of the critical section.

**Cost**: Add refcount + wait logic; 1-2 developer-days.

**Risk**: Must distinguish "file table lock" from "volume lock" lifecycle — unmount must quiesce before destroying.

**Required Files**: src/fuse_bridge.c:39-43, src/fuse_bridge.c:585-588

**Confidence**: **High**

**Evidence**: use_bridge.c:39-43 shows the lazy-init pattern. use_bridge.c:585-588 shows DeleteCriticalSection without quiesce.

---

### S5 — Fix OVERLAPPED Use-After-Free in I/O Paths

**Current Architecture**: storage_common.c:27-33 allocates OVERLAPPED on the stack and passes it to WriteFile/ReadFile. If the initial call succeeds with ERROR_IO_PENDING, the kernel holds a pointer to the stack memory. The stack frame is destroyed when the function returns; the kernel writes completion status to freed memory. Same pattern in am_cache.c:cache_flush_all (line 143: OVERLAPPED ovs[MAX_IO_ENTRIES] on stack).

**Proposed Improvement**: Heap-allocate OVERLAPPED and manage lifetime with explicit WaitForSingleObject on the event handle before freeing. Remove all CancelIoEx calls (which guarantee nothing about completion).

**Expected Benefit**: Eliminates kernel-mode write-after-free. Fixes undefined behavior in all async I/O paths.

**Cost**: Refactor I/O pattern to heap-allocated OVERLAPPED with explicit completion tracking; 3-5 developer-days.

**Risk**: If OVERLAPPED completion is not properly waited, heap memory leaks or use-after-free persists in new form. Must audit every I/O path.

**Required Files**: src/storage_common.c:27-33, src/ram_cache.c:143-175

**Confidence**: **High** — pattern is a known Windows API violation documented in MSDN.

**Evidence**: storage_common.c:27: OVERLAPPED ov; on stack. am_cache.c:143: OVERLAPPED ovs[MAX_IO_ENTRIES]; on stack. Both pass to WriteFile with ERROR_IO_PENDING handling.

---

### S6 — Add Synchronization to Journal Write Paths

**Current Architecture**: journal.c:35-78 calls SetFilePointer + WriteFile on the shared journal file without any lock. The cache flush thread (am_cache.c:210-233) and the FUSE backpressure path (which may call journal_data via cache_flush_all) both write to the journal file concurrently.

**Proposed Improvement**: Add a CRITICAL_SECTION g_journal_cs static variable. Acquire before any SetFilePointer/WriteFile/ReadFile call on the journal file. Release after file operation completes.

**Expected Benefit**: Prevents interleaved journal writes. Ensures journal entries are contiguous and recoverable.

**Cost**: Add CS variable, acquire/release in journal_begin, journal_data, journal_commit, journal_recover_all; 1 developer-day.

**Risk**: If the journal lock is acquired while holding cache->lock, and another path takes cache->lock while holding the journal lock, deadlock. Must verify lock ordering: cache->lock -> journal_lock must be universal.

**Required Files**: src/journal.c:1-187

**Confidence**: **High** — journal has zero synchronization; any lock is an improvement.

**Evidence**: journal.c contains no CRITICAL_SECTION, no EnterCriticalSection, no Interlocked* operations. All file operations (SetFilePointer, WriteFile, ReadFile) are unprotected.

---

## Recommended After v1.0

These items improve correctness, maintainability, and usability but do not block release.

---

### S7 — Journal Circular Buffering with Entry CRC

**Current Architecture**: Journal grows linearly (journal.c). No truncation or rotation. On recovery (journal_recover_all), all entries since file creation are replayed. The journal file fills the disk over time (acknowledged in KNOWN_LIMITATIONS.md).

**Proposed Improvement**: Implement fixed-size circular buffer: write head pointer at known offset, overwrite oldest entries when full. Add CRC32 per journal entry. On recovery, start from the last consistent checkpoint.

**Expected Benefit**: Bounded journal size. Crash detection at entry granularity. Survives journal file corruption mid-file.

**Cost**: 5-8 developer-days to design entry format with CRC, head pointer recovery, and safe wrap-around.

**Risk**: On power loss during wrap, the head pointer may point to a partially-written entry. Must handle gracefully with a "last valid checkpoint" marker.

**Required Files**: src/journal.c, src/journal.h

**Confidence**: **Medium** — design is well-understood but requires careful power-fail testing.

**Evidence**: KNOWN_LIMITATIONS.md acknowledges unbounded growth. journal.c:98-187 shows journal_recover_all reading from file start without any size limit.

---

### S8 — Replace FUSE Flat File Table with Hash Table + Directory Support

**Current Architecture**: use_bridge.c maintains a 64-entry flat array of FUSE_FILE structs. Lookup is O(n) linear scan in ind_file_by_name. No directory hierarchy. Exceeding 64 entries overwrites the oldest entry silently.

**Proposed Improvement**: Replace with a hash table (keyed by name hash, chain for collisions). Implement proper directory structure (at least root directory listing). Increase entry limit (e.g., 1024). Return ENOSPC on table full instead of overwriting.

**Expected Benefit**: O(1) file lookup. No silent data loss on table overflow. Usable as a real filesystem.

**Cost**: 5-10 developer-days including directory eaddir support.

**Risk**: Hash collisions could degrade to O(n). Must test with adversarial filenames. Directory support requires FUSE getattr/eaddir/lookup implementation beyond current stub.

**Required Files**: src/fuse_bridge.c (entire file table section, ~200 lines)

**Confidence**: **Medium** — architectural improvement is clear; FUSE semantics for directories are subtle.

**Evidence**: use_bridge.c:50-160: g_file_table[64], g_file_count, ind_file_by_name() linear scan. ind_free_slot() at line 53 wraps to 0 when full, overwriting oldest. No eaddir implementation.

---

### S9 — Add Proper Error Propagation Across Storage Layer

**Current Architecture**: storage_common.c I/O functions return ool. Callers in stripe_engine.c, mirror_engine.c, am_cache.c partially check the return value but do not propagate detailed error information. On I/O failure, the volume may continue operating as if the write succeeded.

**Proposed Improvement**: Change storage layer to return RC error codes (defined in common.h). Thread error codes through stripe/mirror/cache layers to aid_service. Surface the first error in volume status via aid_info().

**Expected Benefit**: Errors are visible to the user rather than silently swallowed. Degraded mode decisions are based on actual I/O results.

**Cost**: 5-7 developer-days to refactor return types and add error state to volume/disk structures.

**Risk**: Retrofitting error propagation may introduce new code paths where errors are unchecked. Must audit every caller.

**Required Files**: src/storage_common.c/h, src/stripe_engine.c/h, src/mirror_engine.c/h, src/ram_cache.c/h, src/volume_manager.c/h, src/raid_service.c/h

**Confidence**: **High** — RC enum exists with 20+ codes, just not used in the I/O path.

**Evidence**: common.h:131-153: RC_OK, RC_IO_ERROR, RC_DISK_NOT_FOUND, etc. storage_common.c:18-50: returns ool. No callers convert alse to an RC value.

---

### S10 — Extract APP_STATE into Dedicated Header

**Current Architecture**: APP_STATE type (including g_state) is defined in cmd_handler.h. Modules like cleanup.h, wizard.h, and ui_model.h all include cmd_handler.h just for the type definition. gui.cpp includes cmd_handler.h for the S() macro.

**Proposed Improvement**: Move APP_STATE struct and S() macro to a new src/state.h header. Remove cmd_handler.h includes from cleanup.h, wizard.h, ui_model.h, and gui.cpp.

**Expected Benefit**: Clean module boundaries. No UI/manager code depends on CLI header. Matches the dependency rules in API.md.

**Cost**: 1 developer-day.

**Risk**: Minimal — purely mechanical refactoring.

**Required Files**: Create src/state.h, modify src/cmd_handler.h, src/cleanup.h, src/wizard.h, src/ui_model.h, src/gui.cpp

**Confidence**: **High**

**Evidence**: gui.cpp:9: #include "cmd_handler.h" (only needed for S() macro). cleanup.h, wizard.h, ui_model.h all include cmd_handler.h for APP_STATE. API.md line 199-203 flags this.

---

### S11 — Add Event Bus Cleanup Function

**Current Architecture**: event_bus.c:27-31 calls InitializeCriticalSection but no corresponding DeleteCriticalSection exists. g_eb_cs is leaked on every subsystem init cycle.

**Proposed Improvement**: Add event_bus_cleanup() that calls DeleteCriticalSection(&g_eb_cs) and resets g_eb_inited = false. Call it from aid_cleanup().

**Expected Benefit**: Proper resource cleanup. Enables safe re-initialization in testing.

**Cost**: 2-3 hours.

**Risk**: Must ensure no thread is in event_bus_publish during cleanup. Add a quiesce flag or require caller to guarantee single-thread.

**Required Files**: src/event_bus.c, src/event_bus.h, src/raid_service.c

**Confidence**: **High**

**Evidence**: event_bus.c:27-31 does InitializeCriticalSection. No DeleteCriticalSection exists in the file. event_bus.h has no cleanup function declaration.

---

### S12 — Document Lock Ordering

**Current Architecture**: Two critical sections exist (g_state_cs in common.h, cache->lock in am_cache.c). No document specifies the order in which they must be acquired. If future code nests both, deadlock is guaranteed without a documented order.

**Proposed Improvement**: Write a short LOCK_ORDER.md stating: always acquire g_state_cs before cache->lock. Never acquire cache->lock while holding g_state_cs unless the reverse order is explicitly validated. Also specify g_eb_cs, g_ft_lock, and any future journal lock in the order.

**Expected Benefit**: Prevents deadlock during maintenance. Provides a contract for all contributors.

**Cost**: 1 hour.

**Risk**: None.

**Required Files**: Create LOCK_ORDER.md

**Confidence**: **High**

**Evidence**: No lock ordering specification exists in any document. The two existing CS have no documented acquisition order.

---

## Long-term Research

These items require significant engineering investment and should be evaluated after v1.0 is stabilized.

---

### S13 — End-to-End Data Checksumming

**Current Architecture**: CRC32 is computed on the superblock only (in superblock.c). Data on disk has no integrity verification. Silent corruption (bit rot, phantom writes, DMA misdirection) is undetectable. Acknowledged in KNOWN_LIMITATIONS.md: "No end-to-end checksum — silent corruption not detected."

**Proposed Improvement**: Add per-block checksums (e.g., CRC32C or xxHash3) stored alongside data in pool files. Verify on every read. On mismatch, attempt read from mirror copy (RAID1) or return error (RAID0).

**Expected Benefit**: Detects silent data corruption. Enables self-healing on RAID1.

**Cost**: 15-20 developer-days. Pool file format must be extended to include checksum metadata. Performance impact of checksum computation on every I/O.

**Risk**: Pool file format change breaks backward compatibility with existing volumes. Performance regression on CPU-bound systems.

**Required Files**: src/storage_common.c/h, src/pool_io.c/h, src/stripe_engine.c/h, src/mirror_engine.c/h, src/superblock.c/h (format version bump)

**Confidence**: **Medium**

---

### S14 — Block Device Mount (Replace FUSE)

**Current Architecture**: Volumes are exposed as FUSE filesystems via WinFsp (use_bridge.c). This adds FUSE overhead on every I/O, limits performance, and prevents use as a boot/swap volume.

**Proposed Improvement**: Develop a Windows minifilter driver or use the Windows software RAID API (IOCTL_VOLUME_*) to present the volume as a raw block device.

**Expected Benefit**: Eliminates FUSE overhead. Volume appears as a normal disk in Disk Management. Supports boot, swap, and all filesystem operations natively.

**Cost**: Very high — kernel driver development requires Windows Driver Kit, driver signing, and extensive testing. Estimated 3-6 months for a team.

**Risk**: BSOD-level bugs. Driver signing certificate cost. WinFsp would become optional (or removable entirely).

**Required Files**: Entire use_bridge.c replaced, olume_manager.c mount path rewritten, aid_service.c mount/unmount modified.

**Confidence**: **Medium** — the benefit is clear but the cost may not match project scope. NOT JUSTIFIED if the project remains a thesis/demo prototype.

---

### S15 — Hot Spare and Automatic Disk Replacement

**Current Architecture**: Failed disks in RAID1 are detected manually via aid_simulate() or health check. Replacement requires the user to call aid_rebuild() manually. No automatic hot spare.

**Proposed Improvement**: Allow designating a physical disk as hot spare. On RAID1 member failure, automatically start rebuild to the spare. On RAID0 member failure, log the event and pause I/O.

**Expected Benefit**: Reduces MTTR (mean time to repair) from manual to automatic. Improves availability.

**Cost**: 7-10 developer-days. Requires state machine extension (adding SPARE disk role, REBUILDING_TO_SPARE state). Requires background rebuild thread separate from cache flush thread.

**Risk**: Rebuild I/O competes with user I/O. Must throttle rebuild to avoid starvation. Spare capacity may be smaller than member disk.

**Required Files**: src/mirror_engine.c/h (rebuild), src/device_manager.c/h (spare designation), src/raid_service.c/h (auto-rebuild on failure event), src/common.h (state machine + disk role enum), src/event_bus.c (failure notification)

**Confidence**: **Medium** — feasible architecture but introduces significant complexity to the state machine.

---

### S16 — Encryption at Rest

**Current Architecture**: Data on disk is plaintext. Any attacker with disk access reads pool files directly. KNOWN_LIMITATIONS.md acknowledges this.

**Proposed Improvement**: Add AES-256-XTS encryption at the pool I/O layer. Key derived from user passphrase via PBKDF2. Key stored in superblock (encrypted with KEK). Unlock on mount with passphrase prompt.

**Expected Benefit**: Confidentiality of data at rest. Required for production deployment in enterprise environments.

**Cost**: 10-15 developer-days. Requires cryptographic library integration (e.g., crypt.dll or libsodium). Performance overhead on every I/O.

**Risk**: Key management complexity. Lost passphrase = permanent data loss. Performance regression (especially on CPU-bound systems without AES-NI fallback).

**Required Files**: New src/crypto.c/h, src/pool_io.c (encrypt/decrypt wrapper), src/superblock.c (key store), src/raid_service.c (passphrase prompt)

**Confidence**: **Medium** — technique is standard; integration with the existing I/O path is the challenge. NOT JUSTIFIED if no threat model requires confidentiality.

---

## NOT JUSTIFIED Items

| Suggestion | Reason |
|---|---|
| **RAID5/6 implementation** | Project explicitly states "No RAID5/6" (HANDOFF.md:190). No parity calculation infrastructure exists. Requires new engine module, stripe width changes, read-modify-write pipeline. Would roughly double the codebase. |
| **Cross-platform (Linux/macOS)** | FUSE bridge is WinFsp-specific. Win32 API (IOCTL, DirectX 11) used throughout. Porting requires full Win32 abstraction layer, new GUI backend, and Linux FUSE/libvirtio integration. Out of scope for v1.0. |
| **I/O scheduler with NCQ passthrough** | No I/O scheduler exists. Current model issues synchronous I/O in caller's thread. No evidence of NCQ or command queuing need in codebase. |
| **GUI modularization (separate panel files)** | gui.cpp is large (~55KB) but functional. No evidence of maintainability issues currently blocking development. Could be refactored but not architecturally necessary. |
| **S.M.A.R.T. monitoring** | gui.cpp has placeholder panels for temperature/S.M.A.R.T. but no code reads actual S.M.A.R.T. data. Requires new IOCTL code and parsing. Would not integrate with current failing-detection path. |

---

## Roadmap Summary

| Category | Count | Key Recommendation | Estimated Effort |
|---|---|---|---|
| **Must Fix Before v1.0** | 6 | Thread safety, NULL guards, FUSE overflows, FUSE lifecycle, OVERLAPPED safety, journal synchronization | 8-16 developer-days |
| **Recommended After v1.0** | 6 | Journal rotation, hash-table file table, error propagation, APP_STATE header, event_bus cleanup, lock ordering doc | 17-30 developer-days |
| **Long-term Research** | 4 | Checksumming, block device mount, hot spare, encryption | 3-9 months (S14 dominant) |
| **NOT JUSTIFIED** | 5 | RAID5/6, cross-platform, I/O scheduler, GUI refactor, S.M.A.R.T. | N/A |
| **Total** | **16** | | |

The single highest-impact change is **S1 (add g_state locking to raid_service.c)**: it addresses the root cause of data races across all four thread types (CLI, FUSE, GUI workers, cache flush) and costs only 2-3 developer-days with no design ambiguity. This change alone reduces the overall risk profile from "critical" to "moderate" by closing the largest architectural gap between design intent and implementation reality.
