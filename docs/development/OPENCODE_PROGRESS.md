# RAIDTEST v1.0 RC4 — Repository Progress (codename: RAIDV3)

## Current Status: Final Demo Rehearsal Complete

All 6 phases verified. Build OK, 39/39 tests PASS, all stress tests PASS. Created `FINAL_DEMO_REHEARSAL.md`. Verdict: DEMO-READY.

## What Was Done

- **Phase 1 — Environment Verification:** Confirmed GUI (`raidtest_winfsp.exe`) launches, CLI fallback (`--cli`, `--help`, `--version`) works, WinFsp installed, all runtime DLLs present. 1 WARNING: must run as Administrator.
- **Phase 2 — Demo Path Trace:** Verified all 9 demo steps against source code (Startup, Scan, Create RAID0, Mount, File Write, Failure Simulation, Degraded State, Rebuild, Data Verification). Every step traceable to source.
- **Phase 3 — Risk Assessment:** Built risk table covering B1, B2, B3, B9, WinFsp missing, Admin missing, GUI failure, and 8 other risks. All have documented mitigations.
- **Phase 4 — Timing Plan:** Created 5-min (core only), 10-min (full), and 15-min (with architecture + Q&A) versions.
- **Phase 5 — Professor Q&A Simulation:** 17 questions prepared in PROFESSOR_QA.md with source evidence locations verified.
- **Phase 6 — Final Build & Test:** `build.bat` OK (pre-existing warnings), `raidtest_tests.exe` 39/39 PASS, stress tests all PASS.
- **Deliverable:** Created `docs/release/FINAL_DEMO_REHEARSAL.md` with environment readiness, workflow verification, risk assessment, timing plan, emergency fallback plan, and final recommendation.

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
`ARCHITECTURE_PRESENTATION.md`, `BUILD_STATUS.md`, `CAPSTONE_DEMO_PLAN.md`, `DEMO_RUN_CHECKLIST.md`, `FEATURE_MATRIX.md`, `FINAL_CLEANUP_REPORT.md`, `PRESENTATION_SCRIPT.md`, `PROFESSOR_QA.md`, `RELEASE_CHECKLIST.md`, `RELEASE_VERIFICATION.md`

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

## Capstone Presentation Preparation (2026-07-07)

Changes made:
- **Phase 1 — Presentation Package**: Created `docs/release/PRESENTATION_SCRIPT.md` with 10-session script covering project intro, problem background, Windows RAID rationale, architecture, asymmetric stripe algorithm, cache/journal/recovery, WinFsp FUSE, demo flow, test results, and honest limitations.
- **Phase 2 — Professor Q&A Enhancement**: Added 9 new Q&A items to `docs/release/PROFESSOR_QA.md` (Q9-Q17): RAID5 rationale, Storage Spaces comparison, innovation highlights, data consistency, crash recovery, multi-disk failure, thread safety, WinFsp choice, C/C++ choice. Expanded from 8 to 17 prepared questions.
- **Phase 3 — Demo Dry Run Checklist**: Created `docs/release/DEMO_RUN_CHECKLIST.md` with pre-demo environment/build/test preparations and step-by-step demo flow with CLI fallback commands.
- **Phase 4 — Final Repository Check**: Verified `build.bat` PASS (pre-existing warnings only), `raidtest_tests.exe` 39/39 PASS, updated `DOCUMENT_INDEX.md` to include all 10 docs/release/ files.
- Updated: `DOCUMENT_INDEX.md`, `OPENCODE_PROGRESS.md`, `OPENCODE_TEST_LOG.md`, `NEXT_SESSION.md`.

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

## Final Demo Rehearsal (2026-07-07)

