# Sprint 10B Report — RC2 Stabilization

## Objective
Stabilize RAIDTEST to RC2 by fixing all verified P0/P1 bugs from the Sprint 10A architecture audit. No new features, no refactoring of unrelated code.

## P0 Bugs (Fixed)

### P0#1: Superblock double-close — `src/superblock.c:167-168`

**Issue:** `try_read_superblock_from_drive` called `CloseHandle` inside the `ReadFile` failure branch and separately after the `if` block, creating a use-after-close on I/O error.

**Fix:** Replaced with single-exit pattern — `ReadFile` result saved in a `BOOL`, `CloseHandle` called once unconditionally, then the status checked. All subsequent validation paths (checksum mismatch, version fallthrough) are unaffected since the handle is already closed.

**File changed:** `src/superblock.c` — lines 149-153

### P0#2: Journal payload CRC missing — `src/journal.c:152-153`

**Issue:** During `journal_recover_all`, DATA entries had their payload bounds checked (`offset + payload <= read`) but the `data_crc` field was never validated against the actual payload bytes. A corrupted journal file could replay garbage data as valid.

**Fix:** Added `crc32(raw + offset, payload) == je.data_crc` check inside the DATA entry acceptance branch. Entries with mismatched payload CRC are silently skipped (not added to the replay ranges list), preventing corrupted data from being applied.

**File changed:** `src/journal.c` — inside `journal_recover_all`

## P1 Bugs (Fixed)

### P1#3: CRC duplication — `src/journal.c:5-17` + `src/superblock.c:5-18`

**Issue:** Identical `static uint32_t crc32()` functions existed in both `journal.c` (with LCG-based table generation, polynomial `0xEDB88320`) and `superblock.c` (bit-by-bit table generation, same polynomial). Each call recomputed the 256-entry CRC table on every invocation.

**Fix:** Consolidated into `common.h` as a `static inline` function. Both `.c` files now get `crc32` via their existing `#include` chain (`journal.h → common.h`, `superblock.h → common.h`). No new `.c` file, zero call overhead.

**Files changed:** `src/common.h` (added), `src/journal.c` (removed), `src/superblock.c` (removed)

### P1#4: Dead code removal

**Issue:** 15 public API functions had zero callers across the entire codebase, confirmed by grep across all `.c` and `.h` files.

**Action:** All 15 removed from both `.c` implementations and `.h` declarations. One stub (`metadata_upgrade`) kept for declaration intent. Two test utilities kept for future use.

**See:** `DEAD_CODE_AUDIT.md` for full list and rationale.

## Regression Tests (Added)

### Test: `sb_double_close_regression` — `test_superblock.c`
1. Write valid superblock
2. Corrupt checksum field
3. Verify `superblock_read_raw` fails gracefully (no crash)
4. Rewrite valid superblock
5. Verify `superblock_read_raw` succeeds (no stale handle state)

### Test: `journal_corrupted_payload` — `test_journal.c`
1. Write initial data to volume
2. Manually construct a raw journal file with `BEGIN` + `DATA(bad payload CRC=0xDEADBEEF)` + no `COMMIT`
3. Run `journal_recover_all`
4. Verify the corrupted DATA entry is skipped (payload CRC mismatch)
5. Verify original volume data is unchanged

## Verification

- **Test suite:** 38/38 passed (11 superblock + 5 journal + 8 cache + 6 mirror + 4 stripe + 3 normalize + 1 regression)
- **Compiler warnings:** Clean with `gcc -Wall -O2`
- **Main build:** All modified `.c` files compile cleanly with `-DUSE_WINFSP` flags
- **Behavior preserved:** All existing tests unchanged in pass/fail status

## Files Changed

| File | Change |
|---|---|
| `src/common.h` | Added `static inline uint32_t crc32()` |
| `src/superblock.c` | Removed duplicated `crc32()`; single-exit pattern in `try_read_superblock_from_drive` |
| `src/journal.c` | Removed duplicated `crc32()`; added payload CRC validation in `journal_recover_all` |
| `src/device_manager.c` | Removed 9 dead functions |
| `src/device_manager.h` | Removed 9 dead declarations |
| `src/metadata_manager.c` | Removed 2 dead functions |
| `src/metadata_manager.h` | Removed 2 dead declarations |
| `src/event_bus.c` | Removed 3 dead functions |
| `src/event_bus.h` | Removed 3 dead declarations |
| `src/logger.c` | Removed 2 dead functions |
| `src/logger.h` | Removed 2 dead declarations |
| `src/test_superblock.c` | Added `sb_double_close_regression` test |
| `src/test_journal.c` | Added `journal_corrupted_payload` test |
| `DEAD_CODE_AUDIT.md` | New — full dead code audit |
| `SPRINT10B_REPORT.md` | This file |

## RC2 Criteria Status

| Criterion | Status |
|---|---|
| P0 bugs fixed | Both superblock double-close and journal CRC validated |
| P1 bugs fixed | CRC consolidated, dead code removed |
| Regression tests pass | 38/38 |
| New tests for each fix | 2 new tests added |
| Dead code audited | DEAD_CODE_AUDIT.md generated |
| No features added | ✓ |
| No unrelated refactoring | ✓ |
