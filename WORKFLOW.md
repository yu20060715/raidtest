# RAIDTEST v3 — Workflow

## Quick Start

```
raidtest> scan              # enumerate physical SSDs
raidtest> select 0 1        # pick disks (by index from scan)
raidtest> init 0:102400 1:51200   # create pool files (id:sizeMB)
raidtest> create            # build RAID0 volume
raidtest> mount G           # expose as G:\ via WinFsp
raidtest> test              # verify I/O integrity
```

One-shot: `raidtest> quick` — runs scan→select→init→create→mount→test.

## Normal Operation

### 1. Hardware Setup
- Insert physical SSDs into the system
- Ensure each has a unique drive letter

### 2. Scan & Select
```
scan                      → lists all physical disks with model, serial, size
select <id1> <id2> ...    → marks disks for the volume
```

### 3. Initialize & Create
```
init 0:102400 1:51200     → creates pool files (id:sizeMB)
create                    → builds RAID0 stripe volume
mirror                    → builds RAID1 mirror volume
```

### 4. Cache & Mount
```
cache 2048                → enables 2 GB write-back cache
mount G                   → exposes volume as G:\ via WinFsp
```

### 5. I/O Verification
```
test                      → writes pattern, reads back, compares
random 1000 64            → stress test: 1000 random ops, 64 KB max
benchfs 1024 128          → synthetic benchmark
```

## Recovery Workflow

### After Reboot / Reconnection
```
scan                      → re-discover physical disks
load                      → restore from superblock (serial-based matching)
check                     → verify all disks and superblocks are healthy
mount                     → re-mount if healthy
```

### Disk Failure (Mirror Only)
```
check                     → shows which disk failed
simulate 0 f              → mark disk 0 as failed (for testing)
rebuild 0 3 102400        → replace failed disk 0 with disk 3, 100 GB pool
check                     → verify rebuild completed
```

### Planned Takedown
```
config-save               → save current volume config to JSON
unmount                   → unmount WinFsp, keep pool files
```

### Restoration After Takedown
```
load                      → re-load from superblock (serial-based disk matching)
check                     → verify all superblocks match
mount                     → re-mount if healthy
```

### Config Persistence
```
config-save               → save disks/size/cache/volume config as JSON
config-load               → reload saved config (re-applies init + cache size)
```

## Maintenance

### View Metadata
```
metadata C                → dump superblock contents (UUID, generation, disks)
```

### Capacity Planning
```
planner                   → shows available capacity for RAID0/RAID1/RAID10
```

### Destroy vs Purge
```
destroy                   → clean shutdown: unmount + journal commit + delete pool files
purge                     → force cleanup: delete pool files & superblock (no journal commit)
```

`destroy` is the safe removal path — it ensures journal is committed before
deleting pool data. `purge` is a hard reset (e.g. after a failed create) that
clears pool files and superblocks but skips journal commit.

### Event Log
```
events                    → display event history
```

## CLI Reference

| Command | Args | State Required | Description |
|---------|------|----------------|-------------|
| `scan` | — | DISCONNECTED | Enumerate physical disks → DISCOVERED |
| `select` | `<id...>` | DISCOVERED | Mark disks for volume membership |
| `init` | `[id:mb...]` | DISCOVERED | Create pool files → INITIALIZED |
| `create` | — | INITIALIZED | Build RAID0 volume → MOUNTED |
| `mirror` | — | MOUNTED | Build RAID1 volume (keeps MOUNTED) |
| `mount` | `[letter]` | MOUNTED | Mount via WinFsp at specified drive |
| `unmount` | — | MOUNTED | Unmount, preserve pool files → UNMOUNTED |
| `load` | `[letter]` | UNMOUNTED/DISCOVERED | Restore volume from superblock → MOUNTED |
| `destroy` | — | MOUNTED/UNMOUNTED | Full deletion (unmount + pool files) → DISCOVERED |
| `purge` | — | INIT/MOUNTED/UNMOUNTED | Force cleanup, skip journal commit → DISCOVERED |
| `check` | — | MOUNTED/DEGRADED | Health check (superblock + pool files) |
| `metadata` | `[letter]` | ANY | Dump raw superblock from a drive letter |
| `planner` | — | ANY | Capacity analysis (RAID0/1/10) |
| `simulate` | `<idx> <f/h/d>` | MOUNTED | Fault injection (fail/healthy/disconnect) |
| `events` | — | ANY | Display event log |
| `cache` | `<sizeMB/wt/off>` | MOUNTED | Cache control (size/on/off) |
| `rebuild` | `<idx> <disk> [MB]` | MOUNTED/DEGRADED | Replace failed mirror member |
| `expand` | `<id:mb...>` | INITIALIZED | Add new disk to RAID0 (re-stripe) |
| `status` | — | MOUNTED/DEGRADED | Live monitoring dashboard |
| `info` | — | ANY | Volume configuration details |
| `map` | — | MOUNTED | Dump stripe-to-disk mapping |
| `test` | — | MOUNTED | Sequential I/O verification |
| `random` | `[ops] [maxKB]` | MOUNTED | Random I/O stress test |
| `benchfs` | `[MB] [KB]` | MOUNTED | Synthetic filesystem benchmark |
| `config-save` | — | ANY | Save current config as JSON |
| `config-load` | — | ANY | Load config from JSON |
| `quick` | — | ANY | All-in-one: scan→select→init→create→mount→test |
| `wizard` | — | ANY | Interactive guided setup |

> **Note**: `→` indicates state transitions. Commands that don't list a transition keep the current state.
