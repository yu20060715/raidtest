@echo off
cd /d "%~dp0"

:: Build stress/validation tests only (skip main app and GUI)

set SRC_CORE=src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c
set CFLAGS=-Wall -O2 -Isrc

echo Building stress tests...

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc %CFLAGS% tests/test_longrun.c %SRC_CORE% -o test_longrun.exe -static-libgcc 2>&1"
if %errorlevel% neq 0 ( echo FAILED: test_longrun & pause & exit /b 1 )
echo   test_longrun.exe OK

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc %CFLAGS% tests/test_random_io.c %SRC_CORE% -o test_random_io.exe -static-libgcc 2>&1"
if %errorlevel% neq 0 ( echo FAILED: test_random_io & pause & exit /b 1 )
echo   test_random_io.exe OK

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc %CFLAGS% tests/test_concurrent.c %SRC_CORE% -o test_concurrent.exe -static-libgcc 2>&1"
if %errorlevel% neq 0 ( echo FAILED: test_concurrent & pause & exit /b 1 )
echo   test_concurrent.exe OK

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc %CFLAGS% tests/test_metadata_corrupt.c %SRC_CORE% -o test_metadata_corrupt.exe -static-libgcc 2>&1"
if %errorlevel% neq 0 ( echo FAILED: test_metadata_corrupt & pause & exit /b 1 )
echo   test_metadata_corrupt.exe OK

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc %CFLAGS% stress/test_powerfail.c %SRC_CORE% -o test_powerfail.exe -static-libgcc 2>&1"
if %errorlevel% neq 0 ( echo FAILED: test_powerfail & pause & exit /b 1 )
echo   test_powerfail.exe OK

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd '/c/Users/Yu/Desktop/raidv3' && gcc %CFLAGS% -Ibenchmark benchmark/cli_bench.c %SRC_CORE% -o cli_bench.exe -static-libgcc 2>&1"
if %errorlevel% neq 0 ( echo FAILED: cli_bench & pause & exit /b 1 )
echo   cli_bench.exe OK

echo.
echo All stress tests built successfully!
echo.
echo Run: test_longrun.exe           (10K write/read/verify)
echo      test_random_io.exe          (500 random IO ops)
echo      test_concurrent.exe         (4-thread concurrent access)
echo      test_metadata_corrupt.exe   (4 corruption types)
echo      test_powerfail.exe          (crash + recovery)
echo      cli_bench.exe [csv_path]    (CLI benchmark + CSV output)
