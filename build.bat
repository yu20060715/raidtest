@echo off
cd /d "%~dp0"
set "SCRIPT_DIR=%CD%"

:: Ensure build directory exists
if not exist build\* ( mkdir build )

:: Detect toolchain: prefer clang-cl/MSVC if available, fallback to MinGW GCC
where clang-cl >nul 2>nul
if not errorlevel 1 (
  echo Building with clang-cl...
  clang-cl -nologo -O2 -DUSE_WINFSP -Iwinfsp_headers\winfsp-2.1\inc -Iwinfsp_headers\winfsp-2.1\inc\fuse -Isrc -Iimgui src\main.c src\cmd_handler.c src\config.c src\disk_scanner.c src\pool_io.c src\bench_io.c src\stripe_engine.c src\mirror_engine.c src\ram_cache.c src\fuse_bridge.c src\wizard.c src\daemon.c src\cleanup.c src\gui.cpp src\superblock.c src\journal.c src\logger.c src\event_bus.c src\device_manager.c src\metadata_manager.c src\planner_engine.c src\volume_manager.c src\raid_service.c src\ui_model.c imgui\imgui.cpp imgui\imgui_draw.cpp imgui\imgui_tables.cpp imgui\imgui_widgets.cpp imgui\backends\imgui_impl_win32.cpp imgui\backends\imgui_impl_dx11.cpp -Fe:raidtest_winfsp.exe winfsp-x64.lib ole32.lib shell32.lib shlwapi.lib ws2_32.lib advapi32.lib ntdll.lib comctl32.lib d3d11.lib dxgi.lib d3dcompiler.lib gdi32.lib dwmapi.lib /link /LIBPATH:"C:\msys64\mingw64\lib" /LTCG 2>&1
  if errorlevel 1 ( echo Build FAILED & pause & exit /b 1 )
  echo Build OK
  clang-cl -nologo -O2 -Isrc src\test_runner.c src\test_common.c src\test_stripe.c src\test_mirror.c src\test_cache.c src\test_journal.c src\test_superblock.c src\stripe_engine.c src\mirror_engine.c src\ram_cache.c src\logger.c src\journal.c src\pool_io.c src\superblock.c -Fe:raidtest_tests.exe 2>&1
  if errorlevel 1 ( echo Test build FAILED & pause & exit /b 1 )
  echo Test build OK
  exit /b 0
)

:: Fallback: MinGW GCC (default)
:: Step 1: compile all C files with gcc
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/main.c -o build/main.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/cmd_handler.c -o build/cmd_handler.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/config.c -o build/config.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/disk_scanner.c -o build/disk_scanner.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/pool_io.c -o build/pool_io.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/bench_io.c -o build/bench_io.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/stripe_engine.c -o build/stripe_engine.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/mirror_engine.c -o build/mirror_engine.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/ram_cache.c -o build/ram_cache.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/fuse_bridge.c -o build/fuse_bridge.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/wizard.c -o build/wizard.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/daemon.c -o build/daemon.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/profiler.c -o build/profiler.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/cleanup.c -o build/cleanup.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/superblock.c -o build/superblock.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/journal.c -o build/journal.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/logger.c -o build/logger.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/event_bus.c -o build/event_bus.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/device_manager.c -o build/device_manager.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/metadata_manager.c -o build/metadata_manager.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/planner_engine.c -o build/planner_engine.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/volume_manager.c -o build/volume_manager.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/raid_service.c -o build/raid_service.o 2>&1 && gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c src/ui_model.c -o build/ui_model.o 2>&1"
if errorlevel 1 (
  echo C build FAILED
  pause
  exit /b 1
)
echo C objects OK

:: Step 2: compile C++ files (gui + imgui) with g++
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c src/gui.cpp -o build/gui.o 2>&1 && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c imgui/imgui.cpp -o build/imgui.o 2>&1 && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c imgui/imgui_draw.cpp -o build/imgui_draw.o 2>&1 && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c imgui/imgui_tables.cpp -o build/imgui_tables.o 2>&1 && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c imgui/imgui_widgets.cpp -o build/imgui_widgets.o 2>&1 && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c imgui/backends/imgui_impl_win32.cpp -o build/imgui_impl_win32.o 2>&1 && g++ -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -Iimgui -c imgui/backends/imgui_impl_dx11.cpp -o build/imgui_impl_dx11.o 2>&1"
if errorlevel 1 (
  echo C++ build FAILED
  pause
  exit /b 1
)
echo C++ objects OK

:: Step 3: link with g++
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && g++ -Wall -O2 build/*.o -o raidtest_winfsp.exe -L. -lwinfsp-x64 -lole32 -lshell32 -lshlwapi -lws2_32 -ladvapi32 -lntdll -lcomctl32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -ld3dcompiler -static-libgcc -static-libstdc++ 2>&1"
if errorlevel 1 (
  echo Link FAILED
  pause
  exit /b 1
)
echo Build OK

:: Step 4: build tests with gcc (unchanged)
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc src/test_runner.c src/test_common.c src/test_stripe.c src/test_mirror.c src/test_cache.c src/test_journal.c src/test_superblock.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/profiler.c -o raidtest_tests.exe -static-libgcc 2>&1"
if errorlevel 1 (
  echo Test build FAILED
  pause
  exit /b 1
)
echo Test build OK

:: Step 5: build stress/validation tests with gcc
echo Building stress tests...
C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc tests/test_longrun.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c src/profiler.c -o test_longrun.exe -static-libgcc 2>&1"
if errorlevel 1 ( echo test_longrun build FAILED & pause & exit /b 1 )

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc tests/test_random_io.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c src/profiler.c -o test_random_io.exe -static-libgcc 2>&1"
if errorlevel 1 ( echo test_random_io build FAILED & pause & exit /b 1 )

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc tests/test_concurrent.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c src/profiler.c -o test_concurrent.exe -static-libgcc 2>&1"
if errorlevel 1 ( echo test_concurrent build FAILED & pause & exit /b 1 )

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc tests/test_metadata_corrupt.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c src/profiler.c -o test_metadata_corrupt.exe -static-libgcc 2>&1"
if errorlevel 1 ( echo test_metadata_corrupt build FAILED & pause & exit /b 1 )

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc stress/test_powerfail.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c src/profiler.c -o test_powerfail.exe -static-libgcc 2>&1"
if errorlevel 1 ( echo test_powerfail build FAILED & pause & exit /b 1 )

C:\msys64\usr\bin\bash.exe --login -c "export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'; cd "$(cygpath -u "${SCRIPT_DIR}")" && gcc -Wall -O2 -Isrc -Ibenchmark benchmark/cli_bench.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/logger.c src/journal.c src/pool_io.c src/superblock.c src/test_common.c src/profiler.c -o cli_bench.exe -static-libgcc 2>&1"
if errorlevel 1 ( echo cli_bench build FAILED & pause & exit /b 1 )

echo All stress tests OK
