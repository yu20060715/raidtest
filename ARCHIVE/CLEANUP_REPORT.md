# Cleanup Report

## Summary

Cleanup based on `ARCHITECTURE_REVIEW.md` analysis. Each change was followed by a full rebuild (`build.bat`) and test run (`raidtest_tests.exe`). All 38 tests passed after every change.

---

## Changes Made

### 1. Remove duplicate `get_filetime_now()` function

**Files**: `src/common.h`, `src/journal.c`, `src/superblock.c`

**What**: The identical static function `get_filetime_now()` was defined in both `journal.c:5` and `superblock.c:38`. Moved to `common.h` as a `static inline` function, removed both duplicates.

**Rationale**: Textbook code duplication — same 5-line function in two files.

---

### 2. Extract common disk-opening loop from `volume_create()` / `volume_mirror()`

**Files**: `src/volume_manager.c`

**What**: Both `volume_create()` and `volume_mirror()` contained an identical pattern:
1. `cleanup_volume_cache()` + `stripe_volume_destroy()`
2. Open pool files with error rollback
3. Call engine-specific create function
4. Generate UUID, write metadata, publish event

Extracted into `volume_create_internal()` taking a function pointer (`volume_create_fn`). Created a thin wrapper `stripe_volume_create_wrap()` to adapt the 4-parameter `stripe_volume_create()` to the 3-parameter signature.

**Rationale**: Removed ~35 lines of duplicated code, centralizes error handling.

---

### 3. Fix misleading indentation warnings in `gui.cpp`

**Files**: `src/gui.cpp`

**What**: 8 `-Wmisleading-indentation` warnings in GCC build. All caused by patterns like:
```c
if (cond) stmt(); ImGui::SameLine();   // SameLine is NOT guarded
```
Fixed by moving `ImGui::SameLine()` and `ImGui::NextColumn()` calls to their own lines after the if-else blocks.

**Warnings eliminated**: `ShowToolbar()` (4), `ShowPlanner()` (2), `ShowVolumeInfo()` (1).

---

### 4. Remove dead includes

**Files**: `src/cmd_handler.c`, `src/superblock.c`

**What**:
- `cmd_handler.c`: Removed 6 dead includes — `disk_scanner.h`, `bench_io.h`, `fuse_bridge.h`, `wizard.h`, `daemon.h`, `cleanup.h`. All CLI command wrappers delegate through `raid_service.h`; no direct use of these headers.
- `superblock.c`: Removed `mirror_engine.h` — no `mirror_*` symbols used anywhere in the file (confirmed by grep).

---

### 5. No dead TODO/FIXME/HACK/XXX found

Searched all `src/*.c` and `src/*.h` files. Zero matches for TODO, FIXME, HACK, or XXX in source code.

---

## Changes Not Made

| Item | Reason |
|---|---|
| **Split GUI into multiple files** | The architecture review's claim of a 1950-line `ShowGUI()` was incorrect — the file has 1701 lines with 21 well-separated panel functions. `RenderMainUI()` is 95 lines. |
| **Extract phase computation in stripe_engine.c** | The phase computation loops in `stripe_volume_create()` and `stripe_volume_expand()` are ~60 lines each with 10+ mutable local variables. Extracting would require a function with too many pointer parameters, increasing risk without proportional benefit. |
| **Remove unused `VOLUME_OP` enum** | `volume_manager.h` declares `VOLUME_OP` with `VOLUME_OP_CREATE`, `VOLUME_OP_MIRROR`, etc. but this enum is never used in any function signature. However, it may be intended for future use by callers. Marked as low-priority. |

---

## Verification

| Step | Result |
|---|---|
| `build.bat` (full build) | Passed — no errors, only third-party warnings (ImGui) |
| `raidtest_tests.exe` (38 tests) | **38 passed, 0 failed** |
| Test run after each change | All 38 passed after every intermediate build |

---

## Remaining Technical Debt (not addressed)

From the architecture review, the following items were verified but intentionally NOT changed because they would alter behavior/API/UI:

| Issue | Location | Reason Skipped |
|---|---|---|
| `metadata_upgrade()` always returns false | `metadata_manager.c:14` | Fixing would change superblock upgrade behavior |
| `bench_io.c` write error always returns true | `bench_io.c:74-75` | Fixing would change benchmark error handling |
| `event_bus_init()` CS never deleted | `event_bus.c` | Adding cleanup would change API |
| `uuid_generate()` uses weak RNG | `common.h:139` | Fixing would change UUID generation behavior |
