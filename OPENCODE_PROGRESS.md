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

## Priority 4: Wire pool_mb into Settings dialog
- Added "Default pool size (MB)" input field to `ShowSettings()` between mount_letter and cache_mb fields. Syncs to `g_gui.pool_size_mb` on Save (existing logic at line 1038 already handled this).
- Priority 5 (Quick Setup pool_mb consistency) resolved implicitly: settings → `g_gui.pool_size_mb` sync chain already existed at startup (line 1992) and on save (line 1038). Disk allocation panel and Quick Setup both read `g_gui.pool_size_mb`.
- Build: OK

## Priority 6: Remove fake progress Sleep() loops from bench/random
- W_BENCHFS: removed 4× `Sleep(250)` loop with fake step counter (lines 366-371). Now sets progress to 0.5 before raid_benchfs, 1.0 after.
- W_BENCH (raw): removed 4× `Sleep(250)` loop (lines 714-718). Same pattern.
- W_RANDOM: removed 5× `Sleep(200)` loop (lines 768-772). Same pattern.
- Removed unused `bench_done`, `raw_bench_done`, `random_done` goto labels.
- Build: OK

## Priority 7: Dark theme combo only showed Light
- `ApplyTheme()` only called `SetupLightTheme()` regardless of the theme setting.
- Added `SetupDarkTheme()` using `ImGui::StyleColorsDark()`. `ApplyTheme()` now branches on `g_gui.use_light_theme`.
- Startup was hardcoded `use_light_theme = true` — now reads `g_gui.settings.theme` instead.
- Default config is `THEME_DARK` (from `config_defaults`), so first run now uses dark theme as intended.
- Build: OK
- `W_MIRROR` directly called `raid_mirror()` → `volume_create_internal()` which opens pool files, but they were never created. Result: Mirror was entirely broken.
- Fixed by following same pattern as `W_CREATE`: build disk:pool_size arg string from Disk Allocation panel, pass to `raid_init_pools()`, then call `raid_mirror()`.
- Both toolbar Mirror (line 1149) and Actions menu Mirror (line 1923) now build and pass pool params.
- Build: OK