Changes made:
- **Phase 1 — Environment Verification**: Confirmed GUI entry (`main.c:131` → `gui_run()`), D3D11 fallback, CLI `--cli`/`--help`/`--version`. WinFsp FOUND at `C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll`. All stress binaries present and passing.
- **Phase 2 — Demo Path Trace**: Verified all 9 demo steps (Startup, Scan, Create, Mount, Write, Failure, Degraded, Rebuild, Verify) against source code. Every step traceable and functional.
- **Phase 3 — Risk Assessment**: Built risk table. B1/B2/B3 (P0) — LOW demo probability. B9 (P1) — MEDIUM probability. WinFsp missing — LOW (installed). Admin missing — MEDIUM (must elevate). No disks — MEDIUM (pool file fallback).
- **Phase 4 — Timing Plan**: 5-min (core value), 10-min (full demo), 15-min (architecture + Q&A) versions created.
- **Phase 5 — Professor Q&A**: 17 questions prepared in PROFESSOR_QA.md with verified source evidence locations.
- **Phase 6 — Build & Test**: `build.bat` OK (pre-existing warnings), `raidtest_tests.exe` 39/39 PASS, `test_concurrent.exe` PASS, `test_metadata_corrupt.exe` PASS, `test_random_io.exe` PASS.
- Created `docs/release/FINAL_DEMO_REHEARSAL.md` with comprehensive environment readiness, workflow verification, risk assessment, timing plan, emergency fallback plan, and final recommendation.
- Updated: `OPENCODE_PROGRESS.md`, `OPENCODE_TEST_LOG.md`, `NEXT_SESSION.md`.

## Capstone Demo Rehearsal Audit (2026-07-07)

Final independent audit before professor presentation. All 5 phases executed:
- **Phase 1 — Demo Environment Audit**: Verified build.bat, WinFsp (installed), D3D11 (available), Administrator (NOT elevated — WARNING). All 7 executables present. Created `docs/release/DEMO_ENVIRONMENT_REPORT.md`.
- **Phase 2 — Demo Flow Simulation**: Traced all 9 demo steps (Startup→Scan→Create→Mount→Write→Failure→Degraded→Rebuild→Verify) against source. Every step has GUI entry, CLI alternative, backend function, possible failure, and workaround. Created `docs/release/DEMO_FLOW_AUDIT.md`.
- **Phase 3 — Presentation Risk Review**: Found **CRITICAL script contradiction** — PRESENTATION_SCRIPT.md describes failure+rebuild (RAID1-only) but demo plan creates RAID0. Limitations are honestly documented. P0/P1 bugs acceptable for demo. Created `docs/release/PRESENTATION_RISK_REPORT.md`.
- **Phase 4 — Professor Attack Questions**: Added 9 new Q&A items (Q18-Q27) covering performance, correctness, config corruption, file fragmentation, stripe unit size, rebuild crash recovery, journal proof, memory usage, ImGui rationale, multi-volume limits. PROFESSOR_QA.md now has 27 prepared questions.
- **Phase 5 — Final Verification**: `build.bat` OK (2 pre-existing + ImGui warnings), `raidtest_tests.exe` 39/39 PASS, stress tests all PASS (concurrent, random_io, metadata_corrupt, powerfail). Updated control files.
- **Deliverable**: Created `docs/release/CAPSTONE_FINAL_STATUS.md` with verdict.

## Final Release Cleanup (2026-07-07)

Changes made:
- **Phase 1 — Documentation Sync**: Updated `DOCUMENT_INDEX.md` (docs/release 2→7 files, docs/development 3→6 files, added dual-archive note). Updated `REPOSITORY_STRUCTURE.md` (fixed NEXT_SESSION.md path, docs/release 2→7 files, docs/development 4→6 files, Build Artifacts status corrected).
- **Phase 2 — Version Normalization**: Unified version naming to "RAIDTEST v1.0 RC4 (codename: RAIDV3)" across MASTER_BACKLOG.md, KNOWN_LIMITATIONS.md, SECURITY.md, OPENCODE_PROGRESS.md, NEXT_SESSION.md, OPENCODE_TEST_LOG.md.
- **Phase 3 — Artifact Cleanup**: Removed `Craidtest_8.dat` from root.
- **Phase 4 — Archive Review**: Analyzed dual archive locations (`ARCHIVE/` + `docs/archive/`). Noted in DOCUMENT_INDEX.md. Consolidation deferred (risk of broken references).
- **Phase 5 — Build & Test**: `build.bat` OK (pre-existing warnings), `raidtest_tests.exe` 39/39 PASS. Updated logs.
- Created `docs/release/FINAL_CLEANUP_REPORT.md`.

