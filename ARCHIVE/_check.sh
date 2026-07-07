#!/bin/bash
export PATH='/mingw64/bin:/usr/bin:/c/Windows/System32'
cd "C:/Users/Yu/Desktop/raidv3"
rm -f build/*.o
for f in src/cleanup.c src/superblock.c src/journal.c src/metadata_manager.c src/volume_manager.c src/test_superblock.c src/test_journal.c; do
  base=$(basename "$f" .c)
  echo "  CC $base"
  gcc -Wall -O2 -DUSE_WINFSP -Iwinfsp_headers/winfsp-2.1/inc -Iwinfsp_headers/winfsp-2.1/inc/fuse -Isrc -c "$f" -o "build/$base.o" 2>&1
  if [ $? -ne 0 ]; then echo "FAILED: $f"; exit 1; fi
done
echo "All passed"
