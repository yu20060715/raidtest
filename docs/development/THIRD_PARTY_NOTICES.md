# Third Party Notices

RAIDTEST v1.0 uses the following third-party libraries and tools.

---

## Dear ImGui (v1.92.8)

**Source:** https://github.com/ocornut/imgui  
**License:** MIT License  
**Location in repo:** `imgui/` (15 files)

Dear ImGui is a bloat-free graphical user interface library for C++.

```
MIT License

Copyright (c) 2014-2025 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## WinFsp (v2.1)

**Source:** https://github.com/billziss-gh/winfsp  
**License:** GPLv3 with a commercial exception  
**Location in repo:** `winfsp_headers/` (8 files), `winfsp-x64.dll`, `libwinfsp-x64.a`

WinFsp (Windows File System Proxy) provides a FUSE bridge for Windows, enabling RAIDTEST to mount virtual volumes as drive letters.

```
WinFsp - Windows File System Proxy
Copyright (c) Bill Zissimopoulos

WinFsp is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License version 3 as published by the
Free Software Foundation.

WinFsp is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

A commercial license for WinFsp is available from the author.
```

Full license text: https://github.com/billziss-gh/winfsp/blob/master/LICENSE

---

## DirectX 11

**Source:** https://developer.microsoft.com/windows/directx  
**License:** Windows SDK (Microsoft EULA)  
**Location:** System-provided (`d3d11.dll`, `dxgi.dll`, `d3dcompiler.dll`)

DirectX 11 is a component of the Windows operating system. RAIDTEST uses it under the standard Windows SDK license terms.

---

## MinGW-w64 (GCC)

**Source:** https://www.mingw-w64.org  
**License:** GPLv3 with GCC Runtime Library Exception  
**Location:** Build-time dependency (not distributed)

MinGW-w64 provides the GCC compiler toolchain used to build RAIDTEST.

```
GCC (GNU Compiler Collection) is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.

Under Section 7 of GPL version 3, you are granted additional permissions
described in the GCC Runtime Library Exception.
```

---

## Acknowledgements

RAIDTEST builds upon these open-source projects. We thank the maintainers and contributors for their work.
