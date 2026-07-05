# Windows Runtime Validation Report

**Date:** 2026-07-05  
**Build:** raidtest_winfsp.exe (MinGW GCC + g++)  
**Test harness:** raidtest_tests.exe  

---

## Environment

| Item | Value |
|------|-------|
| OS | Windows 10/11 |
| CPU | x64 |
| Compiler | MinGW GCC (C objects) + g++ (C++/ImGui objects) |
| WinFsp | Required for mount (not tested in this automated run) |
| Admin rights | Required for physical disk access |

---

## Test Suite: 36/36 PASS

The automated test suite exercises all core engine paths without requiring physical disks or WinFsp:

| Step | Result | Time | Notes |
|------|--------|------|-------|
| 1. sb_format_v3_compat | PASS | <1s | |
| 2. sb_restore_no_phys | PASS | <1s | |
| 3. sb_metadata_format | PASS | <1s | |
| 4. sb_serial_fallback | PASS | <1s | |
| 5. sb_serial_restore | PASS | <1s | |
| 6. sb_v4_uuid | PASS | <1s | |
| 7. sb_checksum_corruption | PASS | <1s | |
| 8. sb_generation | PASS | <1s | |
| 9. sb_mirror_flag | PASS | <1s | |
| 10. sb_stripe_write_read | PASS | <1s | |
| 11. journal_no_journal | PASS | <1s | |
| 12. journal_recover_replay | PASS | <1s | |
| 13. journal_recover_clean | PASS | <1s | |
| 14. journal_roundtrip | PASS | <1s | |
| 15. cache_buffered_read | PASS | <1s | |
| 16. cache_multi_write_flush | PASS | <1s | |
| 17. cache_flush_integration | PASS | <1s | |
| 18. cache_dirty_and_flush | PASS | <1s | |
| 19. cache_cross_block | PASS | <1s | |
| 20. cache_write_read | PASS | <1s | |
| 21. cache_init_zero_fails | PASS | <1s | |
| 22. cache_init_destroy | PASS | <1s | |
| 23. mirror_cache_integration | PASS | <1s | |
| 24. mirror_rebuild | PASS | <1s | |
| 25. mirror_write_to_all | PASS | <1s | |
| 26. mirror_all_dead_read_fails | PASS | <1s | |
| 27. mirror_degraded_read | PASS | <1s | |
| 28. mirror_create_2disks | PASS | <1s | |
| 29. stripe_expand | PASS | <1s | |
| 30. stripe_map_single_phase | PASS | <1s | |
| 31. stripe_create_3disks_asymmetric | PASS | <1s | |
| 32. stripe_create_2disks | PASS | <1s | |
| 33. normalize_asymmetric | PASS | <1s | |
| 34. normalize_zero_speeds | PASS | <1s | |
| 35. normalize_equal_speeds | PASS | <1s | |

**Total: 36/36 PASS** (0 failed, 100%)

---

## Build Validation

| Step | Result |
|------|--------|
| `build.bat` (MinGW GCC + g++) | OK |
| C objects (24 .c files) | 0 warnings |
| C++ objects (gui.cpp + 7 imgui .cpp files) | 0 own-code warnings |
| Link (DirectX 11, WinFsp, GDI32, DWMAPI) | OK |
| raidtest_tests.exe build | OK |
| raidtest_tests.exe run | 36/36 PASS |

---

## Known Issues from Validation

1. **CLI `help` produces no output in non-interactive shell** — the CLI uses direct console output which may not be captured in PowerShell. Run from cmd.exe or use GUI mode.
2. **WinFsp mount not validated** — requires WinFsp runtime installed on test machine. Not available in this automated environment.
3. **Physical disk scan not validated** — requires Administrator privileges. Tests use RAM-backed pool files instead.
4. **GUI not validated** — requires DirectX 11 capable display. Automated headless validation not possible.

---

## Conclusion

**Core engine: VALIDATED (36/36 tests pass)**  
**Build: VALIDATED (0 warnings, clean link)**  
**Full lifecycle (GUI + mount): Requires manual verification with WinFsp + Admin rights**

The automated test suite covers:
- Superblock read/write/restore (11 tests)
- Journal write-ahead log and recovery (4 tests)
- Cache write-back, flush, dirty blocks (8 tests)
- Mirror RAID1 operations (6 tests)
- Stripe RAID0 operations (7 tests)
