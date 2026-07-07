# Repository Structure

**Last updated**: 2026-07-07

## Layout Overview

```
raidv3/
├── .git/                       # Git metadata (hidden)
├── .gitignore                  # Git ignore rules
│
├── README.md                   # Project entry point          [ROOT]
├── LICENSE                     # MIT License                  [ROOT]
├── CHANGELOG.md                # Release history              [ROOT]
├── CONTRIBUTING.md             # How to contribute            [ROOT]
├── QUICK_START.md              # Quick setup guide            [ROOT]
├── SECURITY.md                 # Security policy              [ROOT]
├── DEMO.md                     # Capstone demo walkthrough    [ROOT]
├── KNOWN_LIMITATIONS.md        # Known issues & limitations   [ROOT]
├── THIRD_PARTY_NOTICES.md      # Third-party license info     [ROOT]
│
├── AGENT.md                    # AI agent operating rules     [ROOT]
├── MASTER_BACKLOG.md           # Single source of truth bugs  [ROOT]
├── DOCUMENT_INDEX.md           # Map of all documents         [ROOT]
├── OPENCODE_PROGRESS.md        # Session progress tracker     [ROOT]
├── OPENCODE_TEST_LOG.md        # Build/test results           [ROOT]
├── NEXT_SESSION.md             # Pre-flight for next session  [ROOT]
│
├── build.bat                   # Main build script            [ROOT]
├── build_asan.bat              # AddressSanitizer build       [ROOT]
├── build_stress.bat            # Stress-test build            [ROOT]
│
├── winfsp-x64.dll              # WinFsp runtime DLL           [ROOT]
├── libwinfsp-x64.a             # WinFsp static import lib     [ROOT]
│
├── docs/                       # Organized documentation
│   ├── architecture/           #   System design documents
│   ├── development/            #   Engineering workflow docs
│   ├── learning/               #   Tutorials and guides
│   ├── release/                #   Release artifacts
│   ├── research/               #   (future) Research notes
│   └── archive/                #   Historical/superseded docs
│
├── ARCHIVE/                    # Old reports moved from root
│                               # (32 historical documents)
│
├── src/                        # Source code (C/C++)
│   ├── *.c / *.h               #   Core source files
│   ├── test_*.c                #   Unit test source files
│   └── common.h                #   Shared types/defines
│
├── tests/                      # Extended stress/validation tests
│   ├── test_concurrent.c
│   ├── test_longrun.c
│   ├── test_metadata_corrupt.c
│   └── test_random_io.c
│
├── stress/                     # Power-fail stress test
│   └── test_powerfail.c
│
├── imgui/                      # Dear ImGui library (third-party)
│   ├── *.cpp / *.h             #   Core ImGui implementation
│   └── backends/               #   Win32 + DX11 backends
│
├── winfsp_headers/             # WinFsp SDK headers (third-party)
│   └── winfsp-2.1/inc/
│       └── fuse/               #   FUSE 2.8 API headers
│
├── build/                      # Object files (.o) — gitignored
│
├── *.exe                       # Build outputs — gitignored
├── Craidtest_*.dat             # Generated test artifacts — gitignored
└── _build.sh                   # [MOVED TO ARCHIVE] Redundant bash script
```

---

## Root Directory

### Files that MUST stay in root

| File | Purpose | Why Root |
|------|---------|----------|
| `README.md` | Project overview, build instructions | Standard entry point |
| `LICENSE` | MIT license text | Legal requirement |
| `CHANGELOG.md` | Version history | Discoverability |
| `CONTRIBUTING.md` | Contribution guide | Developer entry |
| `QUICK_START.md` | Quick setup | Convenience |
| `SECURITY.md` | Security policy | Standard |
| `THIRD_PARTY_NOTICES.md` | Third-party licenses | Legal requirement |
| `DEMO.md` | Capstone demo walkthrough | Demo convenience |
| `KNOWN_LIMITATIONS.md` | Known issues | Transparency |
| `AGENT.md` | AI agent operating rules | Engineering control |
| `MASTER_BACKLOG.md` | Bug/debt tracking | Engineering control |
| `DOCUMENT_INDEX.md` | Document map | Navigation |
| `OPENCODE_PROGRESS.md` | Progress tracking | Engineering control |
| `OPENCODE_TEST_LOG.md` | Test results | Engineering control |
| `NEXT_SESSION.md` | Session planning | Engineering control |
| `build.bat` | Main build script | Build entry point |
| `build_asan.bat` | ASan build variant | Build variant |
| `build_stress.bat` | Stress build variant | Build variant |
| `winfsp-x64.dll` | Runtime dependency | Required at runtime |
| `libwinfsp-x64.a` | Link-time dependency | Required for linking |

