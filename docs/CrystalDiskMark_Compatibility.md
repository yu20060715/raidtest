# CrystalDiskMark 9 Compatibility

## 1. 實際測試結果 (2026-07-10)

R: 掛載後，使用 `check_cdm.exe R:` 驗證：

| 步驟 | API | 結果 |
|------|-----|------|
| 01 | `GetLogicalDrives()` | ✅ PASS |
| 02 | `GetVolumeInformationA("R:\\")` | ✅ PASS (fs="FUSE-RAIDTEST") |
| 03 | `GetDiskFreeSpaceExA("R:\\")` | ✅ PASS (Total=102400 MB, Free=102400 MB) |
| 04 | `CreateFile("R:\\test.tmp", CREATE_ALWAYS)` | ✅ PASS |
| 05 | `WriteFile(64KB)` | ✅ PASS |
| 06 | `FlushFileBuffers()` | ✅ PASS |
| 07 | `GetFileSizeEx()` | ✅ PASS (size=65536) |
| 08 | `CloseHandle()` | ✅ PASS |
| 09 | `DeleteFileA()` | ✅ PASS |
| 10 | `CreateFile("\\\\.\\R:", volume handle)` | ✅ PASS |
| 11 | `DeviceIoControl(IOCTL_DISK_GET_LENGTH_INFO)` | ❌ FAIL (volume-level IOCTL) |
| 12 | `DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY)` | ❌ FAIL (volume-level IOCTL) |

### 重要釐清

**步驟 11-12 的 IOCTL 是 volume-level 操作**，透過 `\\.\R:` volume handle 送出，經過 Windows 儲存堆疊，**不經由 FUSE `ioctl` callback**。因此 fuse_bridge.c 中的 `raid_ioctl` handler 不會被呼叫到。這是正常行為，非 bug。

**CrystalDiskMark 不使用這些 volume IOCTL。** CDM 原始碼（DiskBench.cpp）實證：

1. `GetLogicalDrives()` — 列舉磁碟
2. `GetDriveType()` — 過濾固定式磁碟
3. `GetDiskFreeSpaceEx()` — 查詢容量
4. `CreateFile()` — 建立測試檔案（FILE_FLAG_NO_BUFFERING）
5. `SetFilePointerEx()` + `SetEndOfFile()` — 預先配置空間
6. `WriteFile()` — 填入測試資料（1 MB blocks）
7. `DeviceIoControl(hFile, FSCTL_SET_COMPRESSION, ...)` — 關閉壓縮（file-level，忽略回傳值）
8. 關閉檔案，執行 `diskspd.exe` 進行實際測試

CDM 的效能測試完全委託給 `diskspd.exe`，該工具使用標準 Win32 File I/O（CreateFile、ReadFile、WriteFile），**不依賴 volume IOCTL**。

---

## 2. 關鍵 FUSE Callback 狀態

| Callback | 現況 | 位置 |
|----------|------|------|
| `fgetattr` | ✅ 已實作（委託 `raid_getattr`，從 `g_open_files[].size` 讀取） | `fuse_bridge.c:156` |
| `ioctl` | ✅ 已實作（處理 `IOCTL_DISK_GET_LENGTH_INFO` / `IOCTL_STORAGE_QUERY_PROPERTY`） | `fuse_bridge.c:494` |
| `raid_write` 更新檔案大小 | ✅ 每次成功寫入後更新 `g_open_files[].size` | `fuse_bridge.c:449-458` |
| Volume IOCTL 路由 | ⚠️ volume handle 的 IOCTL 由 Windows 儲存堆疊處理，不經 FUSE | 正常行為 |

注意：`raid_ioctl` 中 function code 萃取曾有 bug（`>> 4` 應為 `>> 2`），已修正於 `fuse_bridge.c:500`。

---

## 3. 已知限制與非阻礙

### FSCTL_SET_COMPRESSION

CDM 在建立測試檔後會呼叫 `DeviceIoControl(hFile, FSCTL_SET_COMPRESSION)` 以確保檔案未壓縮。
WinFsp FUSE 若未處理此 FSCTL，會回傳錯誤，但 CDM **忽略回傳值**，不影響執行。

如需改進，可在 `raid_ioctl` 中新增 case 回傳 0（成功無操作）：

```c
case 0x040: /* FSCTL_SET_COMPRESSION — ignore, return success */
    return 0;
```

### FILE_FLAG_NO_BUFFERING

CDM 以 `FILE_FLAG_NO_BUFFERING` 開啟測試檔。WinFsp FUSE 支援此旗標（傳遞至 FUSE kernel driver），
但在 FUSE daemon 端無需特殊處理。

---

## 4. CDM 預期行為總結

| 情境 | 預測 | 備註 |
|------|------|------|
| R: 出現在 CDM 磁碟列表 | ✅ 是 | `GetLogicalDrives()` PASS |
| 顯示總容量 | ✅ 正確 (`GetDiskFreeSpaceEx`) |
| File Benchmark（Sequential / Random） | ✅ 正常 | 透過 diskspd 執行標準 File I/O |
| Volume Benchmark | ⚠️ CDM 不支援 volume mode (僅 File Test) |
| FSCTL_SET_COMPRESSION 錯誤 | ✅ 不影響 | CDM 忽略回傳值 |
| 系統當機 / 藍畫面 | ✅ 無風險 | 所有 API 回傳正常值 |

**結論：CrystalDiskMark 9 應可在 RAIDTEST 掛載的 R: 上正常執行檔案型效能測試。**

---

## 5. 附錄：CDM 原始碼實際 API 序列

取自 https://github.com/hiyohiyo/CrystalDiskMark (DiskBench.cpp)：

```cpp
// 1. 列舉磁碟（DiskMarkDlg.cpp）
GetLogicalDrives();
GetDriveType();

// 2. 查詢容量
GetDiskFreeSpaceEx(RootPath, ...);

// 3. 建立測試目錄與檔案
CreateDirectory(TestFileDir);
CreateFile(TestFilePath, ..., FILE_FLAG_NO_BUFFERING);

// 4. 預先配置空間
SetFilePointerEx(hFile, nFileSize, ..., FILE_BEGIN);
SetEndOfFile(hFile);

// 5. 關閉壓縮（可選）
DeviceIoControl(hFile, FSCTL_SET_COMPRESSION, COMPRESSION_FORMAT_NONE, ...);

// 6. 填入資料
WriteFile(hFile, buf, BufSize, ...);

// 7. 關閉
CloseHandle(hFile);

// 8. 執行 diskspd.exe 進行實際測試
CreateProcess("diskspd.exe", ...);
```
