#!/bin/bash
export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'
cd "C:/Users/Yu/Desktop/raidv3"

set -e
echo "Compiling C sources..."
for f in src/main.c src/cmd_handler.c src/config.c src/disk_scanner.c src/pool_io.c src/bench_io.c src/stripe_engine.c src/mirror_engine.c src/ram_cache.c src/fuse_bridge.c src/wizard.c src/daemon.c src/cleanup.c src/superblock.c src/journal.c src/logger.c src/event_bus.c src/device_manager.c src/metadata_manager.c src/planner_engine.c src/volume_manager.c src/storage_common.c src/raid_service.c src/ui_model.c src/profiler.c; do
  base=$(basename "$f" .c)
  echo "  CC $base"
  gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c "$f" -o "build/$base.o"
done
echo "All C objects OK"

echo "Linking..."
g++ -Wall -O2 build/*.o -o raidtest_winfsp.exe -L. -lwinfsp-x64 -lole32 -lshell32 -lshlwapi -lws2_32 -ladvapi32 -lntdll -lcomctl32 -lgdi32 -ldwmapi -ld3d11 -ldxgi -ld3dcompiler -static-libgcc -static-libstdc++
echo "Build OK"
