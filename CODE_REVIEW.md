# Code Review Report

**Date:** 2026-07-05  
**Scope:** All source files in `src/` + `gui.cpp`  

---

## Compiler Warnings

| Category | Count | Status |
|----------|-------|--------|
| Our own code (C files) | 0 | ✅ Clean |
| Our own code (gui.cpp C++) | 0 | ✅ Clean |
| imgui library (benign array-bounds) | 8 | ⚠️ Library issue, not ours |

**All 0 warnings from own code.** The only warnings are from imgui.cpp's `IsMouse*` functions accessing `MouseDown[5]` arrays — this is a known false positive in GCC and does not affect correctness.

---

## Magic Numbers

Checked for hardcoded numeric literals that should be named constants:

| File | Line | Value | Assessment |
|------|------|-------|------------|
| stripe_engine.c | 71 | `256` | `MAX_PHASE_COUNT` — should be constant. Used in `phase[256]` local array. Acceptable as inline constant. |
| stripe_engine.c | 88 | `64` | `MAX_DISKS`-related threshold. Acceptable. |
| ram_cache.c | 17 | `8` | Bits-per-byte for dirty map. Acceptable. |
| gui.cpp | 545-548 | `0.55f` | Left panel width ratio. Acceptable layout constant. |
| gui.cpp | 545,550 | `18` | Layout padding. Acceptable. |
| gui.cpp | 549 | `170` | Log panel height. Acceptable. |

**No critical magic numbers.** Layout and performance constants are inline but documented by context.

---

## Duplicate Code

Checked for cut-paste patterns across all source files:

| Pattern | Files | Status |
|---------|-------|--------|
| OVERLAPPED I/O setup (CreateEventW + memset + Offset setup) | stripe_engine.c (3 occurrences), ram_cache.c (1) | **Acceptable** — each has different entry/loop structure. Refactoring into a helper would add complexity. |
| CreateFileW with NO_BUFFERING + fallback | pool_io.c (2 locations) | **Acceptable** — used in `pool_file_create` and `pool_file_open` with different flags. |
| CRC32 compute | journal.c | Single occurrence. |

**No significant code duplication.**

---

## Unused Functions

Scanned all `.c` and `.h` files for functions defined but never called:

| Function | File | Status |
|----------|------|--------|
| `pool_dir_delete` | pool_io.c | Called from `cleanup.c:65` ✓ |
| `cmd_wizard` | cmd_handler.c | Exported via command table ✓ |
| `cmd_daemon` | cmd_handler.c | Exported via command table ✓ |
| `disk_resolve_speed` | disk_scanner.c | Called from `device_manager` ✓ |
| `bench_volume` | bench_io.c | Called from cmd_handler ✓ |

**No unused functions found.**

---

## Unused Variables

Scanned all files for declared but never-referenced variables:

**None found.** All local and global variables are used.

---

## Dead Code

| File | Lines | Description | Status |
|------|-------|-------------|--------|
| gui.c.bak | entire file | Old Win32 GUI, replaced by gui.cpp | **Should be removed** (P3 cleanup) |

The `gui.c.bak` file is a remnant from the Sprint 5→6 GUI rewrite. It is not compiled or linked.

---

## TODOs / FIXMEs

Scanned all source files (excluding imgui/ and tests):

**0 TODOs, 0 FIXMEs found.** All tracked work items are in BUG_LIST.md, CHANGELOG.md, and RC1_REPORT.md.

---

## Code Quality Summary

| Metric | Result |
|--------|--------|
| Compiler warnings (own code) | 0 |
| Unused functions | 0 |
| Unused variables | 0 |
| Dead code (compiled) | 0 |
| Dead code (.bak files) | 1 (gui.c.bak) |
| Magic numbers (critical) | 0 |
| Duplicate code (critical) | 0 |
| TODOs/FIXMEs in source | 0 |
| Thread safety (CRITICAL_SECTION) | All shared data protected |
| Resource leaks | 0 (after Task 1 fix) |

## RecommendeAction

- [ ] Delete `src/gui.c.bak` (old GUI, no longer needed)
