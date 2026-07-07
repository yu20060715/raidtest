# RAIDTEST v1.0 RC4 — Presentation Risk Review

**Date:** 2026-07-07  
**Auditor:** RAIDV3 Capstone Demo Rehearsal Auditor  
**Scope:** Script clarity, concept accuracy, innovation communication, limitation honesty, bug acceptability

---

## 1. Innovation Communicability

### Asymmetric Stripe Algorithm

| Check | Assessment |
|-------|-----------|
| **Is the innovation clearly stated?** | ⚠️ PARTIAL — Section 5 of PRESENTATION_SCRIPT.md explains asymmetric stripe, but the speaker script jumps from "problem" directly to "algorithm flow" without first stating **why this is innovative** or **what problem it solves that existing RAID0 cannot**. |
| **Are the terms defined?** | ✅ YES — `asymmetric`, `phase mapping`, `GCD ratio`, `cycle` are all defined. |
| **Is a concrete example given?** | ✅ YES — NVMe 2800 MB/s + SATA SSD 500 MB/s = 28:5 ratio. 85%/15% I/O split. Good example. |
| **Can a non-expert understand?** | ⚠️ PARTIAL — The script uses "GCD-based proportional phase mapping" without a simple analogy. A **delivery truck analogy** (faster truck makes more trips per hour) would help. |
| **Risk Level** | **MEDIUM** — Innovation is real but may be lost without a headline summary. |

**Recommendation:** Add one sentence at the start of Section 5:  
> "Traditional RAID0 wastes fast disk capacity because all disks must wait for the slowest one. Our algorithm solves this by letting each disk work at its own speed."

### WinFsp FUSE Integration

| Check | Assessment |
|-------|-----------|
| **Is the achievement clear?** | ✅ YES — Section 7 explains that any Windows app can use the RAID volume without special drivers. |
| **Is the FUSE callback model explained?** | ✅ YES — 13 callbacks listed, data flow diagram described. |
| **Is the choice of userspace justified?** | ✅ YES — PROFESSOR_QA.md Q16 and script Section 7 explicitly address kernel vs userspace tradeoff. |
| **Risk Level** | **LOW** — Well explained with clear rationale. |

---

## 2. RAID Concept Accuracy

| Concept | Script Claims | Actual Behavior | Match? |
|---------|--------------|----------------|--------|
| **RAID0 striping** | "stripe across multiple disks for performance" | `stripe_engine.c` → phased LBA mapping with per-disk I/O split | ✅ CORRECT |
| **RAID1 mirroring** | "write same data to all disks" | `mirror_engine.c` → `mirror_write_to_all()` writes to all healthy disks | ✅ CORRECT |
| **Degraded mode** | "RAID1 continues with fewer disks" | `mirror_engine_read()` skips faulty disk, `mirror_write_to_all()` skips faulty disk | ✅ CORRECT |
| **Rebuild** | "copy from healthy to replacement" | `mirror_volume_rebuild()` — 64MB chunks, source selection, FlushFileBuffers | ✅ CORRECT |
| **RAID0 no fault tolerance** | "any disk failure = data loss" | `stripe_io_ok()` marks faulty, volume becomes inaccessible | ✅ CORRECT |
| **Asymmetric vs traditional** | "traditional requires equal disks" | True: traditional RAID0 expects same-size/same-speed disks | ✅ CORRECT |
| **Write-back cache** | "immediate return, async flush" | `ram_cache_write()` → dirty bitmap → background flush thread | ✅ CORRECT |
| **Write-ahead journal** | "BEGIN→DATA→COMMIT, replay on crash" | `journal.c` — three entry types, CRC32, FlushFileBuffers, recovery scan | ✅ CORRECT |

**Risk Level: LOW** — All RAID concepts in the script match actual behavior. No factual errors found.

---

## 3. Limitation Honesty

| Limitation Category | Documented In | Script Section | Honest? |
|--------------------|--------------|----------------|---------|
| Windows-only | KNOWN_LIMITATIONS.md | Section 10 (Limitations) | ✅ YES — explicitly stated |
| No RAID5/6 | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| No RAID10 | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| Cache dirty data loss on power fail | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| Journal unbounded growth | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| Flat 64-entry file table | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| g_state unlocked (T1/P0) | MASTER_BACKLOG.md | Section 10 | ✅ YES — P0 issue disclosed |
| OVERLAPPED stack use (B1/P0) | MASTER_BACKLOG.md | Section 10 | ✅ YES — disclosed |
| Stack buffer overflows (B2,B3/P0) | MASTER_BACKLOG.md | Section 10 | ✅ YES — disclosed |
| Requires Administrator | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| Requires WinFsp | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| Max 4 disks | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| No encryption | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |
| No end-to-end checksum | KNOWN_LIMITATIONS.md | Section 10 | ✅ YES |

**Risk Level: LOW** — Limitations are comprehensively documented and the script includes a dedicated "Limitations (Honest)" section (Section 10). The speaker acknowledges bugs openly, which builds credibility.

---

## 4. Known Bug Acceptability for Demo

