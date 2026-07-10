@echo off
cd /d "%~dp0"

:: Build existing tests with AddressSanitizer + UndefinedBehaviorSanitizer
:: Requires GCC 12+ or clang with ASan support

echo Building with AddressSanitizer + UBSan...

:: GCC with ASan and UBSan
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc -Wall -O1 -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -Isrc src/stripe_engine.c src/mirror_engine.c src/storage_common.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c -o asan_checks.exe -static-libgcc -static-libasan 2>&1"
if errorlevel 1 (
  echo ASan minimal build FAILED
  echo.
  echo NOTE: -static-libasan may not be available on all MinGW installations.
  echo If it fails, try removing -static-libasan and ensure libasan is in PATH.
  pause
  exit /b 1
)

:: Build the full test suite with ASan
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc -Wall -O1 -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -Isrc src/test_runner.c src/test_common.c src/test_stripe.c src/test_mirror.c src/test_cache.c src/test_journal.c src/test_superblock.c src/stripe_engine.c src/mirror_engine.c src/storage_common.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c -o raidtest_tests_asan.exe -static-libgcc -static-libasan 2>&1"
if errorlevel 1 (
  echo ASan test suite build FAILED
  pause
  exit /b 1
)

echo.
echo ==============================================
echo ASan/UBSan builds complete!
echo.
echo Run: raidtest_tests_asan.exe  (full test suite)
echo      asan_checks.exe          (minimal linker check)
echo ==============================================