### Files moved to `docs/`

See `docs/` section below for categorization.

### Files moved to `ARCHIVE/`

- `_build.sh` — Redundant Bash-based build script; `build.bat` is the canonical build
- `_check.sh` — Redundant Bash check for selected files only

### Files deleted

- `Craidtest_8.dat` — Temporary generated test artifact
- All `*.exe` in root — Build outputs (recreatable via `build.bat`)

---

## `docs/` Directory

```
docs/
├── architecture/               # System design & architecture
│   ├── API.md                  #   Public API reference
│   ├── LOCK_ORDER.md           #   Lock ordering specification
│   ├── METADATA.md             #   Metadata format
│   ├── PRODUCT_DESIGN.md       #   Product design document
│   ├── SYSTEM_MAP.md           #   System architecture map
│   └── USER_FLOW.md            #   User interaction flow
│
├── development/                # Engineering workflow
│   ├── BUG_FIX_RESULT.md       #   B10-B12 fix implementation
│   ├── BUG_INVESTIGATION_RESULT.md  # B10-B14 source trace
│   ├── BUG_VERIFICATION.md     #   Independent bug audit report
│   ├── FUTURE_ROADMAP.md       #   Future development plans
│   ├── TEST_PLAN.md            #   Test strategy
│   └── WORKFLOW_VALIDATION.md  #   Per-button workflow verification
│
├── learning/                   # Tutorials & learning
│   └── LEARNING_GUIDE.md       #   System internals tutorial
│
├── release/                    # Presentation package (8 files)
│   ├── PRESENTATION_SCRIPT.md  #   10-section spoken script
│   ├── PROFESSOR_QA.md         #   27 prepared Q&A with evidence
│   ├── BUILD_STATUS.md         #   Build/test validation
│   ├── ARCHITECTURE_PRESENTATION.md   # Architecture diagram and demo
│   ├── FEATURE_MATRIX.md       #   66-feature classification
│   ├── CAPSTONE_DEMO_PLAN.md   #   10-segment demo plan
│   ├── FINAL_DEMO_OPERATOR_GUIDE.md    # Complete operator manual
│   └── DEMO_RUN_CHECKLIST.md   #   Pre-demo + live checklist
│
├── research/                   # (reserved for future research notes)
│
└── archive/                    # Historical/superseded docs
    ├── ARCHITECTURE_REPORT_SPRINT5.md
    ├── ARCHITECTURE.md
    ├── FINAL_ARCHITECTURE_AUDIT.md
    ├── GUI_REPORT.md
    ├── PERFORMANCE_TEST.md
    ├── RC_REPORT.md
    ├── RC1_REPORT.md
    ├── ROADMAP_V2.md
    ├── ROADMAP.md
    ├── summary.md
    ├── VALIDATION.md
    ├── WORKFLOW.md
    ├── benchmark/
    │   └── cli_bench.c
    ├── validation/
    │   ├── BUG_STATUS.md
    │   ├── DEMO_DATASET.md
    │   ├── HARDWARE_VALIDATION.md
    │   ├── PERFORMANCE_REPORT_TEMPLATE.md
    │   ├── PERFORMANCE.md
    │   ├── QA_REPORT.md
    │   ├── REAL_TEST.md
    │   ├── REGRESSION_REPORT.md
    │   ├── VALIDATION_MATRIX.md
    │   └── WINAPI_AUDIT.md
    ├── FINAL_DEMO_REHEARSAL.md
    ├── DEMO_ENVIRONMENT_REPORT.md
    ├── DEMO_FLOW_AUDIT.md
    ├── PRESENTATION_RISK_REPORT.md
    ├── CAPSTONE_FINAL_STATUS.md
    ├── DEMO_SCENARIO_CORRECTION.md
    ├── DEMO_SCENARIO_CORRECTION_REPORT.md
    ├── FINAL_CLEANUP_REPORT.md
    └── RELEASE_VERIFICATION.md
```

