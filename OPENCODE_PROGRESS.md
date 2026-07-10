# OPENCODE Progress

## Priority 1: Actions menu Mount missing mount letter
- **src/gui.cpp:1911** — Actions menu "Mount" called `start_worker(W_MOUNT, NULL)`, always falling back to `'G'` even when a different letter is set in Settings. Toolbar Mount correctly passed the letter. Fixed by passing mount letter from settings (same pattern as toolbar).
- Build: OK

## Priority 2: Mirror never creates pool files
- `W_MIRROR` directly called `raid_mirror()` → `volume_create_internal()` which opens pool files, but they were never created. Result: Mirror was entirely broken.
- Fixed by following same pattern as `W_CREATE`: build disk:pool_size arg string from Disk Allocation panel, pass to `raid_init_pools()`, then call `raid_mirror()`.
- Both toolbar Mirror (line 1149) and Actions menu Mirror (line 1923) now build and pass pool params.
- Build: OK

## Priority 3: strncpy truncation warning (line 904)
- `strncpy(g_gui.status, g_gui.worker_result, sizeof(g_gui.status)-1)` triggered GCC warning because `status` (128B) < `worker_result` (512B). Replaced with `snprintf(g_gui.status, sizeof(g_gui.status), "%s", ...)`.
- Build: OK (no more strncpy warning in our code; only third-party imgui warnings remain)
- `W_MIRROR` directly called `raid_mirror()` → `volume_create_internal()` which opens pool files, but they were never created. Result: Mirror was entirely broken.
- Fixed by following same pattern as `W_CREATE`: build disk:pool_size arg string from Disk Allocation panel, pass to `raid_init_pools()`, then call `raid_mirror()`.
- Both toolbar Mirror (line 1149) and Actions menu Mirror (line 1923) now build and pass pool params.
- Build: OK
