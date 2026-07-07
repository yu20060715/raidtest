# Static Analysis Report

**Repository:** RAIDTEST v1.0 RC4
**Analysis Date:** 2026-07-06
**Scope:** `src/*.c`, `src/*.h`, `src/gui.cpp`, `tests/*.c`, `stress/*.c`

---

## Severity Classification

| Severity | Meaning |
|----------|---------|
| **Must Fix** | Bug-correctness issue: wrong behavior, data corruption, crash, or security |
| **Should Fix** | Maintainability / correctness risk: could cause issues under edge cases |
| **Cosmetic** | Style / warning suppression: no runtime impact but improves code quality |

---

## Include Dependency

### Dependency Graph (direct includes only)

```
common.h → windows.h, stdint.h, stdbool.h, stdio.h, stdlib.h, string.h,
           tchar.h, strsafe.h, process.h, errno.h, logger.h, uuid.h, crc32.h

*.h (most project headers) → common.h  [plus specific extras below]

daemon.h           → common.h, cmd_handler.h
cleanup.h          → common.h, cmd_handler.h
wizard.h           → common.h, cmd_handler.h
metadata_manager.h → common.h, superblock.h
raid_service.h     → common.h, cmd_handler.h, device_manager.h, metadata_manager.h,
                     volume_manager.h, planner_engine.h, raid_query.h
volume_manager.h   → common.h, stripe_engine.h
test_common.h      → common.h, stripe_engine.h, mirror_engine.h
```

### Cyclic Include

**None found.** `#pragma once` is used consistently in all headers. No preprocessor cycles exist.

One minor design note: `logger.c` includes `common.h`, which in turn includes `logger.h` — this creates a self-referential include cycle:
- `logger.c` → `common.h:108` → `logger.h`
This is harmless (both headers have `#pragma once`) but unconventional. `logger.c` could include `logger.h` directly and remove the `common.h` dependency.

---

## Unused / Redundant Includes

### Redundant — header already available through transitive include

| File | Include | Reason |
|------|---------|--------|
| `journal.c:3` | `#include "logger.h"` | Already available via `journal.h:2` → `common.h:108` → `logger.h` |
| `mirror_engine.c:4` | `#include "logger.h"` | Already available via `mirror_engine.h:2` → `common.h:108` → `logger.h` |

Both are harmless (explicit includes document intent), but redundant given the transitive chain.

---

## Unused Variable

### Must Fix

| File | Line | Code | Problem |
|------|------|------|---------|
| `gui.cpp` | 132 | `char console_input[256];` | Struct field declared, **never read or written anywhere in the codebase** |
| `gui.cpp` | 133 | `char console_output[CONSOLE_SCROLLBACK];` | Struct field declared, never used |
| `gui.cpp` | 134 | `int console_output_len;` | Struct field declared, never used |
| `gui.cpp` | 135 | `char console_history[CONSOLE_HISTORY][256];` | Struct field declared, never used |
| `gui.cpp` | 136 | `int console_history_count;` | Struct field declared, never used |
| `gui.cpp` | 137 | `int console_history_pos;` | Struct field declared, never used |
| `gui.cpp` | 138 | `bool console_scroll_to_bottom;` | Struct field declared, never used |

**Impact:** Dead struct fields waste stack space in the GUI state struct. The console feature was apparently planned but never wired up. Recommend removing the 7 fields (~1.5 KB) or implementing the console.

### Should Fix

| File | Line | Code | Problem |
|------|------|------|---------|
| `disk_scanner.c` | 90–93 | `STORAGE_ADAPTER_DESCRIPTOR adapter;` if-DeviceIoControl block writes `adapter` but never reads it | Dead query — comment says "Use adapter info for sector size" but `disk->sector_size` is hardcoded to 512 on line 94. The adapter query is wasted IOCTL |
| `profiler.c` | 115–118 | `uint64_t prev_read = g_profiler.last_read_bytes;` ... `(prev_read ? 0 : 0)` | The ternary always yields 0. Variables `prev_read` / `prev_write` are dead — the IOPS formula computes `(read_ops - 0) / dt` instead of a proper delta. Either tracking fields are missing or the subtraction should be removed |

