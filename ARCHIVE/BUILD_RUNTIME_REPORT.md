# Build Runtime Report

## Problem

`raidtest_winfsp.exe` fails to start on systems without MSYS2/MinGW installed:

```
The program can't start because libwinpthread-1.dll is missing.
```

The test executable `raidtest_tests.exe` is **not** affected — it is a pure-C binary with no C++ objects and only depends on `KERNEL32.dll` and `msvcrt.dll`.

---

## Root Cause

### 1. GCC Thread Model: Posix

The MinGW-w64 GCC installed at `C:\msys64\mingw64\` was configured with `--enable-threads=posix`:

```
Thread model: posix
gcc version 16.1.0 (Rev5, Built by MSYS2 project)
```

This means GCC uses **POSIX threads** (via winpthread) rather than native Win32 threads. The winpthread library implements the POSIX threads API (`pthread_*`) on top of Windows primitives.

### 2. Static Libraries That Depend on pthreads

Two static libraries linked into `raidtest_winfsp.exe` have undefined references to pthread symbols:

**`libgcc_eh.a`** (GCC exception handling):
- `pthread_getspecific`, `pthread_key_create`
- `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock`
- `pthread_once`, `pthread_setspecific`

**`libstdc++.a`** (C++ standard library):
- `pthread_mutex_destroy`, `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock`
- `pthread_cond_broadcast`, `pthread_cond_wait`
- `pthread_once`

No source `.o` file in the project directly references pthread symbols — the dependency enters through these statically-linked runtime libraries.

### 3. GCC Spec Adds `-lpthread` Automatically

The GCC spec file unconditionally appends `-lpthread` to the linker command line:

```
%{!no-pthread:-lpthread}
```

This adds a second `-lpthread` search at the end of the link line, long after the `-Wl,-Bstatic ... -Wl,-Bdynamic` flags from `build.bat` have reset the linker to dynamic mode.

### 4. Library Resolution Order

```
build.bat link command:    ... -Wl,-Bstatic -lstdc++ -Wl,-Bdynamic ...
                                              ↑ libstdc++.a resolved statically ✓

GCC spec (auto-appended):  ... -lmingw32 -lgcc -lgcc_eh ... -lpthread ...
                                              ↑ libwinpthread.dll.a resolved dynamically ✗
```

Since the linker state is `-Bdynamic` when it reaches the spec's `-lpthread`, it finds `libwinpthread.dll.a` (107 KB, dynamic import library) instead of `libwinpthread.a` (91 KB, static archive).

### 5. Resulting DLL Imports from `libwinpthread-1.dll`

```
pthread_cond_broadcast
pthread_cond_wait
pthread_mutex_destroy
pthread_mutex_init
pthread_mutex_lock
pthread_mutex_unlock
pthread_once
```

---

## Build Scripts Inspected

| File | Exists? | Relevant? |
|------|---------|-----------|
| `build.bat` | Yes | **Primary** — contains the MinGW GCC link command |
| `Makefile` | No | Not present |
| `CMakeLists.txt` | No | Not present |

### `build.bat` Line 41 (Link Step)

```
g++ ... -static-libgcc -static-libstdc++
```

- `-static-libgcc` → statically links `libgcc.a` ✓
- `-static-libstdc++` → statically links `libstdc++.a` ✓
- **No flag to statically link `libwinpthread`** → the spec's trailing `-lpthread` resolves dynamically ✗

---

## All Dynamically Linked Libraries

| DLL | Type | Source | Can be statically linked? |
|-----|------|--------|---------------------------|
| `winfsp-x64.dll` | Third-party FUSE library | External dependency | **No** — only distributed as DLL |
| `libwinpthread-1.dll` | MinGW runtime | GCC spec auto-adds `-lpthread` | **Yes** → `libwinpthread.a` exists |
| `ADVAPI32.dll` | Windows system | `-ladvapi32` | No — Windows SDK DLL only |
| `d3d11.dll` | Windows system | `-ld3d11` | No — Windows SDK DLL only |
| `D3DCOMPILER_47.dll` | Windows system | `-ld3dcompiler` | No — Windows SDK DLL only |
| `dwmapi.dll` | Windows system | `-ldwmapi` | No — Windows SDK DLL only |
| `GDI32.dll` | Windows system | `-lgdi32` | No — Windows SDK DLL only |
| `KERNEL32.dll` | Windows system | Implicit | No — Windows SDK DLL only |
| `msvcrt.dll` | Windows CRT | Implicit | No — included in every Windows install |
| `ntdll.dll` | Windows system | Implicit | No — Windows kernel DLL |
| `SHELL32.dll` | Windows system | `-lshell32` | No — Windows SDK DLL only |
| `USER32.dll` | Windows system | `-luser32` | No — Windows SDK DLL only |

**Only `libwinpthread-1.dll` can be eliminated.**

---

## Recommendation

### Option A (Preferred): Add `-static` Flag

Change the link command in `build.bat` line 41 from:

```
g++ ... -static-libgcc -static-libstdc++
```

to:

```
g++ ... -static-libgcc -static-libstdc++ -static
```

**Why this works:**

The `-static` flag causes GCC to insert `-Bstatic` as the **first linker flag**, placing the entire link (including the spec's trailing `-lpthread`) in static mode. All libraries that have a static variant (including `libwinpthread.a`, `libmingwex.a`, `libgcc_eh.a`) are resolved statically, while Windows system DLLs (KERNEL32, ADVAPI32, etc.) and `winfsp-x64.dll` remain dynamic because they have no static alternatives.

**Verification result:**

| Build | Size | `libwinpthread-1.dll` dependency | Other changes |
|-------|------|----------------------------------|---------------|
| Without `-static` | 2,088 KB | **Yes** — fails on clean system | — |
| With `-static` | 2,145 KB | **No** — runs standalone | +57 KB (winpthread code inlined) |

All expected DLLs (`winfsp-x64.dll`, Windows system DLLs) remain unchanged.

**Binary size impact:** +57 KB (2.7% increase) — negligible.

### Option B (Fallback): Bundle `libwinpthread-1.dll`

If modifying the build is undesirable, copy `libwinpthread-1.dll` from the MinGW bin directory:

```
C:\msys64\mingw64\bin\libwinpthread-1.dll  →  <release folder>\
```

This is **not recommended** because it introduces a DLL searching dependency and adds one more file to distribute.
