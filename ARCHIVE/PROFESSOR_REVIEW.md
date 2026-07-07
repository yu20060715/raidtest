# RAIDV3 — Professor Review

| Category | Grade | Score |
|---|---|---|
| Innovation | F | 40/100 |
| Architecture | D | 55/100 |
| Engineering | F | 35/100 |
| GUI | D | 58/100 |
| Reliability | F | 20/100 |
| Maintainability | D | 52/100 |
| Testing | D | 55/100 |
| Documentation | C | 68/100 |
| User Experience | D | 50/100 |
| Code Quality | F | 38/100 |

**Cumulative: F (47.1/100)**

---

## Innovation (40/100)

**Asymmetric stripe ratios across mixed-speed disks is the one genuinely novel idea in this project.** Normalizing disk speeds (500/1000/2000 MB/s → ratios 1:2:4) is a reasonable approach to heterogeneous RAID0. The MAPPING_PHASE structure that allows disks to drop out when full is also a practical design.

However, the claims far exceed what is actually implemented:

- **RAID10 is listed as "✅ Complete" in the planner** (`README.md:23`). It is not. The "planner" estimates capacity only. There is no RAID10 read, write, mount, or rebuild code anywhere in the project. This is a capacity calculator, not a RAID10 implementation.

- **"Rollback — atomic multi-step operations"** (`README.md:29`). There is no rollback. `cleanup.c` does session cleanup (closing handles, freeing memory), not transactional rollback of failed operations. The word "rollback" appears nowhere in the source code.

- **"Write-back Cache — configurable (256 MB–4 GB), async flush thread"** (`README.md:27`). The cache exists, but the async flush thread has confirmed data-loss bugs (see Reliability). Configurable size is technically true but the implementation is broken.

- **"Serial-based Restore — load uses physical disk serial numbers"** (`README.md:31`). This works in `superblock_restore`, but the serial number field was only added in v4. All v1/v2/v3 superblocks have empty serials, causing fallback to drive-letter matching. The fallback path has no validation that the drive letter is the same physical disk.

- **"Health Check — check command verifies disk accessibility"** (`README.md:32`). The `raid_check` function in `raid_service.c:480` calls `require(STATE_MOUNTED)` and returns immediately. There is no health verification beyond checking the state machine. The actual disk health check does nothing meaningful.

- **The project claims "RC2" status** (`README.md:6`) and "v1.0 RC4" (`gui.cpp: APP_VERSION`). Release Candidate status implies feature-complete with only bug fixes remaining. This project has 7 confirmed P0 bugs (data loss, stack smashing), which disqualifies it from RC status.

**Points deducted:**
- (25) Claims features that are not implemented (RAID10, rollback, health check)
- (15) Misleading version labeling (RC2/RC4 with P0 bugs)
- (10) The core novel idea (asymmetric striping) is undermined by implementation bugs
- (5) No original data-structure or algorithm contribution beyond the ratio normalization
- (5) No performance evaluation or comparison against existing RAID solutions

---

## Architecture (55/100)

The modular decomposition from Sprint 5 is the strongest architectural decision in the project. The `raid_service` API layer, `event_bus` pub/sub, and separation of CLI/GUI into thin front-ends are well-motivated.

**What works:**
- `raid_service.h/c` provides a single API surface for both GUI and CLI
- `event_bus.h/c` is a clean pub/sub with 13 event types
- `stripe_engine.c` / `mirror_engine.c` are reasonably self-contained
- The superblock versioning (v1→v4) with backward-compat readers shows forward-thinking design

**What does not work:**

- **Global monolith `APP_STATE g_state`** (`cmd_handler.h:34-42`). Every module reads/writes this single struct. There are no ownership boundaries. The lock (`g_state_cs`) is defined in the same header as the data it protects. 34 functions in `raid_service.c` access `g_state` without acquiring the lock. This is not architecture — it is a shared mutable heap with a suggestion of threading.

- **The README architecture diagram is aspirational.** It shows `device_manager`, `volume_manager`, `metadata_manager`, `planner_engine`, `ui_model` as separate modules. Some of these (`metadata_manager`, `device_manager`, `volume_manager`, `ui_model`) are referenced in `#include` directives but their actual `.c`/`.h` files were not found in the project. The diagram includes modules that either don't exist or are stubs.

