# RAIDTEST v1.0 RC4 — Repository Progress (codename: RAIDV3)

## Current Status: Repository Organization Complete

Repository has been reorganized for long-term maintenance. Root directory cleaned
from 52 to 31 entries. Documentation categorized under `docs/` with clear taxonomy.
All build artifacts removed. Engineering workflow formalized in AGENT.md.

## What Was Done

- Scanned all 74 entries in repository root
- Classified every `.md` and `.txt` file by status (Active / Superseded / Historical / Generated / Temporary)
- Created `MASTER_BACKLOG.md` — single source of truth for engineering work
- Created `DOCUMENT_INDEX.md` — index of all documents with status and recommended action
- Created `AGENT.md` — agent rules referencing MASTER_BACKLOG.md as truth
- Moved 32 non-active documents from root to `ARCHIVE/`
- Updated `DOCUMENT_INDEX.md` to cover root-level files, `docs/archive/`, and `docs/archive/validation/`
- Archived `HANDOFF.md` (architecture details covered by other active docs)
- Restored `OPENCODE_TEST_LOG.md` from `ARCHIVE/` to root (active engineering tracker)
- Created `NEXT_SESSION.md` — pre-flight briefing for next engineering session
- **Release Verification (2026-07-07):** Full repository, build, test, GUI, CLI, and documentation verification completed. Build OK (39/39 tests), GUI/CLI fully functional. `RELEASE_VERIFICATION.md` created.
- **System Mapping (2026-07-07):** Read all 32 source files and 4 architecture documents. Created `SYSTEM_MAP.md` with 7-layer architecture diagram, 30+ module inventory, data flow, state machine, thread/lock model, asymmetric stripe algorithm, superblock v4 format.
- **Bug Verification (2026-07-07):** Independently audited all 32 source files for P0/P1 issues. Created `BUG_VERIFICATION.md` confirming 9 known bugs, finding 2 new P0 and 3 new P1 issues.
- **Learning Guide (2026-07-07):** Created `LEARNING_GUIDE.md` with 15 module deep-dives, 4 data flow walkthroughs, thread/lock model, WinFsp integration notes.
- **Repository Organization (2026-07-07):** Created `REPOSITORY_STRUCTURE.md`. Moved 12 docs from root to `docs/architecture/`, `docs/development/`, `docs/learning/`, `docs/release/`. Archived 2 redundant bash scripts. Deleted all build artifacts (8 EXEs, 35 .o files, 1 temp .dat). Updated `.gitignore` to track `winfsp-x64.dll`. Updated `AGENT.md` with new workflow conventions. Updated `DOCUMENT_INDEX.md` with new locations.
- **Bug Investigation (2026-07-07):** Investigated B10-B14 by tracing source code, analyzing runtime impact, and assessing thread/execution model. Produced `BUG_INVESTIGATION_RESULT.md` with evidence, severity, and reproduction guidance. Updated `MASTER_BACKLOG.md` with findings. B13 dispositioned as FALSE POSITIVE (latent only). B14 downgraded to P2 (cosmetic). No source changes.
- **Bug Fixes (2026-07-07):** Fixed B10 (pool_size_mb config), B11 (cleanup scope), B12 (atomic mirror stats). Added `pool_mb` field to `APP_CONFIG` with save/load. Replaced `cleanup_scan_all_drives()` with `cleanup_pool_session()`. Made `bytes_read` atomic in mirror_engine. Build OK, 39/39 tests pass. Produced `BUG_FIX_RESULT.md`.
- **Capstone Validation (2026-07-07):** Full professor-review validation completed. Produced `WORKFLOW_VALIDATION.md`, `CAPSTONE_DEMO_PLAN.md`, `PROFESSOR_QA.md`, `FEATURE_MATRIX.md`, `BUILD_STATUS.md`. Build OK (pre-existing warnings), tests 39/39 + stress all PASS. Verified all 5 workflows, created demo script, prepared professor Q&A, built 66-feature matrix. Found 1 doc-only feature (embedded CLI console). Updated `NEXT_SESSION.md`, `OPENCODE_TEST_LOG.md`.

