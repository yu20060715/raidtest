# RAIDTEST v3 — Metadata System

## Overview

RAIDTEST uses a distributed metadata system. Every physical disk in the volume
stores a copy of the superblock in its `RAIDTEST\superblock.dat` file. On load,
all copies are scanned and the best (highest generation) is selected.

## Superblock Format (v4)

- **Magic**: `0x52414944` (`"RAID"`)
- **Version**: 4 (current)
- **Backward compatible** readers for v1, v2, v3

### Disk Entry (`SB_DISK_ENTRY`)

| Field | Size | Description |
|-------|------|-------------|
| `drive_letter` | 8 bytes | Wide-char root path (e.g. `C:\`) |
| `bench_write_mbs` | 4 bytes | Write speed |
| `bench_read_mbs` | 4 bytes | Read speed |
| `pool_bytes` | 8 bytes | Pool file size |
| `serial_number` | 64 bytes | Physical disk serial number |

### Volume Header (`SUPERBLOCK`)

| Field | Size | Description |
|-------|------|-------------|
| `magic` | 4 B | `0x52414944` |
| `version` | 4 B | `4` |
| `disk_count` | 4 B | Number of disks |
| `stripe_unit` | 4 B | Stripe unit in bytes |
| `virtual_total_bytes` | 8 B | Total virtual capacity |
| `phase_count` | 4 B | Number of mapping phases |
| `generation` | 8 B | Monotonically increasing write counter |
| `timestamp` | 8 B | Last write time (FILETIME) |
| `feature_flags` | 4 B | Bitfield: journal, cache, mirror |
| `journal_offset` | 8 B | Journal byte offset |
| **`volume_uuid`** | **16 B** | **128-bit volume UUID (new in v4)** |
| **`created_time`** | **8 B** | **Volume creation FILETIME (new in v4)** |
| **`last_mount_time`** | **8 B** | **Last mount FILETIME (new in v4)** |
| `disks[4]` | 352 B | Disk entries (88 B × 4) |
| `phases[4]` | 384 B | Mapping phases (96 B × 4) |
| `checksum` | 4 B | CRC32 checksum of all prior fields |

Total: **828 bytes**

## UUID Generation

- Type: 128-bit struct (`{uint64_t high, uint64_t low}`)
- Algorithm: `QueryPerformanceCounter × GetTickCount64 × GetProcessId`
- Generated once on `create` or `mirror`
- Persisted in v4 superblock

## Serial-Based Restore

On `load`:
1. Scan all fixed drives for superblocks
2. Pick best by generation (latest wins)
3. For each SB disk entry, match `serial_number` against physical disk list
4. If serial matches → use physical disk's drive letter
5. If serial missing/does not match → fall back to SB `drive_letter`
6. Open pool files at resolved locations

### Upgrade Path

| From | Reader Action |
|------|---------------|
| v1 | Upconverts to v2 (zeroes new fields), logs warning |
| v2 | Upconverts to v3 (blank serial), no warning |
| v3 | Upconverts to v4 (zero UUID), silent |
| v4 | Read directly |

All readers validate CRC32 checksum before accepting data.

## Checksum

CRC32 covers all fields before `checksum` (offset 0 to
`offsetof(SUPERBLOCK, checksum)`). If checksum fails, the superblock
is rejected and the next candidate is tried.

## Event Log

Event log is stored as a plain-text file (`events.log`) in `%APPDATA%\RAIDTEST\`.
Entries are timestamped in local time: `[YYYY-MM-DD HH:MM:SS] <event>`.

Currently logged events:
- Destroy (via `destroy` command)
- Simulated disk failures/recovery (via `simulate f/h/d`)

> **Note**: Create, mount, unmount, rebuild, expand, and config changes do
> NOT currently log events. This is a planned enhancement.

Trim threshold: file >512 KB → truncates to last 256 KB (keeps tail).