- **`cmd_handler.h` is a kitchen-sink header.** It declares `extern APP_STATE g_state`, the critical section, the `S()` accessor macro, and serves as the de-facto "everything" header. A change to any field in `APP_STATE` recompiles every translation unit.

- **Header dependency inversion is missing.** `raid_service.c` includes `disk_scanner.h`, `config.h`, `wizard.h`, `cleanup.h`, `event_bus.h`, `stripe_engine.h`, `volume_manager.h`, `profiler.h`. It depends on 10+ modules directly. There is no interface boundary.

- **`gui.cpp` is a 1777-line monolith.** The GUI initialization, all panels (6+), worker thread, theme setup, D3D device management, event handling, logging, and export functionality are in a single file. The GUI "architecture" is one static struct (`g_gui`) with 30+ fields and one giant `worker_thread` switch statement.

- **Test files live in `src/`.** `test_runner.c`, `test_common.c`, `test_stripe.c`, `test_cache.c`, `test_mirror.c`, `test_superblock.c`, `test_journal.c` are all in `src/` alongside production code. There is no `tests/` directory for the main test suite. (The `tests/` directory that exists contains 4 additional test files with a different build pattern.)

**Points deducted:**
- (15) Global mutable state shared across all modules with no locking discipline
- (10) Architecture diagram documents modules that don't exist as separate files
- (10) 1777-line C++ GUI monolith with no component separation
- (5) Test and production code mixed in the same directory
- (5) Header dependency inversion — `cmd_handler.h` is a universal include

---

## Engineering (35/100)

**Stack buffer overflows:** The project has three confirmed stack buffer overflows that an attacker can trigger from user input.

1. `parent_dir_exists` at `fuse_bridge.c:103-107` — `char parent[256]` overflowed when `plen > 255`
2. `raid_rename` at `fuse_bridge.c:269` — `wcscat(wsrc, L"/")` when `wcslen(wsrc) == 255`
3. `raid_rename` at `fuse_bridge.c:273-278` — `newpath[256]` flooded with up to 511 wide chars
4. `config.c:86` — `swscanf` with unbounded `%[^\"]` overflows `lang[8]`

These are not subtle Heisenbugs. They are textbook C security vulnerabilities from the 1990s. Any capstone project that ships stack buffer overflows in 2025-2026 has not done basic security due diligence.

**Error handling:**
- `journal_begin`, `journal_data`, `journal_commit` return values are silently discarded at `ram_cache.c:93,128,199`
- `cache_flush_all` has no re-entrancy guard and is called from 8+ call sites with no mutual exclusion
- `disk_scanner.c:43` uses O(n²) realloc pattern — single-element growth per iteration
- `journal.c:100` performs unbounded `malloc((size_t)file_size.QuadPart)` from an untrusted file

**Thread safety:**
- Zero `gs_lock()` calls in 34 functions of `raid_service.c`
- `cache_flush_all` releases the lock between snapshot and journal write, allowing concurrent write to set dirty bits that are then cleared
- `cache_flush_in_progress` flag set after dirty-bit clear, creating a TOCTOU window
- `profiler.c:45,78` uses non-atomic writes to release slots claimed with `InterlockedCompareExchange`
- `fuse_bridge.c:67` uses `InterlockedIncrement` inside a critical section (unnecessary but harmless)

**Build system:**
- `build.bat` uses batch files; `_build.sh` uses shell scripts
- No CMake, no Meson, no Makefile. No dependency management.
- No CI configuration. No static analysis integration. No sanitizer build in the default path.

**Points deducted:**
- (20) Three confirmed stack buffer overflows reachable from user input
- (15) Zero thread synchronization in the service layer — every state access is a data race
- (10) Discarded error return values in critical WAL path
- (10) Unbounded allocation from untrusted file
- (5) No build system beyond batch files
- (5) No CI, no static analysis, no fuzzing

---

## GUI (58/100)

The GUI exists and is functional. That is the highest praise I can give it.

**Positives:**
- Dear ImGui + DirectX 11 is a reasonable choice for a prototype
- Dark theme is visually coherent
- Disk table with 10 columns is informative
- State-based button enabling/disabling is sensible
- Toast notifications are a nice touch
- Export diagnostic feature is useful

**Problems:**

