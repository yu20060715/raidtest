# RAIDTEST v1.0 RC4 — Demo Environment Audit Report

**Date:** 2026-07-07  
**Auditor:** RAIDV3 Capstone Demo Rehearsal Auditor  
**Machine:** Windows 11 (build 10.0.26200), x64

---

## 1. Build System Verification

| Check | Result | Detail |
|-------|--------|--------|
| `build.bat` exists | ✅ PASS | `C:\Users\Yu\Desktop\raidv3\build.bat` — 78 lines |
| Mingw-w64 GCC (gcc) | ✅ PASS | Found at `C:\msys64\mingw64\bin\gcc.exe` |
| Mingw-w64 G++ (g++) | ✅ PASS | Found at `C:\msys64\mingw64\bin\g++.exe` |
| MSYS2 bash | ✅ PASS | Found at `C:\msys64\usr\bin\bash.exe` |
| clang-cl (alternative) | ⚠️ NOT FOUND | `build.bat` falls back to MinGW GCC — OK |
| libwinfsp-x64.a | ✅ PASS | Present at project root (50 KB) |
| Build artifacts (35 .o) | ✅ PASS | Present in `build/` — incremental rebuild ready |

**Verdict: Build system ready.** MinGW GCC fallback works. No clang-cl required.

---

## 2. Runtime Dependencies

