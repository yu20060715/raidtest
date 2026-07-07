# RAIDV3 — Demo Document Map

**Purpose:** Classify every document in the repository by its role during capstone demonstration.
**Audience:** Student operator preparing for and executing the demo.
**Last updated:** 2026-07-08

---

## A. Read Before Demo

These documents **must** be read before demo day. They cover preparation, step-by-step operation, and the presentation script.

| File | Why |
|------|-----|
| `docs/release/FINAL_DEMO_OPERATOR_GUIDE.md` | Complete operator manual — startup, RAID1 flow, troubleshooting |
| `docs/release/DEMO_RUN_CHECKLIST.md` | Pre-demo prep checklist + 9-step live checklist |
| `docs/release/CAPSTONE_DEMO_PLAN.md` | 10-segment demo plan with timing, expected screens, fallbacks |
| `docs/release/PRESENTATION_SCRIPT.md` | Full spoken script (10 sections, 10-12 min) |
| `DEMO.md` | Quick 10-step walkthrough reference |
| `QUICK_START.md` | Minimal setup guide (Chinese) |
| `README.md` | Project overview, build, usage, architecture |
| `KNOWN_LIMITATIONS.md` | Honest limitations (professor will ask) |

---

## B. Read During Presentation

Keep these open or printed for quick reference during the live demo.

| File | Purpose |
|------|---------|
| `docs/release/PRESENTATION_SCRIPT.md` | Follow spoken script section-by-section |
| `docs/release/CAPSTONE_DEMO_PLAN.md` | Verify each segment timing and expected output |
| `docs/release/DEMO_RUN_CHECKLIST.md` | Check off steps as completed |

---

## C. Professor Deep Dive

Read these **only** if the professor asks a specific question during Q&A. Prepared answers exist in PROFESSOR_QA.md.

| File | When to Open |
|------|-------------|
| `docs/release/PROFESSOR_QA.md` (27 Q&A) | Any professor question — contains source-evidenced answers |
| `docs/release/BUILD_STATUS.md` | "How do we know the build is correct?" |
| `docs/release/ARCHITECTURE_PRESENTATION.md` | "Walk me through the architecture again" |
| `docs/release/FEATURE_MATRIX.md` | "What features are implemented?" |
| `docs/architecture/SYSTEM_MAP.md` | "Explain the 7-layer design" |
| `docs/architecture/PRODUCT_DESIGN.md` | "What were the design decisions?" |
| `docs/architecture/LOCK_ORDER.md` | "How is thread safety handled?" |
| `docs/architecture/METADATA.md` | "How is superblock v4 structured?" |
| `docs/architecture/API.md` | "Show me the public API surface" |
| `docs/architecture/USER_FLOW.md` | "What user personas did you design for?" |
| `docs/learning/LEARNING_GUIDE.md` | "Explain the internals in detail" |
| `docs/development/WORKFLOW_VALIDATION.md` | "How did you verify every button works?" |

---

## D. Developer Reference

Engineering documents — **not** part of the demo. Useful if the professor asks about development process or if post-demo work begins.

| File | Content |
|------|---------|
| `docs/development/BUG_VERIFICATION.md` | Independent bug audit (9 known bugs) |
| `docs/development/BUG_FIX_RESULT.md` | B10-B12 fix implementation |
| `docs/development/BUG_INVESTIGATION_RESULT.md` | B10-B14 source trace |
| `docs/development/TEST_PLAN.md` | Integration test plan (13 cases) |
| `docs/development/FUTURE_ROADMAP.md` | Post-demo engineering roadmap |
| `AGENT.md` | AI agent operating rules |
| `MASTER_BACKLOG.md` | Single source of truth for bugs/debt |
| `NEXT_SESSION.md` | Pre-flight briefing for next session |
| `OPENCODE_PROGRESS.md` | Session progress tracker |
| `OPENCODE_TEST_LOG.md` | Build/test results history |
| `DOCUMENT_INDEX.md` | Master document index |
| `REPOSITORY_STRUCTURE.md` | Repository layout conventions |
| `CHANGELOG.md` | Version history |
| `CONTRIBUTING.md` | Contributor guidelines |
| `SECURITY.md` | Security / threat model |
| `THIRD_PARTY_NOTICES.md` | Third-party license notices |
| `docs/release/RELEASE_CHECKLIST.md` | Pre-release verification steps |
| `build_asan.bat` | AddressSanitizer build script |
| `build_stress.bat` | Stress-test build script |

