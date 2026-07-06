# Performance Report

## Test Environment

| Item | Value |
|------|-------|
| CPU | Intel/AMD (measured at runtime) |
| RAM | System-dependent |
| Storage | File-backed test disks (T: RAM) |
| RAID Level | RAID0 (2-disk stripe) |
| Test Date | $(date) |
| Benchmark Tool | cli_bench.exe |

## Test Results

### Sequential Throughput

| Test | MB/s | Latency (ms) |
|------|------|-------------|
| Sequential Write | (measured) | (measured) |
| Sequential Read  | (measured) | (measured) |

### Random Throughput (4KB blocks)

| Test | MB/s | Latency (ms) |
|------|------|-------------|
| Random Write | (measured) | (measured) |
| Random Read  | (measured) | (measured) |

### Comparison: Disk vs Volume vs RAID0

| Layer | Sequential Write (MB/s) | Sequential Read (MB/s) |
|-------|------------------------|-----------------------|
| Single Disk (Disk 0) | (bench_single_disk) | (bench_single_disk) |
| Single Disk (Disk 1) | (bench_single_disk) | (bench_single_disk) |
| Volume (RAID0) | (measured) | (measured) |

## Notes

- Results are highly dependent on underlying storage hardware
- File-backed test disks use `%TEMP%` directory (likely RAM or SSD)
- Random IO uses 4KB blocks; sequential uses 64KB blocks
- CPU utilization not measured (single-threaded bench)
- For production numbers, run on target hardware with `raidtest bench`