- **The GUI is a 1777-line single-file monolith.** Every panel, the worker thread, event handling, D3D init/cleanup, theme setup, and the main loop are in one translation unit with one static struct for global state. There is no separation of presentation from logic.

- **Worker thread uses a 34-case switch statement** (`gui.cpp:378-736`). Adding a new operation requires adding an enum value, a case block, and updating the caller. This is procedural programming in C++.

- **"Cache: N/A"** (`gui.cpp:979`) — the performance dashboard has a permanent "Cache: N/A" label. The cache hit percentage calculation at `gui.cpp:948-949` is wrong: `avg_iops_read / (avg_iops_read + avg_iops_write) * 100.0` computes the fraction of read IOPS, not cache hit percentage. The code computes one thing but labels it another.

- **Perf history allocation:** `gui.cpp:953-955` allocates three float arrays every frame (`calloc` + `free` each frame). If allocation fails, the function returns early without freeing already-allocated arrays (three separate `free` calls at the end, but the early-return path at line 956 frees all three regardless of which failed — this is correct by accident).

- **No error display for failed operations.** The `check_worker_done` function (`gui.cpp:750-766`) shows a `MessageBoxA` on failure, but the GUI does not surface most errors inline. The toast system is used inconsistently.

- **`system()` call for ZIP export** (`gui.cpp` worker thread). Calling `system()` from a GUI application to invoke PowerShell is a security concern and blocks until PowerShell completes.

- **No accessibility**, no keyboard navigation beyond what ImGui provides, no high-DPI awareness configuration, no localization despite `config.c` parsing a "language" field.

**Points deducted:**
- (15) 1777-line monolith with no component architecture
- (10) Giant switch-statement worker thread is procedural, not object-oriented
- (5) "Cache: N/A" is a permanently non-functional UI element
- (5) Per-frame allocation in the performance panel
- (3) `system()` call for ZIP export
- (2) No error surfacing pattern in the UI
- (2) No accessibility or high-DPI support

---

## Reliability (20/100)

This is the worst category. The project has confirmed data-loss bugs that would result in permanent data corruption under normal use.

**Confirmed data-loss scenarios:**

1. **`cache_flush_all` loses concurrent writes** (`ram_cache.c:124-133`). The lock is released between `memcpy` (snapshot) and `journal_data` (WAL write). A concurrent `cache_write` can set dirty bits on the same blocks; those bits are then cleared by the flush thread. On crash between dirty-bit clear and physical I/O, the latest write is gone forever. Journal replay restores stale data.

2. **`journal_data` failure silently ignored** (`ram_cache.c:128`). If the journal DATA write fails (disk full, I/O error), dirty bits are still cleared (lines 132-133) and physical I/O proceeds. On subsequent crash, the journal contains a BEGIN with no matching DATA for this batch → data is unrecoverable.

3. **No I/O timeout in mirror engine** (`mirror_engine.c:63,88,151,155`). All reads and writes use synchronous `stripe_read_raw`/`stripe_write_raw` with no timeout. A hung disk blocks the calling thread permanently. In the cache flush path, this blocks all subsequent I/O.

4. **`cache_flush_all` is not re-entrant** (`ram_cache.c:86`). Called from 8+ sites with no mutual exclusion. Two concurrent calls clobber `flush_buffer` and interleave journal entries, producing an unrecoverable journal.

5. **State machine unsynchronized** (`raid_service.c`, all 34 functions). Every state transition is a data race when called from FUSE callbacks, GUI worker thread, and CLI handler concurrently. State inconsistency leads to undefined behavior.

**Additional concerns:**
- The WAL journal has no circular buffering — it grows unbounded
- Journal recovery allocates `file_size.QuadPart` bytes with no sanity check
- Partial RAID writes on I/O failure leave stripe inconsistency until the next flush
- No graceful degradation when disks are removed mid-operation
- No power-fail testing beyond a single 147-line stress test

**The project README warns "Data loss possible. Test with non-critical data only"** (`README.md:10-12`). This warning is appropriate but insufficient. The code does not just risk data loss under edge cases — it guarantees data loss under the normal concurrent workload of a mounted FUSE filesystem.

**Points deducted:**
- (25) Confirmed data-loss in normal concurrent write-back cache operation
- (20) Journal error path silently discards failures
- (15) No I/O timeout anywhere in the storage engine
- (15) Cache flush not re-entrant — concurrent calls corrupt state
- (10) State machine operations unsynchronized — every state access is a data race
- (10) WAL journal has unbounded growth
- (5) No graceful degradation for disk removal