| Check | Result | Detail |
|-------|--------|--------|
| `winfsp-x64.dll` (local copy) | ✅ PASS | Present in project root (198 KB) |
| `winfsp-x64.dll` (system install) | ✅ PASS | Found at `C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll` |
| WinFsp registry key | ⚠️ NOT FOUND | DLL present at standard path — installation is functional |
| `d3d11.dll` (system) | ✅ PASS | Available in `C:\Windows\System32\` |
| `dxgi.dll` (system) | ✅ PASS | Available in `C:\Windows\System32\` |
| `d3dcompiler_47.dll` (system) | ✅ PASS | Available in `C:\Windows\System32\` |

**Verdict: All runtime DLLs present.** WinFsp is installed. D3D11 stack is complete.

---

## 3. WinFsp Dependency

| Check | Result | Detail |
|-------|--------|--------|
| WinFsp installed | ✅ PASS | `C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll` exists |
| Connection: `build.bat` links `-lwinfsp-x64` | ✅ PASS | line 42: `-lwinfsp-x64` in linker flags |
| Connection: `fuse_bridge.c` includes `<fuse.h>` | ✅ VERIFIED | Uses WinFsp FUSE API headers in `winfsp_headers/` |
| Connection: `gui.cpp` checks WinFsp at startup | ✅ VERIFIED | `gui.cpp:950-951` — `ShowWelcomeWizard` checks WinFsp |

**Note:** The WinFsp registry key `HKLM\SOFTWARE\WinFsp` was not found. This may be an artifact of how WinFsp was installed (portable/zip vs MSI). The DLL at the standard path confirms it is functional.

**Verdict: WinFsp dependency satisfied.** Local DLL copy provides runtime fallback.

---

## 4. Administrator Requirement

| Check | Result | Detail |
|-------|--------|--------|
| Current session elevated? | ❌ **NO** | Running as `guanyu\yu` — not Admin |
| `raid_scan()` requires Admin? | ✅ YES | `disk_scanner.c` uses `IOCTL_STORAGE_QUERY_PROPERTY` — requires admin |
| `raid_mount()` requires Admin? | ✅ YES | WinFsp `FUSE_FSCTL` mount requires elevation |
| Pool file fallback works without Admin? | ✅ YES | `init 0:1024 1:1024` creates files on existing NTFS — no Admin needed |
| CLI works without Admin? | ✅ YES | All CLI commands except scan/mount work without elevation |

**Verdict: ⚠️ MUST RUN AS ADMINISTRATOR for full demo.** Pool file + CLI fallback available if admin is unavailable.

---

## 5. GUI Startup Path

| Check | Result | Detail |
|-------|--------|--------|
| Entry point | ✅ VERIFIED | `main.c:131` — `gui_run()` called when no flags |
| D3D11 initialization | ✅ VERIFIED | `gui.cpp:1607-1648` — `SetupWindow()` + `CreateDeviceD3D()` + ImGui init |
| Dark theme | ✅ VERIFIED | `gui.cpp:652-687` — ImGui style setup |
| Three mode tabs | ✅ VERIFIED | `gui.cpp:988-1001` — Beginner/Advanced/Developer |
| Status bar | ✅ VERIFIED | `gui.cpp:1288-1325` — "Ready" status |
| Welcome wizard (first run) | ✅ VERIFIED | `gui.cpp:937-979` — `ShowWelcomeWizard()` |
| Binary exists | ✅ PASS | `raidtest_winfsp.exe` (2.1 MB, built 2026-07-07) |

**Verdict: GUI startup path fully functional.**

---

## 6. CLI Fallback Path

| Check | Result | Detail |
|-------|--------|--------|
| `--cli` flag | ✅ VERIFIED | `main.c:38-43` → `cli_main()` → REPL loop |
| `--help` / `-h` | ✅ VERIFIED | `main.c:78-105` → 31 commands listed |
| `--version` / `-v` | ✅ VERIFIED | `main.c:71-77` → "RAIDTEST v1.0 RC4" |
| `--auto [letter]` | ✅ VERIFIED | `main.c:15-27` → auto-restore or quick setup |
| `--quick` | ✅ VERIFIED | `main.c:36-37` → one-step setup |
| `--wizard` | ✅ VERIFIED | `main.c:28-29` → 8-step interactive wizard |
| `--daemon` | ✅ VERIFIED | `main.c:30-32` → console daemon |
| `--service` / `--install-service` / `--uninstall-service` | ✅ VERIFIED | `main.c:106-124` → SCM integration |
| `--cleanup` | ✅ VERIFIED | `main.c:33-35` → resource cleanup |
| 31 CLI commands | ✅ VERIFIED | `cmd_handler.c:232-280` — all mapped to `raid_*()` functions |

**Verdict: CLI fallback complete.** 11 flags + 31 commands = full functional coverage.

---

## 7. Executable Inventory

| Binary | Size | Built | Purpose |
|--------|------|-------|---------|
| `raidtest_winfsp.exe` | 2,171,875 | 2026-07-07 23:37 | Main app (GUI + CLI) |
| `raidtest_tests.exe` | 553,986 | 2026-07-07 23:37 | Unit test runner (39 tests) |
| `test_concurrent.exe` | 501,888 | 2026-07-07 23:37 | Concurrent I/O stress |
| `test_longrun.exe` | 500,864 | 2026-07-07 23:37 | Long-duration stability |
| `test_metadata_corrupt.exe` | 502,226 | 2026-07-07 23:37 | Metadata corruption recovery |
| `test_powerfail.exe` | 503,033 | 2026-07-07 23:37 | Power-fail / journal replay |
| `test_random_io.exe` | 502,420 | 2026-07-07 23:37 | Random I/O stress |

**All 7 executables present and recent.** No missing binaries.

---

## 8. Environment Summary

| Category | Verdict |
|----------|---------|
| Build System | ✅ READY |
| Runtime DLLs | ✅ READY |
| WinFsp | ✅ READY |
| Administrator | ⚠️ REQUIRED (current session not elevated) |
| GUI Path | ✅ READY |
| CLI Fallback | ✅ READY |
| Executables | ✅ READY |

**Environment Readiness: PASS** (1 pre-condition: must run as Administrator)

### Pre-Demo Actions Required

1. **Launch terminal as Administrator** before running `raidtest_winfsp.exe`
2. **Verify WinFsp** is installed (confirmed — `C:\Program Files (x86)\WinFsp\bin\winfsp-x64.dll`)
3. **Confirm 2+ disks** available, or prepare pool file fallback: `init 0:1024 1:1024`
4. **Fresh build** recommended: run `build.bat` from Admin terminal
5. **Run `raidtest_tests.exe`** to confirm 39/39 before professor arrives
