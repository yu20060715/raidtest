# Contributing to RAIDTEST

## Code of Conduct

Be respectful, constructive, and inclusive. Harassment and derogatory comments will not be tolerated.

## How to Contribute

### Reporting Bugs
- Open an issue describing the bug
- Include: RAIDTEST version, Windows version, hardware configuration
- Include reproduction steps
- Include relevant log output (from CLI or GUI event log)

### Suggesting Features
- Open an issue describing the feature
- Explain the use case and expected behavior
- Note any relevant technical constraints (Win32 API, WinFsp, etc.)

### Submitting Code

#### Branching
- `main` — release branch (tagged releases only)
- `develop` — integration branch
- Feature branches: `feature/description`

#### Commit Messages
```
<area>: <brief description>

<optional detailed explanation>
```
Examples:
- `stripe: fix misaligned read in asymmetric phase mapping`
- `gui: add cache flush progress indicator`
- `journal: handle corrupted payload CRC during recovery`

#### Coding Standards

**C Code (`.c`/`.h`):**
- C11 standard (no GNU extensions except `__attribute__`)
- Use `stdint.h` types (`uint32_t`, `int64_t`, etc.)
- Prefix public symbols with module name (e.g., `stripe_volume_create`)
- Use `bool` from `stdbool.h`
- Error handling via `RC` enum return codes
- Use `LOG_*` macros for logging (no direct `printf` in engine code)

**C++ Code (`gui.cpp`):**
- C++17 standard
- ImGui code wrapped in `extern "C"` for C header inclusion
- Use ImGui's built-in types (`ImVec2`, `ImVec4`, `ImDrawList`)
- Keep GUI code in `RenderMainUI` with panel sub-functions

**Headers:**
- Include guards via `#pragma once`
- Document public API with brief comments
- Internal/static functions need no header declaration

#### Testing
- All engine changes must include or update tests
- Tests live in `src/test_*.c`
- Run existing tests: `raidtest_tests.exe`
- All 38 tests must pass before submitting
- Test data path: `C:\RAIDTEST\` (run as Administrator)

#### Build Verification
Before submitting:
```batch
build.bat
raidtest_tests.exe
raidtest_winfsp.exe --version
```

#### Pull Request Process
1. Fork the repository and create a feature branch
2. Make changes following the coding standards
3. Add or update tests as needed
4. Run all 38 tests and confirm they pass
5. Submit a pull request to `develop` branch
6. Ensure the PR description explains the change and its motivation

## Areas Needing Contribution

See [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) for the full list. High-impact areas:

- **RAID5/6 implementation** — parity-based redundancy
- **S.M.A.R.T. monitoring** — disk health attribute reading
- **Journal improvements** — circular buffering, data CRC
- **Cache eviction** — LRU or similar strategy
- **Cross-platform support** — Linux FUSE port
- **Test infrastructure** — mocking layer, test isolation
- **GUI modularization** — split `gui.cpp` into panel files
- **End-to-end checksumming** — data integrity verification
- **Encryption** — transparent data encryption at rest

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