## Demo Scenario Correction (2026-07-08)

Changes made:
- **Phase 1 — Document Review**: Read all 10 demo/release/architecture/development documents. Identified RAID0/RAID1 contradiction across 7 files.
- **Phase 2 — Correction Document**: Created `docs/release/DEMO_SCENARIO_CORRECTION.md` with reasoning: RAID0 has no fault tolerance; RAID1 enables failure detection, degraded mode, and rebuild.
- **Phase 3 — Demo Document Updates**: Updated `CAPSTONE_DEMO_PLAN.md` (Segment 3: RAID0→RAID1 Mirror), `PRESENTATION_SCRIPT.md` (Section 8 Steps 3/5/6/7: RAID0→RAID1 with explanation), `DEMO_RUN_CHECKLIST.md` (Step 3: RAID1 mirror checklist), `FINAL_DEMO_REHEARSAL.md` (timing plans + steps), `PROFESSOR_QA.md` (added Q28: RAID1 vs RAID0 choice), `CAPSTONE_FINAL_STATUS.md` (contradiction marked resolved), `WORKFLOW_VALIDATION.md` (demo path step 3), `ARCHITECTURE_PRESENTATION.md` (Segment 3/5), `DEMO_FLOW_AUDIT.md` (Step 3/5/9).
- **Phase 4 — Professor Q&A**: Added Q28 explaining RAID1 selection: RAID0 demonstrates performance but cannot demonstrate fault recovery. RAID1 enables degraded mode and rebuild demonstration.
- **Phase 5 — Consistency Audit**: Searched all release docs for "RAID0", "RAID1", "Rebuild", "Degraded". Confirmed no document claims RAID0 supports rebuild. All demo docs consistently describe RAID1 scenario.
- **Phase 6 — Build & Test**: `build.bat` OK (pre-existing warnings), `raidtest_tests.exe` 39/39 PASS. No source code changes.
- **Deliverable**: Created `docs/release/DEMO_SCENARIO_CORRECTION.md`, `docs/release/DEMO_SCENARIO_CORRECTION_REPORT.md`.
- Updated: `OPENCODE_PROGRESS.md`, `OPENCODE_TEST_LOG.md`, `NEXT_SESSION.md`.

## Final Demo Operator Training (2026-07-08)

Changes made:
- **Phase 1 — Document Audit**: Analyzed all ~80 documents. Classified into A (Read Before Demo), B (During Presentation), C (Professor Deep Dive), D (Developer Reference), E (Archived). Created `docs/release/DEMO_DOCUMENT_MAP.md`.
- **Phase 2 — Operator Manual**: Created `docs/release/FINAL_DEMO_OPERATOR_GUIDE.md` with 6 sections: Demo Day Prep, Startup Procedure, 9-step RAID1 Flow, Emergency Troubleshooting, Professor Talking Points, Things NOT To Do.
- **Phase 3 — Package Structure**: Created `docs/release/DEMO_PACKAGE_STRUCTURE.md` documenting recommended final repo layout. Moved 9 prep/rehearsal docs from `docs/release/` to `docs/archive/`: FINAL_DEMO_REHEARSAL, DEMO_ENVIRONMENT_REPORT, DEMO_FLOW_AUDIT, PRESENTATION_RISK_REPORT, CAPSTONE_FINAL_STATUS, DEMO_SCENARIO_CORRECTION, DEMO_SCENARIO_CORRECTION_REPORT, FINAL_CLEANUP_REPORT, RELEASE_VERIFICATION.
- **Phase 4 — Index Updates**: Updated `DOCUMENT_INDEX.md` and `REPOSITORY_STRUCTURE.md` to reflect new docs and moves.
- **Phase 5 — Verification**: `build.bat` OK (pre-existing warnings). `raidtest_tests.exe` 39/39 PASS. No source code changes.
- **Deliverable**: Created `docs/release/FINAL_DEMO_OPERATOR_TRAINING_REPORT.md` with final verdict.
- Updated: `OPENCODE_PROGRESS.md`, `OPENCODE_TEST_LOG.md`, `NEXT_SESSION.md`.
