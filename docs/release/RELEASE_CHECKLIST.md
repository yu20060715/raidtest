# RAIDTEST v1.0 — Release Checklist

## Pre-Release

- [ ] All source files compile without warnings (build.bat, both GCC and clang-cl paths)
- [ ] All 38 unit tests pass (`raidtest_tests.exe`)
- [ ] All 6 stress/validation executables build successfully
- [ ] `--version` outputs `RAIDTEST v1.0 RC4 (build ...)`
- [ ] `--help` lists all 14 options and 25 commands
- [ ] GUI launches without crash (`raidtest_winfsp.exe` with no args)
- [ ] CLI mode functions correctly (`raidtest_winfsp.exe --cli`)

## Documentation

- [x] README.md — project overview, features, build, usage, tests
- [x] CHANGELOG.md — complete history (v0.1 → v1.0 RC4)
- [x] KNOWN_LIMITATIONS.md — 45+ documented limitations
- [x] LICENSE — MIT (present)
- [x] THIRD_PARTY_NOTICES.md — ImGui, WinFsp, DirectX, MinGW
- [x] CONTRIBUTING.md — how to contribute
- [x] RELEASE_CHECKLIST.md — this file
- [x] HANDOFF.md — architecture, build, dependencies, roadmap
- [x] FINAL_RELEASE_REPORT.md — build, tests, issues, readiness

## Source Cleanup

- [x] Remove test artifacts (`Craidtest_*.dat`, `test_out.txt`, `test_err.txt`)
- [x] Remove dead/stub code (`benchmark/cli_bench.c` stub)
- [x] Remove unused regression test stubs (`tests/regression/`)
- [x] Remove build artifacts (`build/*.o`)
- [x] Verify `.gitignore` covers all build outputs
- [x] Review source for obsolete comments

## Build Verification

- [x] `build.bat` completes without errors (GCC path)
- [x] `build.bat` completes without errors (clang-cl path) — *requires MSVC*
- [x] `raidtest_winfsp.exe` runs and shows version
- [x] `raidtest_tests.exe` runs — 38/38 pass
- [x] Stress executables exist (`test_longrun.exe`, etc.)
- [x] No undefined references or link errors

## Release Artifacts

- [x] `raidtest_winfsp.exe` — main binary (~2.1 MB, static)
- [x] `raidtest_tests.exe` — test runner (~0.5 MB)
- [x] Stress test executables (6 files, ~3 MB total)
- [x] `winfsp-x64.dll` — WinFsp runtime (bundled for convenience)
- [x] Source tree complete and clean

## Final Sign-Off

- [ ] Build: all targets compile clean
- [ ] Tests: 38/38 pass
- [ ] Binary: version string correct
- [ ] Documentation: complete and accurate
- [ ] Decision: **READY FOR v1.0** / **NOT READY**