---

## E. Archived

These documents are historical, superseded, or internal preparation records. **Never open during the demo.** Doing so will confuse the presentation and waste time.

| Location | Documents |
|----------|-----------|
| `docs/archive/` | `ARCHITECTURE_REPORT_SPRINT5.md`, `ARCHITECTURE.md`, `FINAL_ARCHITECTURE_AUDIT.md`, `GUI_REPORT.md`, `PERFORMANCE_TEST.md`, `RC_REPORT.md`, `RC1_REPORT.md`, `ROADMAP_V2.md`, `ROADMAP.md`, `summary.md`, `VALIDATION.md`, `WORKFLOW.md`, `benchmark/cli_bench.c`, `validation/BUG_STATUS.md`, `validation/DEMO_DATASET.md`, `validation/HARDWARE_VALIDATION.md`, `validation/PERFORMANCE_REPORT_TEMPLATE.md`, `validation/PERFORMANCE.md`, `validation/QA_REPORT.md`, `validation/REAL_TEST.md`, `validation/REGRESSION_REPORT.md`, `validation/VALIDATION_MATRIX.md`, `validation/WINAPI_AUDIT.md` |
| `ARCHIVE/` (root) | 34 historical documents — old audit reports, bug reports, session summaries, implementation reports — all superseded by `MASTER_BACKLOG.md`, `BUG_VERIFICATION.md`, `SYSTEM_MAP.md` |
| Previously in `docs/release/` (now moved to `docs/archive/`) | `FINAL_DEMO_REHEARSAL.md`, `DEMO_ENVIRONMENT_REPORT.md`, `DEMO_FLOW_AUDIT.md`, `PRESENTATION_RISK_REPORT.md`, `CAPSTONE_FINAL_STATUS.md`, `DEMO_SCENARIO_CORRECTION.md`, `DEMO_SCENARIO_CORRECTION_REPORT.md`, `FINAL_CLEANUP_REPORT.md`, `RELEASE_VERIFICATION.md` |

---

## Quick Reference Card

```
┌─────────────────────────────────────────────────────────┐
│                    DEMO DAY PRIORITY                     │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  BEFORE DEMO (Read these first):                        │
│  ├─ FINAL_DEMO_OPERATOR_GUIDE.md          ★ NEW         │
│  ├─ DEMO_RUN_CHECKLIST.md                               │
│  ├─ CAPSTONE_DEMO_PLAN.md                               │
│  ├─ PRESENTATION_SCRIPT.md                              │
│  ├─ DEMO.md                                             │
│  ├─ README.md                                           │
│  └─ KNOWN_LIMITATIONS.md                                │
│                                                         │
│  DURING DEMO (Have open):                               │
│  ├─ PRESENTATION_SCRIPT.md                              │
│  ├─ CAPSTONE_DEMO_PLAN.md                               │
│  └─ DEMO_RUN_CHECKLIST.md                               │
│                                                         │
│  PROFESSOR ASKS (Reference):                            │
│  ├─ PROFESSOR_QA.md (27 answers ready)                  │
│  ├─ BUILD_STATUS.md                                     │
│  └─ ARCHITECTURE_PRESENTATION.md                        │
│                                                         │
│  DO NOT OPEN:                                           │
│  ├─ ARCHIVE/  (34 old reports)                          │
│  ├─ docs/archive/  (22 historical docs)                 │
│  └─ developer-only files                                │
│                                                         │
└─────────────────────────────────────────────────────────┘
```
