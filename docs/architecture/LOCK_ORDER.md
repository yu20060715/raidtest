# RAIDTEST v3 — Lock Acquisition Order

This document defines the mandatory acquisition order for all synchronization
primitives in the RAIDTEST codebase. Violating this order will cause deadlocks.

## Global Order (highest → lowest)

```
g_state_cs           → cache->lock        → g_journal_cs      → g_eb_cs
                                                                     ↓
                                                              g_file_table_lock
                                                                     ↓
                                                              g_log_lock
                                                                     ↓
                                                              g_gui.log_lock
```

## Lock Reference

| Lock | Symbol | Defined In | Initialized In | Description |
|------|--------|------------|----------------|-------------|
| **State lock** | `g_state_cs` | `common.h:15` | `raid_service.c:59` | Protects `g_state` and all volume/disk state |
| **Cache lock** | `cache->lock` | `common.h:217` | `ram_cache.c:33` | Protects RAM cache buffer, dirty/valid maps |
| **Journal lock** | `g_journal_cs` | `journal.c:5` | `journal.c:10` | Serializes journal file writes |
| **Event bus lock** | `g_eb_cs` | `event_bus.c:12` | `event_bus.c:29` | Protects subscriber list during publish |
| **File table lock** | `g_file_table_lock` | `fuse_bridge.c:37` | `fuse_bridge.c:542` | Protects FUSE file table |
| **Log lock** | `g_log_lock` | `logger.c:7` | `logger.c:12` | Serializes log file writes |
| **GUI log lock** | `g_gui.log_lock` | `gui.cpp:70` | `gui.cpp:1613` | Protects GUI event log buffer |

## Rules

1. **Always acquire in the listed order.** If you hold `cache->lock`, you may
   acquire `g_journal_cs`, but you may NOT acquire `g_state_cs`.

2. **Never acquire a lock lower in the order while holding a higher one.**
   Example: while holding `g_state_cs`, do not acquire `cache->lock` unless
   you have first verified the reverse order is safe and documented the
   exception.

3. **Locks at the same indentation level** (e.g., `g_eb_cs` and `g_file_table_lock`)
   have no ordering constraint relative to each other — they are never acquired
   together in the same code path.

4. **Leaf locks** (`g_log_lock`, `g_gui.log_lock`) may be acquired at any time
   without acquiring higher locks, but if you hold a leaf lock you must not
   acquire any higher lock.

5. **Re-entrancy is forbidden.** No lock may be acquired twice by the same
   thread (Windows critical sections are not recursive). All code paths must
   guarantee single acquisition.

## Enforcement

- All new code must follow this order.
- Existing code that nests critical sections should be audited for compliance.
- Deadlocks caused by order violations are considered P0 bugs.

## Thread Model

| Thread Type | Locks Held During Normal Operation |
|-------------|-------------------------------------|
| Main / CLI  | `g_state_cs` (via `gs_lock()`) |
| FUSE callbacks | `g_state_cs`, `g_file_table_lock` |
| Cache flush thread | `cache->lock`, `g_journal_cs` |
| GUI worker thread | `g_state_cs` (calls `raid_*` functions) |
| Logger | `g_log_lock` (leaf — never acquires any other lock) |