### Cosmetic

| File | Line | Code | Problem |
|------|------|------|---------|
| `stripe_engine.c` | 80 | `uint32_t ratios[MAX_DISKS], total_ratio;` | `total_ratio` written by `stripe_volume_normalize_ratios` but never read afterward |
| `stripe_engine.c` | 213 | Same pattern in `stripe_volume_expand` | Same |
| `test_cache.c` | 119–120 | `c.dirty_map[0] = 0;` (appears twice consecutively) | Duplicate assignment; the second is a no-op. Likely copy-paste intended to clear block 2 separately |
| `test_common.c` | 99 | `uint64_t zero = 0;` then `WriteFile(..., &zero, 0, ...)` | Variable used only as a dummy non-NULL address; zero bytes are written so value is irrelevant |

---

## Unreachable Code

**None found.** All code after `return`, `break`, `goto` targets was checked. No dead branches detected.

---

## Switch Fallthrough

**None found.** All non-empty `case` blocks end with `break` or `return`. Empty fall-through cases (e.g., `case A: case B:`) are used correctly. No missing `break` detected.

---

## Signed / Unsigned Conversion

### Must Fix

| File | Line | Code | Problem |
|------|------|------|---------|
| `raid_service.c` | 488–489 | `ops = (uint32_t)atol(args[0]);` `max_kb = (uint32_t)atol(args[1]);` | `atol` returns `long`. If user passes negative input (e.g., `-1`), `(uint32_t)-1` yields `0xFFFFFFFF`. The `if (ops < 1)` guard on line 490 treats this as "less than 1" but `0xFFFFFFFF` as `uint32_t` is actually `4294967295` — **the comparison `ops < 1` will be false**, causing a massive loop count. The other `raid_service.c` functions (`raid_benchfs`) correctly use `safe_atou32` instead |

### Should Fix

| File | Line | Code | Problem |
|------|------|------|---------|
| `test_common.c` | 43 | `InterlockedIncrement((volatile LONG*)&s_disk_serial)` where `s_disk_serial` is `uint32_t` | Casting `uint32_t*` → `volatile LONG*` is technically a signed/unsigned mismatch. `LONG` = `long` (signed 32-bit). Safe in practice because the serial never exceeds INT32_MAX in tests, but warns on MSVC `/W4` |

### Cosmetic

| File | Line | Code | Problem |
|------|------|------|---------|
| `event_bus.c` | 23 | `if (type >= 0 && type < EVENT_COUNT)` where `type` is `EVENT_TYPE` (enum) | On MSVC, unsigned enums make `type >= 0` a tautological comparison — always true. Generates warning C4068 or similar |

---

## Narrowing Conversion

### Should Fix

| File | Line | Code | Problem |
|------|------|------|---------|
| `main.c` | 30 | `pos += snprintf(id_size_buf + pos, (size_t)(256 - pos), ...)` where `pos` is `int` | If `pos > 256` (from repeated snprintf calls), `256 - pos` underflows to negative, then `(size_t)` wraps it to a huge positive value → **buffer overflow past stack buffer**. The `if (pos >= 256)` guard on line 33 runs *after* the write. Fix: check `pos < 256` before writing, or use `uint32_t` for `pos` |
| `wizard.c` | 96 | `prompt_int("...", (int)default_pool, 1024, (int)free_space)` where `free_space` is `uint64_t` | Truncates values > INT_MAX |
| `wizard.c` | 123 | `prompt_int("...", CACHE_DEFAULT_MB, 256, (int)max_cache)` where `max_cache` is `uint32_t` | Same truncation |
| `journal.c` | 103 | `(DWORD)file_size.QuadPart` where `QuadPart` is `LONGLONG` | **Truncates journal files > 4 GB** to `DWORD` (uint32_t). `ReadFile` receives an undersized `read` parameter. While journal files are typically small, a large flush cycle could exceed 4 GB |
| `stripe_engine.c` | 675 | `uint32_t max_size = max_size_kb * 1024;` where `max_size_kb` is `uint32_t` | Multiplication overflows `uint32_t` before the bounds check on line 677. If `max_size_kb` > 4194304, the result wraps. Fix: bounds-check before multiply, or use `uint64_t` |