| Bug | Severity | Demo Trigger Likelihood | Acceptable? |
|-----|----------|------------------------|-------------|
| **B1**: OVERLAPPED on stack | P0 | **VERY LOW** — current code uses blocking wait; only triggered if async completes after frame exits | ✅ YES — latent only |
| **B2**: parent_dir_exists buffer | P0 | **VERY LOW** — requires crafted path >256 chars | ✅ YES — normal demo paths are <50 chars |
| **B3**: raid_rename concat | P0 | **VERY LOW** — requires rename with >254 char suffix | ✅ YES — won't rename during demo |
| **B4**: file_table_lock_init race | P1 | **LOW** — function is no-op; CS init is single-threaded | ✅ YES — benign in practice |
| **B5**: DeleteCS while callbacks active | P1 | **LOW** — only triggered on rapid unmount while I/O in flight | ✅ YES — avoid rapid unmount |
| **B6**: journal no lock | P1 | **MEDIUM** — concurrent flush+FUSE write could corrupt | ⚠️ LOW probability in single-user demo |
| **B7**: event bus CS leak | P1 | **ZERO** — leak only; no functional impact | ✅ YES — cosmetic |
| **B8**: device_get NULL | P1 | **MEDIUM** — fixed: all 8 call sites have NULL checks | ✅ FIXED — no longer a demo risk |
| **B9**: vol->disks[i] NULL | P1 | **MEDIUM** — during failure→rebuild transition | ⚠️ ACCEPTABLE — if triggered, shows error handling |
| **T1**: g_state unlocked | P0 | **LOW** — unlikely to manifest in single-user scenario | ✅ YES — pre-existing, documented |
| **B14**: deprecated GetVersion | P2 | **ZERO** — cosmetic only, no functional impact | ✅ YES — trivial |

**Risk Level: VERY LOW** — No P0/P1 bug is likely to manifest during a normal demo. The two medium-probability items (B6, B9) have acceptable impact.

---

## 5. Presentation Script Issues Found

| # | Issue | Location | Severity | Recommendation |
|---|-------|----------|----------|---------------|
| R1 | Section 5: Innovation not introduced with a headline | PRESENTATION_SCRIPT.md:81-99 | LOW | Add one-sentence summary: "Traditional RAID0 is limited by the slowest disk. Our algorithm fixes this." |
| R2 | Section 5: No analogy for GCD ratio | PRESENTATION_SCRIPT.md:93-97 | LOW | Add delivery truck analogy for clarity |
| R3 | Section 10: Limitations mention P0 bugs but don't tie them to specific B-IDs | PRESENTATION_SCRIPT.md:224-226 | LOW | Map "OVERLAPPED" and "stack overflow" to B1/B2/B3 for professor credibility |
| R4 | Section 8: Steps 6-7 describe failure+rebuild for RAID1, but demo uses RAID0 | PRESENTATION_SCRIPT.md:174-182 | ⚠️ **MEDIUM** | RAID0 cannot degrade or rebuild. The script describes a RAID1 flow. If demo is done with RAID0, failure simulation will crash the volume. **Must use RAID1 (Mirror) for failure+rebuild demo.** |
| R5 | Section 8: Step 8 says "Rebuild wizard" but doesn't specify RAID1 prerequisite | PRESENTATION_SCRIPT.md:179 | LOW | Clarify: "For RAID1 volumes only" |

**⚠️ CRITICAL FINDING (R4):** The PRESENTATION_SCRIPT.md Section 8 demo flow (Steps 6-8) describes failure simulation, degraded mode, and rebuild — but these are **RAID1-only features**. If the demo creates a RAID0 volume (as described in Step 3 and CAPSTONE_DEMO_PLAN.md Segment 3), then:
- Step 6: Simulating a disk failure on RAID0 will make the volume **inaccessible**
- Step 7: There is NO degraded state for RAID0 — the volume is dead
- Step 8: Rebuild is impossible on RAID0

**This is a script contradiction.** The demo plan says "Create RAID0" but the failure/rebuild steps only work with RAID1.

---

## 6. CAPSTONE_DEMO_PLAN.md Contradictions

| Segment | Action | Issue |
|---------|--------|-------|
| Segment 3 | Create RAID0 Volume | ✅ Fine for create/mount/write |
| Segment 6 | Unmount + Restore | ✅ Works with both RAID0 and RAID1 |
| Segment 9 | Destroy | ✅ Works with both |
| **Segments 6-8** of PRESENTATION_SCRIPT | Simulate failure + Degraded + Rebuild | ❌ **Only works with RAID1 (Mirror)** |

**Resolution:** The demo plan should either:
- **Option A:** Change Segment 3 to create RAID1 (Mirror) instead of RAID0
- **Option B:** Change the failure/rebuild steps to show that RAID0 has no fault tolerance and briefly switch to a pre-made RAID1 volume for the rebuild demo

**Recommended: Option A** — Create RAID1 for the demo. This shows both RAID levels:
- Create RAID1 (shows mirroring)
- File write through mirror engine
- Failure simulation shows degraded mode
- Rebuild shows recovery
- This covers more concepts

---

## 7. Risk Summary

| Risk Area | Rating | Key Concern |
|-----------|--------|-------------|
| Innovation Clarity | ⚠️ MEDIUM | No headline statement for asymmetric stripe |
| RAID Concept Accuracy | ✅ LOW | All concepts match actual behavior |
| Limitation Honesty | ✅ LOW | Comprehensive, transparent disclosure |
| Known Bug Acceptability | ✅ VERY LOW | No bug likely to trigger in demo |
| Script Consistency | ⚠️ **MEDIUM** | **RAID0 vs RAID1 contradiction in failure/rebuild flow** |
| Demo Plan Consistency | ⚠️ **MEDIUM** | CAPSTONE_DEMO_PLAN.md says RAID0, PRESENTATION_SCRIPT.md says RAID1 |

**Priority Fix:** Resolve the RAID0 vs RAID1 contradiction before presenting. The demo must either:
1. Create a RAID0 volume and skip failure/rebuild (focus on performance + asymmetric stripe)
2. Create a RAID1 volume and include failure/rebuild (focus on reliability + recovery)

**Recommendation:** Present two RAID levels:
- Quick RAID0 create → show speed, then destroy
- Create RAID1 → show failure + rebuild → verify data