## Active Documents (28 total across root + docs/)

### Root (must stay)
`README.md`, `LICENSE`, `CHANGELOG.md`, `CONTRIBUTING.md`, `QUICK_START.md`,
`SECURITY.md`, `THIRD_PARTY_NOTICES.md`, `DEMO.md`, `KNOWN_LIMITATIONS.md`,
`MASTER_BACKLOG.md`, `DOCUMENT_INDEX.md`, `AGENT.md`, `NEXT_SESSION.md`,
`OPENCODE_PROGRESS.md`, `OPENCODE_TEST_LOG.md`, `REPOSITORY_STRUCTURE.md`,
`build.bat`, `build_asan.bat`, `build_stress.bat`

### `docs/architecture/`
`API.md`, `LOCK_ORDER.md`, `METADATA.md`, `PRODUCT_DESIGN.md`, `SYSTEM_MAP.md`, `USER_FLOW.md`

### `docs/development/`
`BUG_VERIFICATION.md`, `FUTURE_ROADMAP.md`, `TEST_PLAN.md`, `WORKFLOW_VALIDATION.md`

### `docs/learning/`
`LEARNING_GUIDE.md`

### `docs/release/`
`RELEASE_CHECKLIST.md`, `RELEASE_VERIFICATION.md`, `CAPSTONE_DEMO_PLAN.md`, `PROFESSOR_QA.md`, `FEATURE_MATRIX.md`, `BUILD_STATUS.md`

### `docs/research/`
*(reserved — currently empty)*

## Archived Documents (34 in ARCHIVE/ + 22 in docs/archive/)

34 historical documents in `ARCHIVE/` (root) — old audit reports, bug reports,
session summaries, and implementation reports superseded by MASTER_BACKLOG.md.
22 additional historical/superseded documents in `docs/archive/`,
`docs/archive/validation/`, and `docs/archive/benchmark/`.

## Remaining Engineering Tasks

### Bugs (P0-P1)
- OVERLAPPED use-after-free in I/O paths (P0)
- FUSE stack buffer overflows (P0)
- FUSE file table lifecycle race (P1)
- Journal write synchronization (P1)
- Event bus critical section leak (P1)
- NULL dereference risk at device_get() and vol->disks[i] call sites (P1)
- [NEW B14] Export diagnostic uses deprecated GetVersion() (P2 — cosmetic)

### Resolved Bugs
- B10: pool_size_mb — added pool_mb to APP_CONFIG, default 51200, decoupled from cache_mb ✓
- B11: cleanup_scan_all_drives — replaced with cleanup_pool_session (current volume only) ✓
- B12: mirror_engine bytes_read — made atomic with InterlockedExchangeAdd64 ✓