---

## `ARCHIVE/` Directory (Root)

Contains 32 historical documents moved from the root during a previous cleanup.
These are old audit reports, bug reports, session summaries, and implementation
reports that are now superseded.

**Future consolidation**: These could be merged into `docs/archive/` to
eliminate the duplicate archive location, but they are left here for now
to avoid breaking any existing references.

---

## `src/` Directory — Source Code

66 files total:
- 32 `.c` implementation files
- 32 `.h` header files
- 1 `common.h` — shared types, macros, constants
- 1 `gui.cpp` — C++ GUI (Dear ImGui + DX11)

### Source file categories

| Category | Files |
|----------|-------|
| Entry point | `main.c` |
| Service layer | `raid_service.c/.h`, `raid_query.c/.h` |
| FUSE bridge | `fuse_bridge.c/.h` |
| RAID engines | `stripe_engine.c/.h`, `mirror_engine.c/.h` |
| Cache & journal | `ram_cache.c/.h`, `journal.c/.h` |
| Storage I/O | `storage_common.c/.h`, `pool_io.c/.h` |
| Volume management | `volume_manager.c/.h`, `superblock.c/.h` |
| Device management | `device_manager.c/.h`, `disk_scanner.c/.h` |
| CLI | `cmd_handler.c/.h`, `wizard.c/.h` |
| GUI | `gui.cpp`, `gui.h`, `ui_model.c/.h` |
| Event system | `event_bus.c/.h` |
| Config | `config.c/.h` |
| Utilities | `crc32.c/.h`, `uuid.c/.h`, `logger.c/.h`, `profiler.c/.h`, `bench_io.c/.h` |
| Cleanup | `cleanup.c/.h` |
| Daemon | `daemon.c/.h`, `planner_engine.c/.h`, `metadata_manager.c/.h` |
| Tests | `test_runner.c`, `test_common.c/.h`, `test_stripe.c`, `test_mirror.c`, `test_cache.c`, `test_journal.c`, `test_superblock.c` |

---

## `tests/` Directory — Extended Stress Tests

4 stress/validation tests built separately from unit tests:

| File | Purpose |
|------|---------|
| `test_concurrent.c` | Multi-threaded concurrent I/O test |
| `test_longrun.c` | Long-duration stability test |
| `test_metadata_corrupt.c` | Metadata corruption recovery test |
| `test_random_io.c` | Random-access I/O pattern test |

---

## `stress/` Directory — Power-Fail Test

| File | Purpose |
|------|---------|
| `test_powerfail.c` | Simulated power-loss crash recovery |

---

## Third-Party Dependencies

| Directory | Library | Version |
|-----------|---------|---------|
| `imgui/` | Dear ImGui | 1.91.x (docking branch) |
| `imgui/backends/` | ImGui Win32 + DX11 backends | Matching |
| `winfsp_headers/` | WinFsp SDK headers | 2.1 |
| `winfsp-x64.dll` | WinFsp runtime DLL | 2.1 |
| `libwinfsp-x64.a` | WinFsp import library | 2.1 |

---

## Build Artifacts

All build outputs are covered by `.gitignore`:

| Pattern | Files | Status |
|---------|-------|--------|
| `build/*.o` | Object files | gitignored, present in working directory |
| `*.exe` | Executables | gitignored, present in working directory |
| `Craidtest_*.dat` | Test artifacts | gitignored, present in working directory |
