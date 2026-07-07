# RAIDV3 — Recommended Demo Package Structure

**Purpose:** Define the final repository structure optimized for capstone demonstration presentation.
**Audience:** Repository maintainers after demo preparation.
**Last updated:** 2026-07-08

---

## Principle

The repository root must contain only what the operator and professor see during a demo session. Engineering artifacts belong in subdirectories or archives.

---

## Final Recommended Structure

```
raidv3/
│
├── README.md                      # [STAY] Project entry point
├── QUICK_START.md                 # [STAY] Quick minimal guide (Chinese)
├── DEMO.md                        # [STAY] Quick demo walkthrough
├── KNOWN_LIMITATIONS.md           # [STAY] Honest limitations
├── CHANGELOG.md                   # [STAY] Version history
├── LICENSE                        # [STAY] MIT license
├── THIRD_PARTY_NOTICES.md         # [STAY] Legal notices
├── SECURITY.md                    # [STAY] Security policy
├── CONTRIBUTING.md                # [STAY] Contribution guide
│
├── build.bat                      # [STAY] Main build script
├── build_asan.bat                 # [STAY] ASan build variant
├── build_stress.bat               # [STAY] Stress build variant
│
├── winfsp-x64.dll                 # [STAY] WinFsp runtime (required)
├── libwinfsp-x64.a                # [STAY] WinFsp import lib (required)
│
├── AGENT.md                       # [STAY] AI agent rules (engineering)
├── MASTER_BACKLOG.md              # [STAY] Bug/debt tracking
├── DOCUMENT_INDEX.md              # [STAY] Master document index
├── REPOSITORY_STRUCTURE.md        # [STAY] Repository layout
├── OPENCODE_PROGRESS.md           # [STAY] Session tracker
├── OPENCODE_TEST_LOG.md           # [STAY] Test history
├── NEXT_SESSION.md                # [STAY] Pre-flight briefing
│
├── docs/
│   ├── release/                   # ★ DEMO PACKAGE — operator-facing
│   │   ├── FINAL_DEMO_OPERATOR_GUIDE.md     ★ NEW — Complete manual
│   │   ├── DEMO_DOCUMENT_MAP.md             ★ NEW — Doc classification
│   │   ├── DEMO_PACKAGE_STRUCTURE.md        ★ NEW — This file
│   │   ├── PRESENTATION_SCRIPT.md           — Spoken script
│   │   ├── PROFESSOR_QA.md                 — 27 Q&A with evidence
│   │   ├── DEMO_RUN_CHECKLIST.md           — Pre-demo + live checklist
│   │   ├── CAPSTONE_DEMO_PLAN.md           — 10-segment plan
│   │   ├── BUILD_STATUS.md                 — Build/test proof
│   │   ├── ARCHITECTURE_PRESENTATION.md    — Architecture slides
│   │   └── FEATURE_MATRIX.md              — Feature inventory
│   │
│   ├── development/               — Engineering workflow (unchanged)
│   │   ├── BUG_VERIFICATION.md
│   │   ├── BUG_FIX_RESULT.md
│   │   ├── BUG_INVESTIGATION_RESULT.md
│   │   ├── FUTURE_ROADMAP.md
│   │   ├── TEST_PLAN.md
│   │   └── WORKFLOW_VALIDATION.md
│   │
│   ├── architecture/              — System design docs (unchanged)
│   │   ├── API.md, LOCK_ORDER.md, METADATA.md
│   │   ├── PRODUCT_DESIGN.md, SYSTEM_MAP.md, USER_FLOW.md
│   │
│   ├── learning/                  — Tutorials (unchanged)
│   │   └── LEARNING_GUIDE.md
│   │
│   └── archive/                   — Historical / superseded
│       ├── (original archived sprint docs)
│       └── (prep/rehearsal docs moved from release/ )
│           ├── FINAL_DEMO_REHEARSAL.md
│           ├── DEMO_ENVIRONMENT_REPORT.md
│           ├── DEMO_FLOW_AUDIT.md
│           ├── PRESENTATION_RISK_REPORT.md
│           ├── CAPSTONE_FINAL_STATUS.md
│           ├── DEMO_SCENARIO_CORRECTION.md
│           ├── DEMO_SCENARIO_CORRECTION_REPORT.md
│           ├── FINAL_CLEANUP_REPORT.md
│           └── RELEASE_VERIFICATION.md
│
├── ARCHIVE/                       # [STAY] Root archive (34 old reports)
│
├── src/                           # [STAY] Source code (untouched)
├── tests/                         # [STAY] Stress tests (untouched)
├── stress/                        # [STAY] Powerfail test (untouched)
├── imgui/                         # [STAY] Dear ImGui library (untouched)
├── winfsp_headers/                # [STAY] WinFsp SDK (untouched)
└── build/                         # [STAY] Object files (gitignored)
```

