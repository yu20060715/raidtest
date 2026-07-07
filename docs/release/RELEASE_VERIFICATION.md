# RAIDTEST v1.0 RC4 — Release Verification

**Date:** 2026-07-07
**Engineer:** Release Candidate Engineer

---

## 1. Build Result

| Target | Status | Warnings |
|--------|--------|----------|
| `raidtest_winfsp.exe` (main binary) | **PASS** — 2,169,261 bytes | 2 pre-existing (stripe_engine.c always-true; gui.cpp strncpy truncation) |
| `raidtest_tests.exe` (test runner) | **PASS** — 553,986 bytes | Same as above (repeated in test build) |
| `test_concurrent.exe` | **PASS** — 501,888 bytes | Same warnings from shared objects |
| `test_random_io.exe` | **PASS** — 502,420 bytes | Same |
| `test_metadata_corrupt.exe` | **PASS** — 502,226 bytes | Same |
| `test_powerfail.exe` | **PASS** — 503,033 bytes | Same |
| `test_longrun.exe` | **PASS** — 500,864 bytes | Same |

**No new warnings.** All warnings are pre-existing and identical to previous builds.

**No build regression.** All targets build successfully.

---

## 2. Test Result

| Suite | Tests | Passed | Failed |
|-------|-------|--------|--------|
| Unit tests (`raidtest_tests.exe`) | 39 | 39 | 0 |
| Concurrent (`test_concurrent.exe`) | — | PASS | 0 |
| Random I/O (`test_random_io.exe`) | — | PASS | 0 |
| Metadata corrupt (`test_metadata_corrupt.exe`) | — | PASS | 0 |
| Power fail (`test_powerfail.exe`) | — | PASS | 0 |
| Long run (`test_longrun.exe`) | — | Not executed (safe) | — |

**39/39 tests passing.** Results consistent with previous engineering sessions.

---

## 3. GUI Result

All modes and panels traced via source code analysis of `src/gui.cpp` (1695 lines):

### Mode Tabs
| Tab | Reachable | Functional |
|-----|-----------|------------|
| Beginner | ✓ Mode selector at top of window | ✓ All buttons dispatch correct worker actions |
| Advanced | ✓ Same selector | ✓ Shared toolbar + split panels |
| Developer | ✓ Same selector | ✓ Shared toolbar + performance dashboard |

### Beginner Mode Buttons
| Button | Reachable | Functional |
|--------|-----------|------------|
| Quick Setup | ✓ Shown when not busy | ✓ Dispatches `W_QUICK_SETUP` |
| Scan Disks | ✓ Shown when not busy | ✓ Dispatches `W_SCAN` |
| Restore Volume | ✓ Shown when not busy | ✓ Dispatches `W_LOAD_CONFIG` |
| Health Check | ✓ Shown when not busy | ✓ Dispatches `W_CHECK` |
| Unmount | ✓ Shown when not busy + mounted | ✓ Dispatches `W_UNMOUNT` |
| Benchmark | ✓ Shown when not busy + mounted | ✓ Dispatches `W_BENCHFS` |

### Advanced/Developer Toolbar Buttons
| Button | Reachable | Functional |
|--------|-----------|------------|
| Scan | ✓ Always (when not busy) | ✓ `W_SCAN` |
| Create | ✓ State==1 + selected>=2 | ✓ `W_CREATE` with pool params |
| Mirror | ✓ State==1 + selected>=2 | ✓ `W_MIRROR` |
| Mount | ✓ State>=2 + not mounted | ✓ `W_MOUNT` |
| Unmount | ✓ Mounted | ✓ `W_UNMOUNT` |
| Destroy | ✓ State>=2 | ✓ Opens confirm dialog → `W_DESTROY` |
| Bench | ✓ Mounted | ✓ `W_BENCHFS` |

### Menu Bar
| Menu Item | Reachable | Functional |
|-----------|-----------|------------|
| File → Refresh | ✓ Advanced/Developer only | ✓ `W_REFRESH` |
| File → Export Diagnostic | ✓ Advanced/Developer | ✓ `W_EXPORT` |
| File → Settings | ✓ Always | ✓ Opens Settings dialog |
| File → Exit | ✓ Always | ✓ `PostQuitMessage(0)` |
| Actions → Scan | ✓ Advanced/Developer | ✓ `W_SCAN` |
| Actions → Create/Mirror | ✓ State-dependent | ✓ `W_CREATE` / `W_MIRROR` |
| Actions → Restore | ✓ Always | ✓ Opens Restore wizard |
| Actions → Mount/Unmount | ✓ State-dependent | ✓ `W_MOUNT` / `W_UNMOUNT` |
| Actions → Destroy | ✓ State>=2 | ✓ Opens confirm dialog |
| Actions → Benchmark | ✓ Mounted | ✓ `W_BENCHFS` |
| Actions → Rebuild | ✓ Always | ✓ Opens Rebuild wizard |
| View → Settings | ✓ Advanced/Developer | ✓ Opens Settings dialog |
| View → About | ✓ Advanced/Developer | ✓ Opens About dialog |

### Dialogs
| Dialog | Opens When | Buttons |
|--------|------------|---------|
| Welcome Wizard | First run | Quick Setup / Explore Beginner / Don't show again |
| Restore Volume | Actions → Restore | From Superblock / From Saved Config / Cancel |
| Rebuild RAID | Actions → Rebuild | Start Rebuild / Cancel |
| Confirm Destroy | Destroy btn/action | Yes, Destroy / Cancel |
| Benchmark Results | Bench btn | Run Again / Close / Cancel |
| Export Diagnostic | Export menu item | Close |
| Settings | Settings menu item | Save Settings / Cancel |
| About | View → About | Display only (no buttons) |

