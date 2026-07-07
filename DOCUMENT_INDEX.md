# RAIDTEST v1.0 RC4 — Document Index (codename: RAIDV3)

**Purpose**: Master index of all documentation — find any file by name, location, and status.
**Last updated**: 2026-07-08

## Status Legend

| Status   | Meaning |
|----------|---------|
| ACTIVE   | Current, maintained, reflects live source code |
| REFERENCE | Historical reference — superseded but kept for context |
| ARCHIVED | Not actively maintained — moved to `ARCHIVE/` or `docs/archive/` |

---

## Repository Root — Must Stay

| File | Purpose | Status |
|------|---------|--------|
| `README.md` | Project overview, build, usage, governance | ACTIVE |
| `LICENSE` | MIT license | ACTIVE |
| `CHANGELOG.md` | Version history and notable changes | ACTIVE |
| `CONTRIBUTING.md` | Contributor guidelines | ACTIVE |
| `QUICK_START.md` | Minimal quick-start guide (Chinese) | ACTIVE |
| `SECURITY.md` | Threat model and security analysis | ACTIVE |
| `THIRD_PARTY_NOTICES.md` | License notices (ImGui, WinFsp, DirectX, MinGW) | ACTIVE |
| `DEMO.md` | Step-by-step GUI demo walkthrough | ACTIVE |
| `KNOWN_LIMITATIONS.md` | Platform, feature, and design constraints | ACTIVE |
| `DOCUMENT_INDEX.md` | This file — master document index | ACTIVE |
| `AGENT.md` | AI agent operating rules | ACTIVE |
| `MASTER_BACKLOG.md` | Single source of truth for backlog items | ACTIVE |
| `NEXT_SESSION.md` | Current phase, objective, build/test status | ACTIVE |
| `OPENCODE_PROGRESS.md` | Repository progress tracking | ACTIVE |
| `OPENCODE_TEST_LOG.md` | Build/test results history | ACTIVE |
| `REPOSITORY_STRUCTURE.md` | Repository layout and folder conventions | ACTIVE |
| `build.bat` | Main build script | ACTIVE |
| `build_asan.bat` | AddressSanitizer build script | ACTIVE |
| `build_stress.bat` | Stress-test build script | ACTIVE |
| `winfsp-x64.dll` | WinFsp runtime DLL (version 2.1) | ACTIVE |
| `libwinfsp-x64.a` | WinFsp import library (version 2.1) | ACTIVE |

---

## `docs/architecture/` — System Design & Architecture

| File | Purpose | Status |
|------|---------|--------|
| `API.md` | Public API reference (event bus, raid_service, devices, volumes) | ACTIVE |
| `LOCK_ORDER.md` | Mandatory lock acquisition order | ACTIVE |
| `METADATA.md` | Distributed metadata system format reference | ACTIVE |
| `PRODUCT_DESIGN.md` | Product design specification (CLI, GUI, state machine) | ACTIVE |
| `SYSTEM_MAP.md` | 7-layer architecture map with module inventory and data flow | ACTIVE |
| `USER_FLOW.md` | User personas and end-to-end GUI flows | ACTIVE |

---

## `docs/development/` — Engineering Workflow

| File | Purpose | Status |
|------|---------|--------|
| `BUG_FIX_RESULT.md` | B10-B12 fix implementation report | ACTIVE |
| `BUG_INVESTIGATION_RESULT.md` | B10-B14 source trace and impact analysis | ACTIVE |
| `BUG_VERIFICATION.md` | Independent bug audit — confirms known bugs, reports new findings | ACTIVE |
| `FUTURE_ROADMAP.md` | Architecture roadmap, must-fix items, long-term research | ACTIVE |
| `TEST_PLAN.md` | Integration test plan (13 end-to-end test cases) | ACTIVE |
| `WORKFLOW_VALIDATION.md` | Per-button source trace of all 5 workflows | ACTIVE |

---

## `docs/learning/` — Tutorials & Learning

| File | Purpose | Status |
|------|---------|--------|
| `LEARNING_GUIDE.md` | System internals tutorial — 15 module deep-dives, 4 data flows | ACTIVE |

---

## `docs/release/` — Presentation Package (8 files)

