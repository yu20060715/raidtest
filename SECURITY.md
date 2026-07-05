# RAIDTEST v3 — Security Analysis

## Threat Model

RAIDTEST is a single-user, local-only tool. The threat model assumes:
- The attacker has **local filesystem access** (same OS user or admin)
- The attacker can **modify any file** in the `RAIDTEST\` directories
- The attacker **cannot** modify running process memory (unless they have debug privileges)

Security measures are minimal by design. This document analyzes what protections exist and where they are absent.

---

## T1: Superblock Checksum Bypass

**Threat:** Attacker modifies superblock metadata (disk count, stripe unit, generation) and recalculates CRC32.

**Current protection:** CRC32 is used to detect **accidental** corruption. It is **not** a cryptographic hash. An attacker can:
1. Read the superblock
2. Modify any field
3. Recompute CRC32 (same algorithm is visible in `superblock.c:5-17`)
4. Write back the modified superblock

**Mitigation:** None. The CRC32 source is in the open codebase.

**Severity:** High
**Data risk:** Attacker can cause the volume to mount with wrong parameters → data misinterpretation.

---

## T2: UUID Forgery

**Threat:** Attacker creates a fake superblock with a crafted UUID to masquerade as a legitimate volume.

**Current protection:** UUID is generated from `QueryPerformanceCounter + GetTickCount64 + GetProcessId` — this is **not** cryptographically random. An attacker can predict or reproduce the same UUID.

**Mitigation:** None. UUID uniqueness relies on timing entropy and process ID, which are both observable.

**Severity:** Medium
**Data risk:** Volume identity spoofing — but since the volume's data is on the same physical disks, this does not enable data theft.

---

## T3: Serial Number Spoofing

**Threat:** Attacker attaches a disk whose firmware reports a spoofed serial number matching a legitimate volume's superblock.

**Current protection:** Serial numbers are read from `STORAGE_DEVICE_DESCRIPTOR.SerialNumberOffset` via `IOCTL_STORAGE_QUERY_PROPERTY`. This data is provided by the disk firmware and is **not authenticated**.

**Mitigation:** None. Any USB-SATA adapter with firmware flashing capabilities can spoof serial numbers.

**Severity:** Low
**Data risk:** An attacker could trick RAIDTEST into treating a malicious disk as a legitimate pool member. The data written to it would be corrupted, but the attacker gains no read access to existing data (the original disk still holds the real data).

---

## T4: Journal Injection

**Threat:** Attacker modifies `journal.dat` files to inject fake transactions that replay corrupted data.

**Current protection:** Journal entries have a CRC32 checksum over the entry header. However:
- The data payload is **not** checksummed (BUG B8)
- An attacker who can read a valid entry can forge new entries with valid headers
- Recovery blindly replays all uncommitted entries

**Mitigation:** None.

**Severity:** High
**Data risk:** Arbitrary data can be injected into the volume during journal replay.

---

## T5: Rollback Interruption

**Threat:** The system loses power during `cmd_expand` rollback (deleting newly created pool files). Power is restored during deletion.

**Current protection:** The rollback loop in `cmd_handler.c:323-328` calls `pool_file_close` then `pool_file_delete` sequentially. If power loss occurs mid-loop, some new pool files are deleted and some are not.

**Mitigation:** No atomicity. The volume state after crash-during-rollback is inconsistent.

**Severity:** Medium
**Data risk:** Orphaned pool files may remain, consuming disk space. The superblock still reflects the pre-expand state, so data is safe but space is leaked.

---

## T6: Cache Flush Interruption

**Threat:** Power loss while cache flush thread is writing dirty blocks back to disk.

**Current protection:** The cache flush writes blocks one at a time. If power is lost mid-flush:
- Journal may capture the most recent writes but not necessarily the blocks being flushed
- The journal covers virtual-offset writes, not physical pool writes

**Mitigation:** `FlushFileBuffers` is called per disk after each write in non-cached mode, but during cache flush the `WriteFile` to pool is synchronous (overlapped with event wait). If power fails during the `WriteFile` call, the block is partially written.

**Severity:** High
**Data risk:** Partial block write → corrupted data at that virtual offset.

---

## T7: No Encryption

**Threat:** Physical disk theft allows reading all pool data directly from the disk files.

**Current protection:** None. Pool files are plain binary data. Anyone with filesystem access can read or copy `stripe_pool.dat` and reconstruct the stripe/mirror layout using the superblock metadata.

**Mitigation:** None. Encryption is not implemented.

**Severity:** High
**Data risk:** Complete data exposure on disk theft.

---

## T8: Process Memory Exposure

**Threat:** Another process with `PROCESS_VM_READ` access reads RAIDTEST process memory, extracting cached data.

**Current protection:** None. The write-back cache buffer (`RAM_CACHE.buffer`) is allocated with `VirtualAlloc` with `PAGE_READWRITE` — readable by any process with debug privilege.

**Mitigation:** None. No memory encryption or secure zeroing.

**Severity:** Medium
**Data risk:** Cached plaintext data exposed to local administrators.

---

## Security Recommendations (not urgent, deferred)

1. Replace CRC32 with **SHA-256** or **HMAC** for superblock integrity
2. Encrypt superblock metadata (symmetric key derived from a user passphrase)
3. Sign journal entries with a per-volume secret
4. Zero buffer on `cache_destroy` and `stripe_volume_destroy`
5. Use `VirtualAlloc` with `PAGE_NOACCESS` after free (Windows `VirtualFree` + guard pages)
6. Detect physical disk removal via periodic polling