---

## Maintainability (52/100)

**Positive:**
- The Sprint 5 refactoring (separating `raid_service` from `cmd_handler`) was the right move
- Function names are generally descriptive
- `superblock.c` has backward-compat readers for v1-v4, which is good for long-term maintenance

**Negative:**

- **Code duplication in `daemon.c`:** `daemon_run` and `daemon_main` have independently maintained copies of volume-load, mount, cleanup, and shutdown sequences. A bug fix applied to one will not be applied to the other.

- **Test files in `src/`:** Mixing test code with production source creates a maintenance hazard. Build scripts must filter out test files for production builds.

- **Hardcoded paths:** `test_superblock.c:22-25` hardcodes `C:\RAIDTEST\superblock.dat` and `C:\RAIDTEST\stripe_pool.dat`. `test_journal.c:8` hardcodes `C:\RAIDTEST\`. These tests cannot run on a system without write access to C:\, and they collide if multiple developers run tests simultaneously.

- **Undefined module boundaries:** The `#include` graph in `raid_service.c` includes 11 headers directly. `stripe_engine.c` includes `mirror_engine.h`, `storage_common.h`, `ram_cache.h`, `profiler.h`. Modules know about each other's internals.

- **`common.h` is a 260-line dumping ground.** It defines `APP_STATE` (the global monolith), `DISK_INFO`, `STRIPE_VOLUME`, `IO_MAPPING_ENTRY`, `MAPPING_PHASE`, `RAM_CACHE`, the `S()` macro, `gs_lock/gs_unlock`, error codes, utility functions, and more. A header called "common" that contains everything is not maintainable — it's a circular-dependency escape hatch.

- **No module documentation.** No file-level comment in any `.c` or `.h` file explains the module's responsibility, ownership, or threading contract.

**Points deducted:**
- (15) Duplicated code between `daemon_run` and `daemon_main` (~50 lines duplicated)
- (10) Test files in `src/` mixed with production code
- (10) Hardcoded absolute paths in tests (C:\RAIDTEST\)
- (8) `common.h` is a 260-line omnibus header with no cohesion
- (5) No file-level documentation in any source file

---

## Testing (55/100)

**What exists:**
- 36 tests across 5 test suites
- Tests cover basic stripe creation, mirror read/write, cache write/read, journal roundtrip, superblock read/write
- Tests use real files (no mocking)
- Tests are registered via `__attribute__((constructor))` which is creative but fragile
- 4 additional stress/concurrent tests in `tests/` directory

**What is wrong:**

