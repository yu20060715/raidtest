# RAIDTEST v1.0 RC4 — Known Limitations

## Platform

- **Windows only**. The entire codebase uses Win32 API (`CreateFile`, `OVERLAPPED`, `IOCTL_STORAGE_QUERY`). No POSIX compatibility layer. Requires Windows 7+.
- **No Linux/macOS support**. Zero portability in current code.
- **x64 only** in build scripts. Build.bat targets amd64; no ARM64 or x86 build tested.

## WinFsp

- **Mount is not a block device**. WinFsp exposes a filesystem, not a raw block device. Tools like CrystalDiskMark that expect `\\.\PhysicalDriveN` or `\\.\X:` may not detect the volume.
- **WinFsp must be installed separately**. The project links `winfsp-x64.lib` but does not bundle the redistributable.
- **Mount performance is limited** by FUSE translation overhead (user-mode → kernel → user-mode → disk).
- **No TRIM/discard passthrough**. The mounted filesystem cannot forward trim commands to underlying SSDs.

## RAID Levels

- **No RAID5/6**. Only RAID0 (stripe) and RAID1 (mirror) are implemented.
- **No RAID10**. Mirror-of-stripes requires manual setup of two mirror volumes.
- **No nested RAID**. Single level only.
- **No sparse RAID**. Pool files are fully allocated on creation (no thin provisioning).
- **Max 4 disks per volume**. `MAX_DISKS = 4` is a compile-time constant.

## Hot Spare

- **No automatic hot spare detection**. Replacement disk must be specified manually.
- **No automatic rebuild**. `rebuild` command requires explicit index selection.
- **No predictive failure analysis**. S.M.A.R.T. is not read.

## Journal

- **Prototype quality**. Journal is a simple file-based write-ahead log without circular buffering.
- **No journal trimming**. Committed entries are never removed; the journal file grows unbounded.
- **No journal checksums** on data blocks. Only the journal entry header has CRC32; the data payload is not checksummed.
- **Recovery is raw replay**. `journal_recover_all` re-applies incomplete transactions but does not validate data integrity before replay.
- **One journal per disk**. Each pool file has its own `journal.dat`; there is no distributed consensus.

## Cache

- **No LRU eviction**. Cache is a flat array indexed by block number; no eviction strategy beyond flushing.
- **No write-back throttle**. Under sustained heavy write load, the flush thread can fall behind, causing unbounded dirty block accumulation.
- **Write-through mode is global**. Cannot set per-file or per-directory write policy.
- **Cache size is fixed at init time**. No dynamic resizing.
- **Flush-on-unmount is synchronous**. `cleanup_cache` drains the flush thread with `WaitForSingleObject(INFINITE)`, which can block indefinitely if I/O hangs.

## Performance

- **No I/O scheduler**. Requests are dispatched immediately without merging or reordering.
- **No NCQ passthrough**. The engine does not use native command queuing even when the underlying hardware supports it.
- **No direct I/O** (except `FILE_FLAG_NO_BUFFERING` on pool files). The OS cache adds an extra copy.
- **No CPU affinity control**. Benchmark results vary with scheduler decisions.
- **Single flush thread**. All dirty blocks queue through one thread, creating a bottleneck on multi-disk configurations.

## Data Integrity

- **No end-to-end checksum**. Data blocks are not checksummed on read/write; silent corruption is not detected.
- **No scrubbing**. There is no background process to verify data consistency.
- **Mirror read does not verify**. `mirror_volume_read` reads from the healthiest disk without comparing against a second copy.
- **No write verification**. Writes are not read back to confirm.
- **Destroy has no rollback**. If `pool_file_delete` fails on disk 2 of 4, disks 0-1 are already deleted and cannot be recovered.

## CLI

- **No tab completion**. Command names must be typed in full.
- **No command history**. Previous commands cannot be recalled with arrow keys.
- **No argument validation**. `atoi` is used for integer parsing; invalid strings silently return 0.
- **No batch mode**. `cmd_process_auto` supports semicolons but there is no script file execution.
- **Chinese help text**. `cmd_help()` prints descriptions in Traditional Chinese; non-Chinese users may be confused.

## Tests

- **Real filesystem dependency**. All superblock tests use `C:\RAIDTEST\` real pool files. They will fail on systems without write access to `C:\`.
- **No mocking infrastructure**. There is no injected I/O layer; tests are integration-level by necessity.
- **Test order dependency**. Tests share the `C:\RAIDTEST\` directory; parallel execution would corrupt state.
- **No test isolation**. Cleaning up between tests relies on `sb_cleanup()` deleting files — if a test crashes, files leak.

## Monitoring

- **No S.M.A.R.T.** Disk health attributes are not read.
- **No temperature monitoring**. SSD/NVMe temperature not available.
- **No I/O latency histograms**. Only aggregate throughput is measured.
- **No alerting**. There is no mechanism to notify the user of failures outside the CLI.

## Security

- **No encryption**. Data on disk is stored in plaintext.
- **No access control**. Any process on the system can read/write the pool files.
- **No key management**. There is no keyring, no password prompt, no secure erase.
- **Superblock checksum prevents accidental corruption but not targeted tampering**. An attacker with filesystem access can recompute the CRC32 after modifying metadata.
- **Serial number is read-only metadata**. It can be spoofed by a malicious disk's firmware.

## Recovery

- **Unmount is stateful**. `cmd_unmount` sets state to UNMOUNTED but does not persist this. After a crash, the system boots in DISCONNECTED and must scan/load manually.
- **No boot-time auto-load**. There is no service that automatically restores the volume on startup.
- **Load requires physical_disks to be populated**. If `scan` was not called first, serial matching has zero physical entries to match against, falling back to SB drive_letter.