| File | Purpose | Status |
|------|---------|--------|
| `PRESENTATION_SCRIPT.md` | Capstone presentation script (10 sections) | ACTIVE |
| `PROFESSOR_QA.md` | Prepared Q&A with source evidence (27 Q&A) | ACTIVE |
| `BUILD_STATUS.md` | Capstone final build and test validation | ACTIVE |
| `ARCHITECTURE_PRESENTATION.md` | Architecture diagram and innovation explanation | ACTIVE |
| `FEATURE_MATRIX.md` | 66-feature classification with GUI/CLI entries | ACTIVE |
| `CAPSTONE_DEMO_PLAN.md` | 10-segment demo plan for professor review | ACTIVE |
| `FINAL_DEMO_OPERATOR_GUIDE.md` | Complete operator manual (6 sections) | ACTIVE |
| `DEMO_RUN_CHECKLIST.md` | Pre-demo prep and live demo checklist | ACTIVE |

---

## `docs/research/` — Research Notes

*(reserved for future use — currently empty)*

---

## `docs/archive/` — Historical / Superseded

### Sprint Reports & Architecture (original)

| File | Purpose | Status |
|------|---------|--------|
| `ARCHITECTURE_REPORT_SPRINT5.md` | Sprint 5 architecture refactoring | ARCHIVED |
| `ARCHITECTURE.md` | Sprint 5 Manager Architecture overview | ARCHIVED |
| `FINAL_ARCHITECTURE_AUDIT.md` | Full-source audit scoring 62/100 | ARCHIVED |
| `GUI_REPORT.md` | GUI P0 bugs review | ARCHIVED |
| `PERFORMANCE_TEST.md` | Baseline performance measurements | ARCHIVED |
| `RC_REPORT.md` | RC1 assessment (engine/CLI 100%, journal 60%, GUI 0%) | ARCHIVED |
| `RC1_REPORT.md` | RC1 assessment for RAIL v1.0 variant | ARCHIVED |
| `ROADMAP_V2.md` | Sprint 5+ roadmap | ARCHIVED |
| `ROADMAP.md` | Original roadmap sprints 1-5 | ARCHIVED |
| `summary.md` | Sprint 5 completion summary | ARCHIVED |
| `VALIDATION.md` | Feature validation matrix | ARCHIVED |
| `WORKFLOW.md` | CLI command sequence guide | ARCHIVED |

### Demo Preparation & Rehearsal Docs (moved from `docs/release/`)

| File | Purpose | Status |
|------|---------|--------|
| `DEMO_DOCUMENT_MAP.md` | Document classification: A/B/C/D/E for demo day | ARCHIVED |
| `DEMO_PACKAGE_STRUCTURE.md` | Recommended final repository structure | ARCHIVED |
| `FINAL_DEMO_OPERATOR_TRAINING_REPORT.md` | Operator training session report | ARCHIVED |
| `RELEASE_CHECKLIST.md` | Pre-release verification steps | ARCHIVED |
| `FINAL_DEMO_REHEARSAL.md` | Final demo rehearsal report | ARCHIVED |
| `DEMO_ENVIRONMENT_REPORT.md` | Environment audit for demo | ARCHIVED |
| `DEMO_FLOW_AUDIT.md` | 9-step demo flow trace | ARCHIVED |
| `PRESENTATION_RISK_REPORT.md` | Risk review (found RAID0/1 contradiction) | ARCHIVED |
| `CAPSTONE_FINAL_STATUS.md` | Final status after rehearsal audit | ARCHIVED |
| `DEMO_SCENARIO_CORRECTION.md` | RAID0→RAID1 scenario correction | ARCHIVED |
| `DEMO_SCENARIO_CORRECTION_REPORT.md` | Correction session report | ARCHIVED |
| `FINAL_CLEANUP_REPORT.md` | Final release cleanup report | ARCHIVED |
| `RELEASE_VERIFICATION.md` | RC4 release verification report | ARCHIVED |

### `docs/archive/validation/`

| File | Purpose | Status |
|------|---------|--------|
| `WINAPI_AUDIT.md` | WinAPI call site fixes | ARCHIVED |
| `VALIDATION_MATRIX.md` | Test coverage matrix | ARCHIVED |
| `REGRESSION_REPORT.md` | Regression test report | ARCHIVED |
| `REAL_TEST.md` | Manual test procedure (Chinese) | ARCHIVED |
| `QA_REPORT.md` | Sprint 8 QA report | ARCHIVED |
| `PERFORMANCE_REPORT_TEMPLATE.md` | Performance template (Chinese) | ARCHIVED |
| `PERFORMANCE.md` | RAID0 performance measurements | ARCHIVED |
| `HARDWARE_VALIDATION.md` | Hardware validation (Chinese) | ARCHIVED |
| `DEMO_DATASET.md` | Demo dataset plan (Chinese) | ARCHIVED |
| `BUG_STATUS.md` | Sprint 9 bug status | ARCHIVED |

