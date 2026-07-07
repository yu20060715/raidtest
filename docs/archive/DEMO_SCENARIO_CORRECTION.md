# DEMO SCENARIO CORRECTION

**Date:** 2026-07-08
**Role:** RAIDV3 Demo Scenario Correction Engineer
**Scope:** Correct demonstration documents to use RAID1 instead of RAID0 for fault-tolerance demo

---

## Reason for Correction

### Original
RAID0 + failure recovery

### Problem
Conceptually invalid. RAID0 provides striping only — no redundancy, no degraded mode, no rebuild capability.

| RAID Level | Redundancy | Degraded Mode | Rebuild |
|------------|-----------|---------------|---------|
| RAID0 | None | Not possible | Not possible |
| RAID1 | Mirroring | Supported | Supported |

### Corrected
RAID1 + failure recovery

### Reason
The purpose of the capstone demo is not only showing storage creation, but proving reliability features:
- failure detection
- degraded mode
- recovery
- rebuild

RAID0 is useful for demonstrating striping performance, but it cannot demonstrate fault recovery. Therefore RAID1 is selected for this presentation because the objective is demonstrating reliability and rebuild behavior.

---

## Documents Corrected

| Document | Change |
|----------|--------|
| `docs/release/CAPSTONE_DEMO_PLAN.md` | Segment 3: RAID0 → RAID1/Mirror creation |
| `docs/release/PRESENTATION_SCRIPT.md` | Section 8 Steps 3,5,6,7: RAID0 → RAID1; added RAID1 rationale |
| `docs/release/DEMO_RUN_CHECKLIST.md` | Step 3: RAID Level → RAID1; Pre-demo: RAID1 config |
| `docs/release/FINAL_DEMO_REHEARSAL.md` | Timing plans + Step 3: RAID0 → RAID1 |
| `docs/release/PROFESSOR_QA.md` | Added Q28: RAID1 vs RAID0 choice |
| `docs/release/CAPSTONE_FINAL_STATUS.md` | RAID0/RAID1 contradiction marked resolved |
| `docs/development/WORKFLOW_VALIDATION.md` | Demo path step 3: RAID0 → RAID1 |

---

## Correction Verification

- [x] No C/C++ source code modified
- [x] No new features added
- [x] No architecture refactoring
- [x] No folder reorganization
- [x] All demo documents now describe RAID1 scenario consistently
- [x] No document claims RAID0 supports rebuild
- [x] Build and tests confirmed passing (39/39)

---

## Consistent Demo Flow (Post-Correction)

1. Startup — Launch GUI
2. Disk scan — Click Scan
3. Create RAID1 volume — Select disks, Click Mirror
4. Mount filesystem — Click Mount
5. Write test file — Create capstone.txt on G:\
6. Simulate disk failure — Developer mode → Simulate Fail
7. Show DEGRADED state — Volume Info shows yellow DEGRADED
8. Rebuild failed disk — Actions → Rebuild wizard
9. Verify recovered data — Open capstone.txt, content intact