### Cosmetic

| File | Line | Code | Problem |
|------|------|------|---------|
| `stripe_engine.c` | 332, 358, 364, 368, 395, 401, 405 | `(uint32_t)chunk` where `chunk` is `uint64_t` | Safe because `chunk ≤ remaining ≤ request_length` (uint32_t). Multiple occurrences |
| `daemon.c` | 106 | `(char)state->disk.physical_disks[i]->drive_letter[0]` where `drive_letter[0]` is `wchar_t` | Safe for ASCII but technically narrowing |
| `disk_scanner.c` | 56, 62 | `(uint32_t)strlen(...)` — `size_t` → `uint32_t` | Safe in practice (strings < 4 GB) |
| `journal.c` | 100 | `(size_t)file_size.QuadPart` — `LONGLONG` → `size_t` | Safe on 64-bit; narrowing on 32-bit |
| `ram_cache.c` | 8 | `(uint32_t)(size_bytes / cache->block_size)` — `uint64_t` → `uint32_t` | Safe due to cache size limits |

---

## Implicit Cast

No significant implicit casts beyond the narrowing conversions listed above. The codebase uses explicit casts consistently for integer truncation.

---

## Const Correctness

### Should Fix

| File | Line | Current Signature | Suggested |
|------|------|------------------|-----------|
| `config.c` | 25 | `bool config_save(APP_CONFIG* cfg)` | `bool config_save(const APP_CONFIG* cfg)` — `cfg` never modified |
| `disk_scanner.c` | 137 | `bool disk_select(DISK_INFO** disks, uint32_t count, uint32_t* indices, uint32_t sel_count)` | `const uint32_t* indices` — `indices` is read-only |
| `disk_scanner.c` | 168 | `void disk_print_list(DISK_INFO** disks, uint32_t count)` | `const DISK_INFO* const* disks` — disks are printed, not modified |
| `device_manager.c` | 45 | `bool device_select(uint32_t* indices, uint32_t count)` | `const uint32_t* indices` — read-only |
| `metadata_manager.c` | 14 | `bool metadata_upgrade(SUPERBLOCK* sb)` | `const SUPERBLOCK* sb` — only reads `sb->version` |
| `planner_engine.c` | 3 | `void planner_calculate(PLANNER_DISK* disks, ...)` | `const PLANNER_DISK* disks` — read-only |
| `planner_engine.c` | 47 | `void planner_print(const PLANNER_RESULT* result, PLANNER_DISK* disks, ...)` | `const PLANNER_DISK* disks` — read-only |

### Cosmetic

| File | Line | Current Signature | Suggested |
|------|------|------------------|-----------|
| `journal.c` | 22 | `static bool journal_write_entry(STRIPE_VOLUME* vol, ...)` | `const STRIPE_VOLUME* vol` — `vol` is read-only |
| `journal.c` | 44 | `bool journal_begin(STRIPE_VOLUME* vol)` | `const STRIPE_VOLUME* vol` |
| `journal.c` | 52 | `bool journal_data(STRIPE_VOLUME* vol, ...)` | `const STRIPE_VOLUME* vol` |
| `journal.c` | 77 | `bool journal_commit(STRIPE_VOLUME* vol)` | `const STRIPE_VOLUME* vol` |
| `stripe_engine.c` | 41 | `bool stripe_volume_normalize_ratios(uint32_t* speeds, ...)` | `const uint32_t* speeds` |

Note: `journal.c` functions technically only read from `vol` (accessing `disk_count`, `drive_letter`), but marking them `const` would require changing the callback-style helper `journal_write_entry` signature, which cascades. Low priority.

---

## Missing `static`

### Cosmetic