### Technical Debt
- `g_state` locking missing in raid_service.c and FUSE callbacks (P0)
- Hardcoded `C:\RAIDTEST\` path in tests (P1)
- I/O functions return `bool` instead of RC error codes (P2)
- `APP_STATE` defined in wrong header (P2)
- No lock ordering documentation (P2)
- Unbounded journal growth (P2)
- Flat 64-entry FUSE file table (P2)

### Architecture Work (Resolved)
- A2: Cache flush race — re-dirty check after I/O in ram_cache.c ✓
- A3: Restore all disks — verified do_restore() iterates all disks ✓
- A4: Dead code removal — bench_elapsed, PerfSample unused fields removed ✓

### Remaining Architecture Work
- Lock acquisition wrappers for raid_service (S1)
- NULL guards for device_get() call sites (S2)
- FUSE overflow fixes (S3)
- FUSE file table refcounting (S4)
- OVERLAPPED heap allocation (S5)
- Journal synchronization (S6)

### Documentation Work (Completed)
- D1: Created `LOCK_ORDER.md` documenting lock acquisition order ✓
- D2: Updated `README.md` demo instructions to match current GUI layout and RC4 feature set ✓
- D3: Aligned `PRODUCT_DESIGN.md` feature claims with actual implemented behavior ✓
- D4: Release Cleanup — removed `Craidtest_8.dat` artifact from repo root ✓
- D5: Release Cleanup — fixed `README.md` project tree stale `RC1_REPORT.md` reference ✓
- D6: Release Cleanup — added `select`, `status`, `test`, `exit` to `README.md` CLI table ✓

## Active Engineering Control Files

- `AGENT.md` — Agent operating rules (updated with new docs structure)
- `MASTER_BACKLOG.md` — Single source of truth for all backlog items
- `DOCUMENT_INDEX.md` — Master index of all documentation (updated)
- `REPOSITORY_STRUCTURE.md` — Repository layout and folder conventions (new)
- `NEXT_SESSION.md` — Pre-flight briefing for next session
- `OPENCODE_PROGRESS.md` — This file, tracks repository state
- `OPENCODE_TEST_LOG.md` — Build/test results tracking

## Demo Hardening Complete (2026-07-07)

Changes made:
- **GUI demo flow**: Added Simulation Controls (`W_SIMULATE_FAIL`, `W_SIMULATE_HEALTHY`, `W_SIMULATE_DISCONNECT`) in Developer mode. Reuses existing `raid_simulate()` backend.
- **State visibility**: `refresh_ui_model()` now uses `g_state.rt.state` directly. DEGRADED/RECOVERING states shown with color coding in Volume Info.
- **Rebuild progress**: Removed fake progress loop in `W_REBUILD`. Progress is now honest (indeterminate → complete).
- **Presentation asset**: Created `docs/release/ARCHITECTURE_PRESENTATION.md`.

## Release Status

**v1.0 RC4 — CLEANED for capstone submission.**

All release verification checks passed:
- Build OK (no regression, pre-existing warnings only)
- Tests: 39/39 passed
- GUI: All mode tabs (Beginner/Advanced/Developer) functional. Developer mode now includes simulation controls and proper state display.
- CLI: --help lists 28 commands, all exist in dispatch table, --version correct
- Documentation: README, DEMO, PRODUCT_DESIGN match current source
- Known limitations documented in `KNOWN_LIMITATIONS.md`
- Repository structure documented in `REPOSITORY_STRUCTURE.md`

Refer to `docs/release/RELEASE_VERIFICATION.md` for detailed findings.

---

## Final Release Cleanup (2026-07-07)

Changes made:
- **Phase 1 — Documentation Sync**: Updated `DOCUMENT_INDEX.md` (docs/release 2→7 files, docs/development 3→6 files, added dual-archive note). Updated `REPOSITORY_STRUCTURE.md` (fixed NEXT_SESSION.md path, docs/release 2→7 files, docs/development 4→6 files, Build Artifacts status corrected).
- **Phase 2 — Version Normalization**: Unified version naming to "RAIDTEST v1.0 RC4 (codename: RAIDV3)" across MASTER_BACKLOG.md, KNOWN_LIMITATIONS.md, SECURITY.md, OPENCODE_PROGRESS.md, NEXT_SESSION.md, OPENCODE_TEST_LOG.md.
- **Phase 3 — Artifact Cleanup**: Removed `Craidtest_8.dat` from root.
- **Phase 4 — Archive Review**: Analyzed dual archive locations (`ARCHIVE/` + `docs/archive/`). Noted in DOCUMENT_INDEX.md. Consolidation deferred (risk of broken references).
- **Phase 5 — Build & Test**: `build.bat` OK (pre-existing warnings), `raidtest_tests.exe` 39/39 PASS. Updated logs.
- Created `docs/release/FINAL_CLEANUP_REPORT.md`.
