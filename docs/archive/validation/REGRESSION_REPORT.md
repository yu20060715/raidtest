# Regression Test Report — Sprint 9

## Overview

Each Sprint 9 bug fix includes a dedicated regression test that verifies the fix and prevents re-introduction.

## Regression Tests

| Test | File | Tests | Bug | Scope |
|------|------|-------|-----|-------|
| B4 | `tests/regression/test_zero_pool.c` | 6 | Zero-byte Pool Crash | `validate_pool_size()` + `pool_file_create()` |
| B7 | `tests/regression/test_write_fail.c` | 8 | Unchecked WriteFile | Journal roundtrip + NULL volume handling |
| B10 | `tests/regression/test_cache_shutdown.c` | 16 | Cache Thread Race | `cache_init` field init + `cache_destroy` with event handle + write/read lifecycle |
| B2 | `tests/regression/test_event_handle.c` | 2 | CreateEventW NULL check | Sanity check + compile verification |

### B4 — Zero-byte Pool Crash

**Tests:**
1. validate_pool_size(0) → INVALID_ARG
2. validate_pool_size(< SECTOR_SIZE) → INVALID_ARG
3. validate_pool_size(1MB) → OK
4. pool_file_create(0) → false
5. pool_file_create(< SECTOR_SIZE) → false
6. pool_file_create(1MB) → true

**Status:** PASS (0 failures)

### B7 — Unchecked WriteFile

**Tests:**
1. Create RAIDTEST directory
2. Create volume with journal
3. journal_begin succeeds
4. journal_data succeeds
5. journal_commit succeeds
6. journal_data(NULL) → false
7. journal_begin(NULL) → false
8. journal_commit(NULL) → false

**Status:** PASS (0 failures)

### B10 — Cache Thread Race

**Tests:**
1. cache_init(1MB) → true
2. cache_init sets flush_thread = NULL
3. cache_init sets write_through = 0
4. cache_init allocates buffer
5. cache_init sets running = 1
6. cache_destroy frees buffer
7. cache_destroy clears flush_thread
8-11. cache_destroy with event handle → frees buffer + clears flush_thread
12-16. Normal write/read lifecycle with destroy

**Status:** PASS (0 failures)

### B2 — CreateEventW NULL check

**Tests:**
1. CreateEventW() returns valid handle
2. B2 fix compiles (verified by existing 36-test suite)

**Status:** PASS (0 failures)

## Test Suite Results

| Component | Tests | Passed | Failed |
|-----------|-------|--------|--------|
| Existing unit tests | 36 | 36 | 0 |
| Regression (Sprint 9) | 32 | 32 | 0 |
| **Total** | **68** | **68** | **0** |

## Compilation Warnings

`_WIN32_WINNT` redefinition warning: **FIXED** (0 own-code warnings).

Build command: `gcc -Wall -O2 -Isrc <sources> -o <target> -static-libgcc`

Before fix: `_WIN32_WINNT` redefined warning on every compilation unit.
After fix: Zero warnings — `#ifndef _WIN32_WINNT` guard in `common.h:2`.