| File | Line | Function | Reason |
|------|------|----------|--------|
| `stripe_engine.c` | 671 | `bool stripe_volume_random_test(...)` | Declared in `stripe_engine.h`, has external consumers — correct as non-static |
| `stripe_engine.c` | 77 | `bool stripe_volume_normalize_ratios(...)` | Declared in `stripe_engine.h` — correct |

All file-internal helpers across the codebase are correctly marked `static`. No instances of missing `static` were found for functions confined to a single translation unit.

---

## Missing `inline` / `constexpr`

### Missing `inline`

Header functions in `common.h` are correctly marked `static inline`. No missing `inline` detected.

### Missing `constexpr` (C++ — `gui.cpp`)

The `gui.cpp` file uses C++ but does not use `constexpr` anywhere. Several compile-time constants could benefit:

| File | Line | Code | Suggestion |
|------|------|------|------------|
| `gui.cpp` | 42 | `#define CONSOLE_SCROLLBACK (64 * 1024)` | Replace with `constexpr size_t CONSOLE_SCROLLBACK = 64 * 1024;` |
| `gui.cpp` | 43 | `#define CONSOLE_HISTORY 16` | Replace with `constexpr int CONSOLE_HISTORY = 16;` |
| `gui.cpp` | 44 | `#define MAX_TOASTS 8` | Replace with `constexpr int MAX_TOASTS = 8;` |
| `gui.cpp` | 45 | `#define PERF_HISTORY 120` | Replace with `constexpr int PERF_HISTORY = 120;` |

The remaining `#define` constants in `gui.cpp` and `common.h` are in C headers/contexts where `constexpr` is not applicable.

---

## Shadow Variable

### Should Fix

| File | Line | Code | Problem |
|------|------|------|---------|
| `cmd_handler.c` | 176 | `APP_CONFIG* cfg = &g_state.cfg.config;` declared inside a loop that already has access to `g_state.cfg.config` | Shadows no local variable but creates a redundant pointer. The real issue: **loop always uses index 0** (`cfg->disks[0]`, see Logical Bug below) |

---

## Logical Bugs (from original BUG_REPORT.md — confirmed by static analysis)

These were previously identified and **fixes have been applied**. Included here for completeness:

| ID | File | Severity | Status |
|----|------|----------|--------|
| BUG-1 | `ram_cache.c:128-134` — dirty bits cleared before `journal_data()` | Critical | **Fixed** |
| BUG-2 | `ram_cache.c:92-97` — TOCTOU race `has_work` / `journal_begin()` | Critical | **Fixed** |
| BUG-3 | `journal.c:160-176` — journal truncated after failed replay write | High | **Fixed** |
| BUG-4 | `fuse_bridge.c:90-93` — `is_dir_by_path()` race without lock | Medium | **Fixed** |

---

## Additional Issues Found

### Must Fix

#### BUG-A: cmd_handler.c — auto_restore loop uses index 0 for all iterations

**File:** `cmd_handler.c:175-181`

```c
for (uint32_t i = 0; i < g_state.cfg.config.disk_count; i++) {
    APP_CONFIG* cfg = &g_state.cfg.config;
    if (!g_state.disk.physical_disks || !cfg || cfg->disk_count == 0) break;
    char dl_str[2] = { cfg->disks[0].drive_letter, 0 };
    raid_mapdrive(cfg->disks[0].disk_id, dl_str);
    break;  // <-- loop always exits after first iteration
}
```

**Root Cause:** Three bugs in one:
1. `!cfg` check is dead code (`cfg = &g_state.cfg.config`, never NULL)
2. `cfg->disks[0]` should be `cfg->disks[i]` — always uses disk 0
3. Unconditional `break` at end of loop body prevents multi-disk iteration

**Impact:** When config has multiple disks, only the first is mapped. Volume creation/restore will fail or create a degraded volume because not all disks are mapped.

**Severity: Must Fix** — functional bug.

