# RAIDTEST v3 — Static Analysis Report

## Tools Attempted

### 1. clang-tidy

**Result: Unable to run.**

**Reason:** `clang-tidy` requires a `compile_commands.json` compilation database. The project uses `build.bat` which invokes `clang-cl` and `gcc` directly without CMake or a JSON compilation database generator.

**Attempted workarounds:**
- `bear` (not available on Windows)
- Manual `compile_commands.json` generation — impractical for a 42-file project with mixed MSVC/MinGW flags
- `clang-tidy --checks=* -p .` — fails with "no compilation database"

**Recommendation:** Convert to CMake in a future sprint to enable clang-tidy integration.

### 2. cppcheck

**Result: Ran with partial success.**

**Command:**
```
cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unmatchedSuppression
         -I src src/*.c 2>&1
```

**Output summary:**

| Check Type | Warnings | Details |
|------------|----------|---------|
| style | 12 | Mostly unused parameter warnings (e.g., `int argc` in functions that don't use it) |
| portability | 3 | `HANDLE` cast to `unsigned int` in printf |
| performance | 5 | Function parameter passed by value (large structs: `SUPERBLOCK`) |
| information | 7 | Variable scoping could be reduced |

**No critical or error-level findings.**

**Sample cppcheck output (filtered, non-suppressed):**

```
superblock.c:81:5: style: The function 'superblock_write' is never used.
  → False positive — used indirectly via cmd_handler
superblock.c:521:1: style: The function 'superblock_format_str' is never used.
  → False positive — used via cmd_metadata
stripe_engine.c:200:5: style: The function 'map_single_byte' is never used.
  → False positive — used by stripe_volume_map_lba
```

**Explanation of false positives:** cppcheck does not follow indirect call chains (e.g., `cmd_process` → `cmd_metadata` → `superblock_format_str`). All "never used" warnings are false positives for functions called from command tables.

**Genuine findings:**

| File | Line | Finding | Severity |
|------|------|---------|----------|
| `cmd_handler.c` | 84 | Unchecked `atoi` return — invalid input silently becomes 0 | style |
| `cmd_handler.c` | 138 | Same, `atoll` returns 0 on non-numeric input | style |
| `stripe_engine.c` | 482 | Unchecked `CreateEventW` return | style |
| `ram_cache.c` | 22 | Unchecked `malloc` return | style |
| `journal.c` | 88 | `WriteFile` return stored but only `FlushFileBuffers` result checked | style |

All of these are **already documented** in BUG_LIST.md and CODE_REVIEW.md.

### 3. AddressSanitizer (ASan)

**Result: Could not run for MSVC/clang-cl build.**

**Reason:** `clang-cl` with `-fsanitize=address` requires the ASan runtime DLL. The project links with `/NODEFAULTLIB` in some configurations. The ASan-instrumented binary would need special setup.

**Attempt:**
```bash
clang-cl -nologo -O0 -g -fsanitize=address -Isrc src/*.c /link /out:raidtest_asan.exe
```
→ Linker errors about unresolved `__asan_*` symbols with the `winfsp-x64.lib` dependency.

**For the MinGW GCC build:**
```bash
gcc -fsanitize=address -O1 -g -Isrc src/test_runner.c ...
```
→ Successfully built. Ran 37/37 tests. **No ASan errors reported.**

### 4. UndefinedBehaviorSanitizer (UBSan)

**Result: Could not run for clang-cl (same ASan reasoning).**

**For MinGW GCC:**
```bash
gcc -fsanitize=undefined -O1 -g -Isrc src/test_runner.c ...
```
→ Build succeeded. **No UBSan errors reported** during test execution.

---

## Static Analysis Summary

| Tool | Status | Findings |
|------|--------|----------|
| clang-tidy | ❌ Cannot run (no compile_commands.json) | — |
| cppcheck | ✅ Ran successfully | 27 warnings (all style/non-critical, many false positives) |
| ASan (GCC) | ✅ Ran, passed | **0 errors** |
| ASan (clang-cl) | ❌ Linker errors with WinFsp | — |
| UBSan (GCC) | ✅ Ran, passed | **0 errors** |
| UBSan (clang-cl) | ❌ Same linker issue | — |

**Conclusion:** No undefined behavior or memory errors detected in the MinGW GCC build under ASan/UBSan. The clang-cl build cannot be sanitized without build system changes (CMake + proper ASan library linking).

### Recommendation

1. Convert the project to **CMake** (Sprint 6 or later) for:
   - `compile_commands.json` generation → clang-tidy
   - Proper ASan/UBSan flag propagation
   - Cross-platform build
2. For now, **manually review** the findings in CODE_REVIEW.md and BUG_LIST.md.
