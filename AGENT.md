# Agent Operating Rules

Last updated: 2026-07-07

## Source of Truth

`MASTER_BACKLOG.md` is the single source of truth for engineering work.
Future agents must ignore all archived reports.

## Repository Structure

```
root/
├── docs/
│   ├── architecture/   → System design documents
│   ├── development/    → Engineering workflow docs
│   ├── learning/       → Tutorials and guides
│   ├── release/        → Release artifacts
│   ├── research/       → Research notes (future use)
│   └── archive/        → Historical/superseded docs
├── ARCHIVE/            → Old reports moved from root
├── src/                → Source code (DO NOT MODIFY)
├── tests/              → Extended stress/validation tests
├── stress/             → Power-fail stress test
├── imgui/              → Dear ImGui library (third-party)
├── winfsp_headers/     → WinFsp SDK headers (third-party)
└── build/              → Object files (gitignored)
```

## Rules

1. Do not modify any C/C++ source files (`src/*.c`, `src/*.h`, `src/*.cpp`).
2. Do not modify any test files (`src/test_*.c`, `tests/*.c`, `stress/*.c`).
3. Do not modify build scripts (`build.bat`, `build_asan.bat`, `build_stress.bat`).
4. Only reorganize documentation and update engineering control files.
5. All audit reports, bug reports, and session transcripts in `ARCHIVE/` are superseded — do not use them as references.
6. Old documents in `docs/archive/` are historical — for context only.

## Reference Documents

| Topic | File | Location |
|-------|------|----------|
| Bug/debt tracking | `MASTER_BACKLOG.md` | root |
| Architecture map | `SYSTEM_MAP.md` | `docs/architecture/` |
| API reference | `API.md` | `docs/architecture/` |
| Product design | `PRODUCT_DESIGN.md` | `docs/architecture/` |
| Metadata format | `METADATA.md` | `docs/architecture/` |
| Lock ordering | `LOCK_ORDER.md` | `docs/architecture/` |
| User flows | `USER_FLOW.md` | `docs/architecture/` |
| Bug verification | `BUG_VERIFICATION.md` | `docs/development/` |
| Test plan | `TEST_PLAN.md` | `docs/development/` |
| Future roadmap | `FUTURE_ROADMAP.md` | `docs/development/` |
| Learning guide | `LEARNING_GUIDE.md` | `docs/learning/` |
| Design constraints | `KNOWN_LIMITATIONS.md` | root |

## Active Engineering Control Files

These files are updated by agents during each session:

| File | Location | Purpose |
|------|----------|---------|
| `MASTER_BACKLOG.md` | root | All confirmed bugs, tech debt, remaining work |
| `DOCUMENT_INDEX.md` | root | Master index of all documentation |
| `NEXT_SESSION.md` | root | Pre-flight briefing, phase tracking, next action |
| `OPENCODE_PROGRESS.md` | root | Repository-level progress tracking |
| `OPENCODE_TEST_LOG.md` | root | Build/test results history |

## Where New Reports Go

| Report Type | Location |
|-------------|----------|
| New bug/audit findings | `docs/development/` |
| Architecture analysis | `docs/architecture/` |
| Learning materials | `docs/learning/` |
| Release verification | `docs/release/` |
| Research notes | `docs/research/` |

## Workflow

1. Read this file and `MASTER_BACKLOG.md` before starting any task.
2. Refer to `REPOSITORY_STRUCTURE.md` to understand the file layout.
3. For design questions, consult `docs/architecture/` files.
4. For bug/debt work, update `MASTER_BACKLOG.md` only — never create new standalone bug reports.
5. After completing a task, update `DOCUMENT_INDEX.md`, `OPENCODE_PROGRESS.md`, `OPENCODE_TEST_LOG.md`, and `NEXT_SESSION.md`.
6. Run `build.bat` and `raidtest_tests.exe` before finishing to verify no regression.