---

## Changes Made in This Session

### New Files (4)
| File | Purpose |
|------|---------|
| `docs/release/DEMO_DOCUMENT_MAP.md` | Classify all docs for demo day use |
| `docs/release/FINAL_DEMO_OPERATOR_GUIDE.md` | Complete operator manual |
| `docs/release/DEMO_PACKAGE_STRUCTURE.md` | This file — proposed structure |
| `docs/release/FINAL_DEMO_OPERATOR_TRAINING_REPORT.md` | Session report |

### Moved to `docs/archive/` (9)
| File | Old Location | New Location |
|------|-------------|-------------|
| `FINAL_DEMO_REHEARSAL.md` | `docs/release/` | `docs/archive/` |
| `DEMO_ENVIRONMENT_REPORT.md` | `docs/release/` | `docs/archive/` |
| `DEMO_FLOW_AUDIT.md` | `docs/release/` | `docs/archive/` |
| `PRESENTATION_RISK_REPORT.md` | `docs/release/` | `docs/archive/` |
| `CAPSTONE_FINAL_STATUS.md` | `docs/release/` | `docs/archive/` |
| `DEMO_SCENARIO_CORRECTION.md` | `docs/release/` | `docs/archive/` |
| `DEMO_SCENARIO_CORRECTION_REPORT.md` | `docs/release/` | `docs/archive/` |
| `FINAL_CLEANUP_REPORT.md` | `docs/release/` | `docs/archive/` |
| `RELEASE_VERIFICATION.md` | `docs/release/` | `docs/archive/` |

### Not Moved (kept in `docs/release/`) — 10 files
`PRESENTATION_SCRIPT.md`, `PROFESSOR_QA.md`, `DEMO_RUN_CHECKLIST.md`, `CAPSTONE_DEMO_PLAN.md`, `BUILD_STATUS.md`, `ARCHITECTURE_PRESENTATION.md`, `FEATURE_MATRIX.md`, `DEMO_DOCUMENT_MAP.md`, `FINAL_DEMO_OPERATOR_GUIDE.md`, `DEMO_PACKAGE_STRUCTURE.md`

### No Changes
- `src/`, `tests/`, `stress/`, `imgui/`, `winfsp_headers/` — untouched
- `ARCHIVE/` — untouched
- `docs/development/`, `docs/architecture/`, `docs/learning/` — untouched
- `build.bat`, `build_asan.bat`, `build_stress.bat` — untouched

---

## Demo Day File Access Strategy

| Context | Open These |
|---------|------------|
| Before demo prep | `FINAL_DEMO_OPERATOR_GUIDE.md` → `DEMO_RUN_CHECKLIST.md` |
| During presentation | `PRESENTATION_SCRIPT.md` → `CAPSTONE_DEMO_PLAN.md` |
| Professor Q&A | `PROFESSOR_QA.md` → `BUILD_STATUS.md` → `ARCHITECTURE_PRESENTATION.md` |
| Never | Any file in `ARCHIVE/`, `docs/archive/`, `docs/development/`, or source code |
