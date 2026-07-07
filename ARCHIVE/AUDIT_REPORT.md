# RAIDTEST v3 — Full Architecture Audit Report

**Project**: RAIDTEST v3 (v1.0 RC4)  
**Binary**: `raidtest_winfsp.exe` (2.1 MB, static-linked MinGW-w64 GCC 16.1.0)  
**Scope**: All 60 source files in `src/`, 14 documentation files, 7 build/test scripts  
**Date**: 2026-07-06  
**Auditor Role**: Senior Storage Software Architect  

---

## Executive Summary

This report presents a complete architecture audit of the RAIDTEST v3 software RAID project. The audit covered all 60 source files, 14 documentation files, and 7 build/scripts files across six phases: Architecture Understanding, Module Analysis, Systematic Bug Search, Design Cross-Check, Technical Debt Report, and Future Architecture Suggestions.

### Key Findings

| Metric | Value |
|---|---|
| Source files examined | 60 (25 .c, 32 .h, 1 .cpp) |
| Documentation files examined | 14 |
| Build/test scripts examined | 7 |
| Bugs found | 48 (11 critical, 15 high, 15 medium, 7 low) |
| Technical debts identified | 29 (3 critical, 7 high, 9 medium, 10 low) |
| Design-implementation gaps | 6 (1 critical) |
| Future suggestions | 16 (6 must-fix, 6 recommended, 4 research) |
| Tests passing | 26/38 (12 superblock tests fail without test infrastructure) |

### Single Biggest Issue

**Global state (`g_state`) is completely unsynchronized across all four thread types (CLI, FUSE, GUI workers, cache flush).** The `gs_lock()`/`gs_unlock()` functions exist in `common.h` but are never called in `raid_service.c` (24 functions), `fuse_bridge.c` (14 callbacks), or `gui.cpp` worker threads. They are only used in the CLI dispatch path (`cmd_handler.c`), the daemon path (`daemon.c`), and the auto-setup path (`main.c`). All state machine transitions from FUSE callbacks and GUI workers are data races.

### Verdict

**Below production standard.** The implementation is functionally complete (51/51 features), has a well-structured modular layout, and works correctly under single-thread CLI usage. However, the complete absence of thread safety in the primary code paths, pervasive NULL-pointer discipline failures (25+ sites), stack buffer overflows in the FUSE bridge, and unsafe OVERLAPPED I/O patterns make it unsuitable for multi-threaded production deployment. The project requires approximately 10-15 developer-days of focused bug fixing to reach production readiness.

---

## Overall Architecture Review

### Layered Architecture (6 Layers)

```
Layer 0 (Entry Point)        main.c
Layer 1 (Frontend)           gui.cpp, cmd_handler.c, daemon.c, wizard.c
Layer 2 (Service API)        raid_service.c/h
Layer 3 (Managers)           volume_manager, device_manager, metadata_manager,
                             planner_engine, ui_model, event_bus
Layer 4 (Engines)            stripe_engine, mirror_engine, ram_cache,
                             journal, storage_common, superblock
Layer 5 (Infrastructure)     pool_io, disk_scanner, bench_io, config,
                             logger, profiler, cleanup, fuse_bridge
Layer 6 (External)           WinFsp FUSE, Win32 API, DirectX 11
```

### Architectural Strengths

1. **Clean layering** — Each layer depends only on layers below it (at the include level). The `raid_service` layer successfully decouples UI from engine internals.

2. **Complete feature inventory** — All 31 CLI commands, 10 GUI buttons, and 3 GUI modes are implemented and match the PRODUCT_DESIGN.md specification exactly.

3. **Backward-compatible metadata format** — Superblock v4 format with v1/v2/v3 readers ensures existing volumes survive format upgrades.

4. **Event bus for decoupling** — The `event_bus` publish/subscribe mechanism is well-designed with per-event subscriber arrays and proper critical section protection for its internal state.

5. **Comprehensive error code system** — The `RC` enum in `common.h` provides 20+ distinct error codes covering initialization, I/O, parameter, metadata, and system errors.

### Architectural Weaknesses

1. **Circular call dependency** — `ram_cache ↔ stripe_engine` is a bidirectional call cycle. `cache_flush_all` calls `stripe_volume_map_lba`, and `stripe_volume_read/write` calls `cache_read/cache_write`. This makes the modules untestable in isolation and prevents independent lifecycle management.