- **Tests write to `C:\RAIDTEST\`.** All superblock and journal tests hardcode `C:\RAIDTEST\`. This requires Administrator privileges, writes to the boot drive, and means tests cannot run in parallel or in CI without contaminating each other.

- **The `test_cache_dirty_and_flush` test does not test flushing.** At `test_cache.c:118-121`, it manually clears dirty bits:
  ```c
  EnterCriticalSection(&c.lock);
  c.dirty_map[0] = 0;
  c.dirty_map[0] = 0;  // <-- same line twice, copy-paste error
  LeaveCriticalSection(&c.lock);
  ```
  This test claims to test "flush_all clears dirty bits" but never calls `cache_flush_all`. It writes to `dirty_map` directly. The same line is duplicated — line 119 and 120 are identical. This is either a copy-paste bug or evidence that the test is not actually testing the flush path.

- **No negative testing.** There are no tests for: out of disk space, disk removed mid-operation, invalid user input, corrupted journal recovery, concurrent cache flush, state machine violations, pool file corruption, stripe mapping overflow.

- **No parameterized tests.** Each test is a separate function with a separate registration. There is no data-driven test infrastructure.

- **No coverage measurement.** The project has no `gcov`, `lcov`, or any other coverage tool configuration. It is unknown what percentage of code is exercised by tests.

- **Stress tests are minimal.** One power-fail test (`stress/test_powerfail.c`, 147 lines). The concurrent test (`tests/test_concurrent.c`, 123 lines) runs 4 threads for 250 ops each — this is not a stress test.

- **Tests use `printf` for output** and parse results manually. There is no test assertion framework beyond the `ASSERT` macro.

- **The `__attribute__((constructor))` test registration** (`test_common.h:27`) is GCC-specific and does not work on MSVC. This ties the project to MinGW-w64.

**Points deducted:**
- (15) Tests write to hardcoded C:\RAIDTEST\ — not portable, not CI-safe
- (10) "Flush" test does not actually call flush function — copy-paste bug in test
- (5) No negative or error-path tests
- (5) No coverage measurement
- (5) GCC-specific constructor attribute ties to MinGW
- (3) No parameterized or data-driven test infrastructure
- (2) Stress tests are minimal

---

## Documentation (68/100)

**Positives:**
- README.md is comprehensive and well-structured
- ARCHITECTURE_REFACTOR_PLAN.md is detailed
- BUG_AUDIT.md catalogs issues with line numbers
- Superblock format has comments and backward-compat documentation in code
- CLI command reference in README is useful

**Problems:**

- **README.md line counts are wrong.** "gui.cpp (~770 lines)" at `README.md:222` — the actual file is 1777 lines. A README that is wrong about basic file sizes suggests the author has not updated it.

- **README.md lists modules that don't exist as separate files.** `device_manager.h/c`, `volume_manager.h/c`, `metadata_manager.h/c`, `ui_model.h/c`, `planner_engine.h/c` are listed in the module map (`README.md:86-99`) but some of these were not found as separate files in the project. The architecture diagram (`README.md:60-83`) includes these layers, but the code does not reflect the diagram.

- **`docs/archive/` contains 23 files** including `ARCHITECTURE.md`, `RC_REPORT.md`, `FINAL_ARCHITECTURE_AUDIT.md`, and validation reports. These are historical documents that may or may not reflect the current state of the code. Having 23 unmaintained documents in an `archive/` directory is not documentation — it is a graveyard.

- **No API reference documentation** for the 30+ `raid_*` functions beyond their declarations. No Doxygen, no Sphinx.

- **No inline documentation** in any source file. No file headers, no module descriptions, no function-level comments explaining preconditions, postconditions, or thread-safety requirements.

- **`KNOWN_LIMITATIONS.md`** is referenced (`README.md:213`) but it is unclear whether this file actually exists. The README points to it but the review found only the documents listed in the project glob.

**Points deducted:**
- (10) File size in README is off by 2.3× (770 vs 1777 lines)
- (8) Architecture diagram documents modules that don't exist as separate files
- (5) 23-file archive directory is a document graveyard, not living documentation
- (5) No API reference or generated docs
- (2) No inline documentation in any source file
- (2) Cross-references to files that may not exist

---

## User Experience (50/100)

**Positives:**
- Beginner/Advanced/Developer mode tabs
- Welcome wizard on first launch
- Toast notifications
- Confirmation dialogs for destructive operations
- State-based button enabling/disabling

**Problems:**

- **No error messages for failures.** When `cache_flush_all` fails (disk full, I/O error), the function logs "Cache flush: X blocks flushed, Y batch(es) failed" and returns `void`. The GUI worker thread receives no error indicator for most failure modes. `cache_flush_all` returns `void` — there is no way for the caller to know whether data was actually persisted.

- **"Cache: N/A" in the performance dashboard** is a permanent placeholder. If the cache hit rate cannot be computed, the field should be hidden, not labeled "N/A".

- **The mount/unmount cycle requires manual intervention.** After unmount, the user must manually run "Export" to get diagnostics. There is no post-mortem analysis or automatic journal recovery feedback.

- **No undo for any operation.** Destroy shows a confirmation dialog but there is no "Recycle Bin" or undo. Create/Destroy are irreversible from the UI.

- **Export uses `system()` to invoke PowerShell Compress-Archive.** This takes 5-10 seconds with no progress feedback beyond a progress bar that jumps from 80% to 100%.

- **No idle-timeout or auto-close.** The application has no screensaver awareness or auto-unmount on idle.

- **The welcome wizard suggests "Quick Setup" as the recommended action** but Quick Setup has never been tested with actual hardware failures (disk pull, power loss) because the project has no fault injection testing.

**Points deducted:**
- (15) `cache_flush_all` returns `void` — no error propagation to user
- (10) "Cache: N/A" is a permanently broken UI element
- (5) No undo for destructive operations
- (5) Export uses blocking `system()` call
- (5) No progress feedback for long operations
- (5) Welcome wizard recommends an untested workflow
- (5) No keyboard shortcuts beyond ImGui defaults

---

## Code Quality (38/100)

**Naming and style:**
- Inconsistent naming conventions: `camelCase` (`cache_init`, `stripe_volume_create`) mixed with `snake_case` in some local variables. Some functions use Hungarian-like prefixes (`test_cache_*`), others don't.
- `S()` macro (`cmd_handler.h:43-45`) is completely opaque. A new reader has no idea what `S()` returns or where the state comes from.
- Magic constants abound: `MAX_LOG_LINES 500`, `MAX_LOG_LINE_LEN 256`, `CONSOLE_SCROLLBACK 4096` — defined at file scope in `gui.cpp` but not exported or documented.

**Error handling discipline:**
- Three functions in the WAL path discard return values (`ram_cache.c:93,128,199`)
- `cache_flush_all` returns `void` — the flush thread and callers cannot distinguish success from failure
- `disk_scanner.c` has `realloc` failure that is silently handled by continuing the loop with the old pointer (correct behavior, but undocumented)
- `profiler.c` uses non-atomic writes for flags that were claimed atomically

**Memory safety:**
- Three stack buffer overflows (see Engineering)
- Unbounded `malloc` from untrusted file (see Engineering)
- `gui.cpp` allocates arrays every frame with `calloc` instead of reusing buffers
- `cache_init` has a complex multi-step allocation with manual cleanup on each failure path — some paths leak (if `cache->flush_buffer` allocation fails, `cache->dirty_map` is freed but `cache->buffer` is also freed — this is correct, but there are 4 allocation steps with 5 different cleanup combinations)

**Thread safety:**
- Global mutable state with no ownership discipline (see Architecture)
- `CRITICAL_SECTION g_state_cs` protects nothing specific since all access is unsynchronized
- `cache_flush_in_progress` is read without the cache lock in `stripe_volume_write` (line 499 of `stripe_engine.c`) — this is a data race on the flag itself, not just the data it guards

**Type safety:**
- `DWORD dummy = 0` passed to `WriteFile` for overlapped I/O (`ram_cache.c:163`) — should be `NULL` per MSDN
- `uint64_t` values cast to `DWORD` and back throughout the mapping code
- `int` used for array indices and sizes where `uint32_t` would be type-safe

**Compiler warnings:**
- The code compiles with GCC under C11 and C++17, but the review did not check for `-Wall -Wextra -Wpedantic` compliance. Given the patterns observed (implicit truncations, discarded return values), warning-free compilation is unlikely.

**Points deducted:**
- (15) Three stack buffer overflows in production code
- (10) No error handling discipline — void returns from critical functions
- (10) Opaque `S()` macro obscures data flow
- (8) Thread safety violations throughout
- (5) Type safety issues (DWORD dummy, implicit truncations)
- (5) Per-frame allocation in GUI
- (4) Compiler warning profile unknown
- (3) Inconsistent naming conventions

---

## Summary

| Category | Grade | Key Issue |
|---|---|---|
| Innovation | F | Claims unimplemented features (RAID10, rollback) |
| Architecture | D | Global mutable state, no layering enforcement |
| Engineering | F | Stack buffer overflows, discarded error returns |
| GUI | D | 1777-line monolith, "Cache: N/A" broken element |
| Reliability | F | Confirmed data-loss in normal cache operation |
| Maintainability | D | Duplicated code, hardcoded paths, common.h dumping ground |
| Testing | D | Tests write to C:\, flush test doesn't test flush |
| Documentation | C | README line counts wrong, archive graveyard |
| User Experience | D | No error propagation, void returns from critical paths |
| Code Quality | F | Stack overflows, no error discipline, opaque macros |

**Cumulative: 47.1/100 — F**

This project demonstrates ambition but fails on execution. The asymmetric striping concept is interesting, but the implementation contains fundamental engineering flaws that make it unsuitable for any use beyond a classroom demonstration. The stack buffer overflows alone would require a complete rewrite of the affected components to bring to production quality. The data-loss bugs in the cache/journal subsystem indicate that the core WAL protocol was designed but never verified under concurrent load.

The project would benefit from: (1) fixing all P0 security bugs immediately, (2) rewriting the cache flush with proper locking and re-entrancy protection, (3) adding comprehensive error propagation, (4) establishing a proper test infrastructure with mocking and CI, and (5) reducing the scope of claims to match what is actually implemented.

---

## Top 20 Things That Must Be Improved Before Final Submission

### Critical (Must Fix — Grade = F without these)

1. **Fix stack buffer overflow in `parent_dir_exists`** (`fuse_bridge.c:103-107`) — Add `if (plen >= sizeof(parent)) return false;` before `strncpy`. This is an exploitable security vulnerability.

2. **Fix stack buffer overflow in `raid_rename` `wsrc`** (`fuse_bridge.c:269`) — Check `wcslen(wsrc) < 255` before `wcscat`. Same for `newpath` at lines 273-278.

3. **Fix stack buffer overflow in `config.c`** (`config.c:86`) — Add width specifier `%7[^\"]` to `swscanf` to bound writes to 7 characters.

4. **Fix `cache_flush_all` stale-snapshot race** (`ram_cache.c:124-133`) — Hold the cache lock across `memcpy` → `journal_data` → dirty-bit-clear. Or use a two-phase approach with a snapshot of dirty-bit ownership.

5. **Check all journal return values** (`ram_cache.c:93,128,199`) — If `journal_data` fails, do not clear dirty bits and do not proceed with the flush. Propagate the error to the caller.

6. **Add synchronization to all 34 `raid_service.c` functions** — Every public `raid_*` function must acquire `g_state_cs` on entry and release on exit. Without this, the state machine is a data race.

7. **Guard journal recovery against OOM** (`journal.c:100`) — Reject journal files larger than a reasonable maximum (e.g., 64 MB) before calling `malloc`.

### High (Should Fix — Grade = D without these)

8. **Make `cache_flush_all` re-entrant** — Add an atomic flag (`InterlockedCompareExchange`) at function entry to reject concurrent calls. Currently clobbers `flush_buffer` and interleaves journal entries.

9. **Fix `cache_flush_in_progress` ordering** (`ram_cache.c:131-137`) — Set the flag to 1 before clearing dirty bits, not after. This closes a TOCTOU window.

10. **Fix non-atomic profiler slot release** (`profiler.c:45,78`) — Use `InterlockedExchange(&g_profiler.samples[slot].active, 0)` instead of a plain write.

11. **Add I/O timeout to mirror engine** (`mirror_engine.c:63,88,151,155`) — Use overlapped I/O with `WaitForMultipleObjects` and a timeout parameter. A hung disk must not block the system permanently.

12. **Remove code duplication in `daemon.c`** — Extract shared logic (volume-load, mount, cleanup, shutdown) into a single helper function called by both `daemon_run` and `daemon_main`.

13. **Fix cache test that doesn't test flush** (`test_cache.c:118-121`) — Replace the manual `dirty_map[0] = 0` with an actual call to `cache_flush_all` against a real volume. Fix the duplicated line.

14. **Move test files out of `src/`** — Create a proper `tests/` directory with its own build rule. Do not link test code into the production binary.

### Medium (Should Fix — Grade rises to C)

15. **Add `if (strlen(src) > 255)` check in `raid_rename`** (`fuse_bridge.c:254-255`) — Only `dst` is length-checked. `src` can silently truncate.

16. **Add `if (strlen(p) > 255)` check in `raid_open`** (`fuse_bridge.c:172-191`) — Unlike `raid_create`/`raid_mkdir`, `raid_open` has no path-length guard. This enables the `parent_dir_exists` overflow.

17. **Replace O(n²) `realloc` pattern in disk scanner** (`disk_scanner.c:43`) — Double the allocation size on each growth, or pre-count disks with an initial scan.

18. **Fix `lpNumberOfBytesWritten` in overlapped `WriteFile`** (`ram_cache.c:163`) — Pass `NULL` instead of `&dummy` per MSDN specification for overlapped I/O.

19. **Implement proper test isolation** — Replace hardcoded `C:\RAIDTEST\` paths with `GetTempPathW` for journal and superblock tests. Ensure tests clean up after themselves and can run in parallel.

### Nice-to-Have (Grade rises to B with these)

20. **Replace `APP_STATE` global monolith with owned context objects** — Decompose into `DiskPoolContext`, `VolumeContext`, `RuntimeContext`. Pass by parameter, not by global. This is the single highest-impact architectural improvement possible.