### `docs/archive/benchmark/`

| File | Purpose | Status |
|------|---------|--------|
| `cli_bench.c` | CLI benchmark source (unused, stub) | ARCHIVED |

---

## `ARCHIVE/` (Root) — Old Reports Moved During Cleanup

34 historical documents — old audit reports, bug reports, review documents,
session summaries, and implementation reports that are superseded by
`MASTER_BACKLOG.md`, `BUG_VERIFICATION.md`, or are no longer relevant.

**Note on archive locations**: Three archive sources exist:
- `ARCHIVE/` (root) — 34 historical documents moved from root during cleanup
- `docs/archive/` — Sprint reports, old roadmaps, and validation docs
- `docs/archive/` (demo prep subsection) — Rehearsal/prep docs moved from `docs/release/` during operator training

All contain superseded material. Consolidation is deferred to avoid breaking existing references.

Key files:
- `AUDIT_REPORT.md` — 60-file audit (superseded by BUG_VERIFICATION.md)
- `BUG_AUDIT.md` / `BUG_REPORT.md` / `BUG_PRIORITY.md` — old bug tracking
- `PROFESSOR_REVIEW.md` / `UNIVERSITY_REVIEW.md` — academic assessments
- `HANDOFF.md` — architecture overview (superseded by SYSTEM_MAP.md)
- `REPOSITORY_CLEANUP_REPORT.md` — previous cleanup report
- `_build.sh` / `_check.sh` — redundant bash scripts (recently moved)
- Other implementation, validation, and audit reports

---

## `src/` — Source Code (66 files)

Not indexed individually here. See `REPOSITORY_STRUCTURE.md` for the source
file inventory and `SYSTEM_MAP.md` for module descriptions.

---

## File Move History (This Session)

| File | Old Location | New Location | Reason |
|------|-------------|-------------|--------|
| `SYSTEM_MAP.md` | root | `docs/architecture/` | Architecture doc |
| `PRODUCT_DESIGN.md` | root | `docs/architecture/` | Architecture doc |
| `LOCK_ORDER.md` | root | `docs/architecture/` | Architecture doc |
| `METADATA.md` | root | `docs/architecture/` | Architecture doc |
| `USER_FLOW.md` | root | `docs/architecture/` | Architecture doc |
| `API.md` | root | `docs/architecture/` | Architecture doc |
| `TEST_PLAN.md` | root | `docs/development/` | Development doc |
| `FUTURE_ROADMAP.md` | root | `docs/development/` | Development doc |
| `BUG_VERIFICATION.md` | root | `docs/development/` | Development doc |
| `LEARNING_GUIDE.md` | root | `docs/learning/` | Learning doc |
| `RELEASE_CHECKLIST.md` | root | `docs/release/` → `docs/archive/` | Release doc → archived |
| `RELEASE_VERIFICATION.md` | root | `docs/release/` → `docs/archive/` | Release doc → archived |
| `DEMO_DOCUMENT_MAP.md` | `docs/release/` | `docs/archive/` | Rehearsal doc → archived |
| `DEMO_PACKAGE_STRUCTURE.md` | `docs/release/` | `docs/archive/` | Rehearsal doc → archived |
| `FINAL_DEMO_OPERATOR_TRAINING_REPORT.md` | `docs/release/` | `docs/archive/` | Rehearsal doc → archived |
| `_build.sh` | root | `ARCHIVE/` | Redundant script |
| `_check.sh` | root | `ARCHIVE/` | Redundant script |
| `Craidtest_8.dat` | root | `Craidtest_8.dat` | Generated artifact (gitignored) |
| `raidtest_winfsp.exe` | root | *(gitignored)* | Build output (recreatable) |
| `raidtest_tests.exe` | root | *(gitignored)* | Build output (recreatable) |
| `test_*.exe` (5 files) | root | *(gitignored)* | Build output (recreatable) |
| `build/*.o` (35 files) | build/ | *(gitignored)* | Build output (recreatable) |