2. **Thread safety entirely absent** — The architecture was designed for single-threaded CLI and never adapted for the multi-threaded reality of FUSE callbacks, GUI workers, and cache flush threads.

3. **No lock ordering specification** — Two critical sections (`g_state_cs`, `cache->lock`) exist with no documented acquisition order. Any future code that nests them risks deadlock.

4. **FUSE bridge is a flat file table** — 64-entry linear array, O(n) lookup, no directory hierarchy, silent overwrite on overflow. This is the weakest component architecturally.

5. **No error propagation contract** — The I/O layer returns `bool`; callers partially check it but errors do not propagate to user-visible state.

---

## Overall Quality Score

### Scoring Rubric (1-10, 10 = Best)

| Category | Score | Rationale |
|---|---|---|
| **Functional completeness** | 10/10 | All 51/51 features implemented |
| **Code organization** | 8/10 | Well-structured modular layout, clear file boundaries |
| **Thread safety** | 1/10 | g_state completely unsynchronized in main paths |
| **Error handling** | 4/10 | NULL checks missing at 25+ sites, errors don't propagate |
| **Memory safety** | 5/10 | OVERLAPPED use-after-free, realloc leaks, stack overflows |
| **Test coverage** | 5/10 | 38 tests exist but 12 fail without test infrastructure; no mocking |
| **Documentation accuracy** | 6/10 | Architecture doc incomplete, test pass claim stale |
| **Design-to-implementation fidelity** | 7/10 | Feature-complete but thread safety design intent unimplemented |
| **Security posture** | 4/10 | Stack overflows, no input validation on FUSE paths |
| **Maintainability** | 6/10 | 47-line macros, dead code, unnecessary abstraction layers |

**Overall Quality Score: 5.6/10** — Below production standard. Functional and complete for CLI single-thread use but unsafe for multi-threaded deployment.

---

## Overall Risk Assessment

### Risk Matrix

| Risk | Likelihood | Impact | Items |
|---|---|---|---|
| Crash on malformed FUSE path | **High** (triggered by any path >255 chars) | **High** (process termination) | C2, C3 |
| Data race on g_state | **High** (4 thread types, concurrent access) | **High** (state corruption, undetected data loss) | C5 |
| OVERLAPPED stack corruption | **Medium** (error-path race) | **High** (kernel-mode write to freed memory) | C6 |
| Journal entry corruption | **Medium** (concurrent flush + FUSE write) | **High** (unrecoverable journal replay) | H5 |
| 64-entry FUSE overflow | **Medium** (many files created) | **High** (silent overwrite of old files) | D4 |
| FUSE table lock destroyed while in use | **Medium** (unmount races with callback) | **High** (deadlock/crash) | H6 |
| Disk scan partial failure | **Low** (realloc OOM) | **Medium** (silent disk omission) | H12 |
| Cache flush corrupt on concurrent flush | **Low** (backpressure + timer overlap) | **High** (persistent data corruption) | H2 |
| NULL dereference in thread proc | **Low** (requires NULL arg) | **High** (process termination) | C1 |
| Mirror pointer torn on write | **Medium** (concurrent I/O + rebuild) | **Medium** (read from wrong disk) | H12 |

### Risk Mitigation Priority

1. **Immediate (before any production use):** S1 (g_state locking), S2 (NULL guards), S3 (FUSE overflow fix), S5 (OVERLAPPED safety)
2. **Short-term (within first maintenance cycle):** S4 (FUSE lifecycle), S6 (journal lock), error propagation
3. **Medium-term:** Journal rotation, FUSE hash table, lock ordering documentation
4. **Long-term:** End-to-end checksums, hot spare, encryption

### Guardrails That Reduce Risk in Practice

The project works in practice despite these issues because of three implicit guardrails:

1. **CLI path (the most-used path) acquires `gs_lock()`** around command dispatch in `cmd_handler.c`
2. **Single-user assumption** — most users never trigger concurrent FUSE + GUI + flush scenarios
3. **Small scale** — 4-disk limit, 64-file FUSE limit, simple RAID0/1 only keeps complexity bounded

These guardrails are not documented and could be violated by any configuration change or extended usage pattern.