**No dead actions found.** Every button maps to a valid `WorkerAction` enum value and dispatches the corresponding `start_worker()` call.

---

## 4. CLI Result

| Check | Result |
|-------|--------|
| `--help` output | 28 commands listed, 8 options listed |
| `--version` output | `RAIDTEST v1.0 RC4 (build Jul  7 2026 21:10:07)` |
| Commands exist in dispatch | All `--help` commands verified present in `cmd_handler.c::cmd_process()` (lines 242-275) |
| No fake commands | Every listed command has a corresponding function |

### CLI Command Inventory (from `--help`)

| Command | Dispatch Match | Command | Dispatch Match |
|---------|---------------|---------|---------------|
| `scan` | ✓ line 244 | `mapdrive` | ✓ line 245 |
| `bench` | ✓ line 246 | `select` | ✓ line 247 |
| `init` | ✓ line 248 | `create` | ✓ line 249 |
| `mirror` | ✓ line 250 | `rebuild` | ✓ line 252 |
| `cache` | ✓ line 253 | `mount` | ✓ line 254 |
| `unmount` | ✓ line 255 | `load` | ✓ line 256 |
| `purge` | ✓ line 257 | `destroy` | ✓ line 258 |
| `test` | ✓ line 259 | `random` | ✓ line 260 |
| `benchfs` | ✓ line 261 | `check` | ✓ line 262 |
| `info` | ✓ line 263 | `map` | ✓ line 264 |
| `status` | ✓ line 265 | `metadata` | ✓ line 266 |
| `simulate` | ✓ line 267 | `planner` | ✓ line 268 |
| `events` | ✓ line 269 | `config-save` | ✓ line 270 |
| `config-load` | ✓ line 271 | `wizard` | ✓ line 272 |
| `quick` | ✓ line 273 | `cleanup` | ✓ line 274 |
| `help` | ✓ line 243 | `expand` | ✓ line 251 |
| `exit` / `quit` | ✓ line 242 | | |

---

## 5. Remaining Known Limitations

### Non-Blocking Issues (recorded only — no fix required)

| # | Area | Description | Source |
|---|------|-------------|--------|
| L1 | Repository | `Craidtest_8.dat` test artifact file present in repo root (RELEASE_CHECKLIST.md item marked done but file remains) | ✅ Resolved — file removed (already covered by .gitignore `Craidtest_*.dat`) |
| L2 | Documentation | `README.md` line 263 project structure tree shows `RC1_REPORT.md` in root but file moved to `docs/archive/RC1_REPORT.md` | ✅ Resolved — tree now shows `docs/archive/RC1_REPORT.md` |
| L3 | Documentation | `README.md` CLI command table (lines 160-189) does not include `select`, `status`, `test`, `exit` which are listed by `--help` | ✅ Resolved — commands added to table + features section |

### Known Design Limitations (from KNOWN_LIMITATIONS.md)

- Windows only — no Linux/macOS support
- WinFsp dependency — mount is FUSE-based, not a block device
- Max 4 disks per volume
- No RAID5/6 — only RAID0, RAID1, RAID10 planner
- Journal is prototype — no circular buffering, no data CRC
- No S.M.A.R.T. — disk health attributes not read
- No encryption — data on disk is plaintext

### Confirmed Bugs (from MASTER_BACKLOG.md — not fixed for demo)

| ID | Priority | Description |
|----|----------|-------------|
| B1 | P0 | OVERLAPPED allocated on stack; kernel writes to freed stack memory |
| B2 | P0 | `parent_dir_exists()` copies unchecked path length into fixed buffer |
| B3 | P0 | `raid_rename()` concatenates path+suffix without bounds check |
| B4 | P1 | `file_table_lock_init()` has no synchronization |
| B5 | P1 | `fuse_unmount()` calls DeleteCriticalSection while FUSE callbacks may execute |
| B6 | P1 | Journal file writes have no lock; concurrent flush+FUSE path corrupts entries |
| B7 | P1 | Event bus critical section never deleted |
| B8 | P1 | 8 `device_get()` call sites dereference without NULL check |
| B9 | P1 | ~25+ `vol->disks[i]` accesses without NULL checks |

### Technical Debt (from MASTER_BACKLOG.md — not fixed for demo)

| ID | Priority | Description |
|----|----------|-------------|
| T1 | P0 | 24 raid_service functions + 14 FUSE callbacks access g_state without lock |
| T2 | P1 | All superblock tests use hardcoded C:\RAIDTEST\ path |
| T3 | P2 | I/O functions return bool instead of RC error codes |
| T4 | P2 | APP_STATE defined in CLI header, included by GUI files only for type |
| T5 | P2 | No documented lock ordering between g_state_cs, cache->lock, g_eb_cs |
| T6 | P2 | Journal grows unbounded; no circular buffering |
| T7 | P2 | File table is flat 64-entry array with O(n) lookup |

---

## 6. Final Release Status

**READY** — All 3 non-blocking cleanup issues resolved (2026-07-07 Release Cleanup Engineer).

### Resolution Summary

| # | Area | Action Taken |
|---|------|-------------|
| L1 | Repository | Deleted `Craidtest_8.dat` from repo root (already gitignored, untracked) |
| L2 | Documentation | Updated README.md project tree: `RC1_REPORT.md` → `docs/archive/RC1_REPORT.md` |
| L3 | Documentation | Added `select`, `status`, `test`, `exit` to README.md CLI table + features summary |
| — | Build | `build.bat` — OK (same 2 pre-existing warnings, no new warnings) |
| — | Tests | `raidtest_tests.exe` — 39/39 passed, no regression |
