# OPENCODE Progress

## Priority 1: Actions menu Mount missing mount letter
- **src/gui.cpp:1911** — Actions menu "Mount" called `start_worker(W_MOUNT, NULL)`, always falling back to `'G'` even when a different letter is set in Settings. Toolbar Mount correctly passed the letter. Fixed by passing mount letter from settings (same pattern as toolbar).
- Build: OK
