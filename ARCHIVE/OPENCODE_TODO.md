# RAIDV3 OpenCode TODO

## Active Tasks

| # | Task | Phase | Priority | Status |
|---|------|-------|----------|--------|
| 1 | Fix cache flush consistency and shutdown ordering | 1 | P0 | Done |
| 2 | Normalize setup/restore/mount workflow | 1 | P1 | Done |
| 3 | Remove dead GUI state or connect cache controls | 2 | P1 | Done |
| 4 | Define clean Beginner/Advanced/Developer mode boundaries | 2 | P2 | Done |
| 5 | Make CLI help and banner text match reality | 2 | P1 | Done |
| 6 | Remove unused API and stale claims | 3 | P2 | Done |
| 7 | Split large modules by responsibility | 3 | P2 | Skipped |
| 8 | Add/strengthen regression coverage for risky paths | 3 | P2 | Done |
| 9 | Produce professor-safe demo flow and release wording | 4 | P3 | Done |

## Deferred Tasks
None.

## Tasks Removed / Skipped
| # | Task | Reason |
|---|------|--------|
| 7 | Split large modules | Codebase reasonably modular; split would be speculative refactor with regression risk before demo |

## Repository Audit
- **Build**: ✅ OK (no errors)
- **Tests**: ✅ 39/39 passed
- **CLI help**: ✅ All commands documented, matches implementation
- **GUI modes**: ✅ Clean Beginner/Advanced/Developer boundaries
- **Dead code**: ✅ 4 unused functions removed, dead GUI state removed
- **Documentation**: ✅ README, DEMO updated to match RC4
- **Unfixed issues**: Superblock tests use hardcoded `C:\RAIDTEST\` (pre-existing, not introduced)