**Suggested Fix:**
```c
for (uint32_t i = 0; i < g_state.cfg.config.disk_count; i++) {
    DISK_CONFIG* dc = &g_state.cfg.config.disks[i];
    if (!g_state.disk.physical_disks || g_state.cfg.config.disk_count == 0) break;
    char dl_str[2] = { dc->drive_letter, 0 };
    raid_mapdrive(dc->disk_id, dl_str);
}
```

#### BUG-B: gui.cpp — snprintf signed underflow → buffer overflow

**File:** `gui.cpp:483-485, 537-541, 650, 1092`

```c
// Pattern (4 occurrences):
int pos = 0;
for (...) {
    pos += snprintf(init_buf + pos, 255 - pos, ...);  // pos is int, 255-pos underflows when pos > 255
}
```

**Root Cause:** `pos` is `int`. When accumulated output exceeds 255 bytes, `255 - pos` becomes negative. Passed to `snprintf` (which takes `size_t`), it wraps to a huge positive value → unbounded write past `init_buf[256]` stack buffer.

**Impact:** Stack buffer overflow → crash or arbitrary code execution (security).

**Severity: Must Fix** — security.

**Suggested Fix:**
```c
int pos = 0;
for (...) {
    if (pos >= 255) break;
    int n = snprintf(init_buf + pos, (size_t)(255 - pos), "...");
    if (n > 0) pos += n;
}
```

### Should Fix

| File | Line | Issue | Detail |
|------|------|-------|--------|
| `daemon.c` | 162, 170 | Inconsistent NULL check | `WaitForSingleObject(stop_ev, INFINITE)` on line 162 has no NULL check; line 170 checks `if (stop_ev)`. If `CreateEvent` returned NULL, line 162 would `WAIT_FAILED`. Fix: add NULL check before line 162 |
| `volume_manager.c` | 133, 189 | Misleading `(void)physical_count;` | Variable IS used later (lines 142, 192). The suppression is unnecessary and misleading |

### Cosmetic

| File | Line | Issue |
|------|------|-------|
| `test_superblock.c` | 455 | Misplaced comment — `/* Blank serial restore ... */` appears before all 10 TEST wrappers but the test function is at line 505 |
| `cmd_handler.c` | 177 | `!cfg` is dead code — `cfg` always points to `&g_state.cfg.config` which is never NULL |

---

## Summary

| Category | Must Fix | Should Fix | Cosmetic |
|----------|----------|------------|----------|
| Unused variable | 7 (gui.cpp struct fields) | 2 | 4 |
| Unreachable code | 0 | 0 | 0 |
| Switch fallthrough | 0 | 0 | 0 |
| Signed/unsigned | 1 (`raid_service.c:488-489`) | 1 | 1 |
| Narrowing conversion | 1 (`main.c:30` buffer overflow) | 4 | 6 |
| Const correctness | 0 | 7 | 5 |
| Missing static | 0 | 0 | 0 |
| Missing inline/constexpr | 0 | 0 | 4 |
| Shadow variable | 0 | 1 | 0 |
| Logical bug (new) | 2 (cmd_handler loop, gui.cpp overflow) | 2 (daemon NULL, volume_manager) | 2 |
| Include issues | 0 | 0 | 2 (redundant includes) |
| **Total** | **11** | **17** | **24** |

### Priority Action Items

1. **Must Fix:** `cmd_handler.c:175-181` — fix loop to use `cfg->disks[i]` instead of `cfg->disks[0]` and remove spurious `break`
2. **Must Fix:** `gui.cpp:483,537,650,1092` — add `pos < 255` guard before each `snprintf` to prevent stack overflow
3. **Must Fix:** `raid_service.c:488-489` — replace `atol` + cast with `safe_atou32` (as done in `raid_benchfs`)
4. **Should Fix:** `main.c:30` — check `pos < 256` before snprintf write, or use `uint32_t` for `pos`
5. **Should Fix:** `stripe_engine.c:675` — bounds-check `max_size_kb` before multiply
6. **Should Fix:** `journal.c:103` — handle journal files > 4 GB by reading in chunks
