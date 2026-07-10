# CrystalDiskMark 9 Compatibility

## 1. R: 是否看得到？

**現況：R: 未掛載。**

使用 `check_cdm.exe` 實際測試結果：

| 操作 | 結果 |
|------|------|
| `GetLogicalDrives()` 檢查 R: | ❌ R: 不存在 |
| `GetVolumeInformation("R:\\")` | ❌ 錯誤 3 (ERROR_PATH_NOT_FOUND) |
| `GetDiskFreeSpaceEx("R:\\")` | ❌ 錯誤 3 |
| `CreateFile("R:\\test.tmp")` | ❌ 錯誤 3 |
| `CreateFile("\\\\.\\R:")` | ❌ 錯誤 2 (ERROR_FILE_NOT_FOUND) |

**原因**：RAIDTEST volume 尚未 mount。

**預期掛載後**：WinFsp 掛載成功後，R: 會出現在檔案總管及所有應用程式的磁碟列表中，CrystalDiskMark 應該看得到。

---

## 2. 如果看得到，Start 時可能失敗在哪？

假設 RAID 已 mount R:，以下是 CrystalDiskMark 9 各測試模式的預期行為：

### CrystalDiskMark 9 預設模式：File Test

| 步驟 | Windows API | FUSE Callback | 現況 | 預測 |
|------|-------------|---------------|------|------|
| 1. 查詢磁碟資訊 | `GetVolumeInformation("R:\\")` | WinFsp 內部處理 | 從 mount option `volname=RAIDTEST` / `FileSystemName=NTFS` 讀取 | ✅ 正常 |
| 2. 查詢可用空間 | `GetDiskFreeSpaceEx("R:\\")` | `statfs` | `raid_statfs` 實作中 | ✅ 正常 |
| 3. 建立測試檔 | `CreateFile("R:\\test.tmp")` | `create` / `open` | `raid_create` 實作 | ✅ 正常 |
| 4. 寫入 | `WriteFile` | `write` | `raid_write` 實作 | ✅ 正常 |
| 5. 讀取 | `ReadFile` | `read` | `raid_read` 實作 | ✅ 正常 |
| 6. Flush | `FlushFileBuffers` | `flush` | `raid_flush` 實作 | ✅ 正常 |
| 7. 取得檔案大小 | `GetFileSizeEx` | `fgetattr` (或 WinFsp 內部) | `fgetattr` = NULL, `getattr` 查 `g_open_files[].size` | ⚠️ 見下方分析 |
| 8. 關閉檔案 | `CloseHandle` | `release` | `raid_release` 實作 | ✅ 正常 |
| 9. 刪除檔案 | `DeleteFile` | `unlink` | `raid_unlink` 實作 | ✅ 正常 |

#### ⚠️ GetFileSizeEx / fgetattr 問題

根據 WinFsp issue #326：

> The WinFsp kernel protocol requires that the file size is correctly reported upon completion of a Write request. The WinFsp-FUSE layer needs to perform a getattr or fgetattr in order to implement this requirement.

**每次 write 之後，WinFsp 會自動呼叫 `getattr` 或 `fgetattr` 來回報檔案大小給核心。**

`raid_write` 並未更新 `g_open_files[].size`（只更新 `g_ctx.highest_byte_written`），所以 `raid_getattr` 回傳的檔案大小始終為 0（除非先呼叫過 `truncate`）。

若 CrystalDiskMark 在寫入後查詢檔案大小，可能看到 size=0，導致：
- `GetFileSizeEx` 回傳 0
- 若 CDM 依賴此數據判斷測試是否正常，可能拒絕執行或顯示錯誤

**但 CDM 可能使用不同方法確認寫入資料量（如自行累計 written bytes），不一定依賴檔案大小查詢。需實際測試才能確定。**

### CrystalDiskMark "All" 模式（Volume 測試）

| 步驟 | Windows API | FUSE Callback | 現況 | 預測 |
|------|-------------|---------------|------|------|
| 1. 開啟 Volume | `CreateFile("\\\\.\\R:")` | WinFsp 內部 | WinFsp 允許 volume handle open | ✅ 可能正常 |
| 2. 取得容量 | `DeviceIoControl(IOCTL_DISK_GET_LENGTH_INFO)` | `ioctl` | `ioctl` callback = **NULL** | ❌ 失敗 |
| 3. 查詢設備屬性 | `DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY)` | `ioctl` | `ioctl` callback = **NULL** | ❌ 失敗 |
| 4. 寫入 | `WriteFile` | `write` | `raid_write` 實作（路徑為 "/"） | ✅ 可能正常 |
| 5. 讀取 | `ReadFile` | `read` | `raid_read` 實作 | ✅ 可能正常 |

---

## 3. 未完成的 WinFsp Callback

以下為 `fuse.h` 中標記 `[S]`（WinFsp 支援）但目前為 NULL 的 callback：

| Callback | 用途 | 優先度 |
|----------|------|--------|
| `readlink` | Symbolic link 讀取 | 低 |
| `symlink` | Symbolic link 建立 | 低 |
| `chmod` | 變更權限 | 低 |
| `chown` | 變更擁有者 | 低 |
| `utime` / `utimens` | 檔案時間設定 | 低 |
| `setxattr` / `getxattr` / `listxattr` / `removexattr` | Extended attributes | 低 |
| `opendir` | 目錄開啟 | 低（目前無實作也正常） |
| `releasedir` | 目錄關閉 | 低 |
| `fsyncdir` | 目錄 sync | 低 |
| `init` / `destroy` | 初始化/銷毀 | 低 |
| `access` | 存取權限檢查 | 低 |
| `ftruncate` | 透過 handle truncate | 中 |
| **`fgetattr`** | **透過 handle 取得檔案屬性** | **高** |
| **`ioctl`** | **DeviceIoControl 處理** | **高** |
| `getpath` | WinFsp 專用路徑查詢 | 中 |
| `setchgtime` / `setcrtime` | 檔案時間設定 | 低 |
| `chflags` | 檔案旗標設定 | 低 |

---

## 4. CrystalDiskMark 需要 vs 現有實作

| 需求 | Windows API | FUSE Callback | 狀態 |
|------|-------------|---------------|------|
| ✅ 取得磁碟可用空間 | `GetDiskFreeSpaceEx` | `statfs` | 已完成（`raid_statfs`） |
| ✅ 取得磁碟資訊 | `GetVolumeInformation` | WinFsp 內部（mount option） | 已完成（volname=RAIDTEST, FileSystemName=NTFS） |
| ✅ 檔案建立 | `CreateFile` | `create` / `open` | 已完成（`raid_create` / `raid_open`） |
| ✅ 資料寫入 | `WriteFile` | `write` | 已完成（`raid_write` → `stripe_volume_write`） |
| ✅ 資料讀取 | `ReadFile` | `read` | 已完成（`raid_read` → `stripe_volume_read`） |
| ✅ Flush | `FlushFileBuffers` | `flush` | 已完成（`raid_flush` → cache flush） |
| ✅ 檔案關閉 | `CloseHandle` | `release` | 已完成 |
| ✅ 檔案刪除 | `DeleteFile` | `unlink` | 已完成 |
| ❌ 檔案大小查詢 | `GetFileSizeEx` | `fgetattr` | **未實作**（`raid_write` 也未更新檔案大小） |
| ❌ Volume 容量查詢 | `DeviceIoControl(IOCTL_DISK_GET_LENGTH_INFO)` | `ioctl` | **未實作** |
| ❌ 設備屬性查詢 | `DeviceIoControl(IOCTL_STORAGE_QUERY_PROPERTY)` | `ioctl` | **未實作** |
| ❌ FSCTL 控制 | `DeviceIoControl(FSCTL_*)` | `ioctl` | **未實作** |

---

## 5. 最小修改方案

### 目標

只修改 `src/fuse_bridge.c`（WinFsp FUSE bridge），不修改 RAID 核心程式。

### 修改 A：實作 `ioctl` callback

在 `raid_ops` 中新增 `ioctl` 項目，處理以下 IOCTL：

#### IOCTL_DISK_GET_LENGTH_INFO (CTL_CODE(IOCTL_DISK_BASE, 0x0017, METHOD_BUFFERED, FILE_READ_ACCESS))

回傳 `GET_LENGTH_INFORMATION`：
```c
typedef struct {
    LARGE_INTEGER Length;
} GET_LENGTH_INFORMATION;
```

`Length` = `vol->virtual_total_bytes`（RAID 總容量）

#### IOCTL_STORAGE_QUERY_PROPERTY (CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_READ_ACCESS))

回傳 `STORAGE_DEVICE_DESCRIPTOR`：
```c
typedef struct {
    ULONG Version;
    ULONG Size;
    UCHAR DeviceType;          // FILE_DEVICE_UNKNOWN (0x22)
    UCHAR DeviceTypeModifier;
    BOOLEAN RemovableMedia;    // FALSE
    BOOLEAN CommandQueueing;   // TRUE
    ULONG VendorIdOffset;      // 0
    ULONG ProductIdOffset;     // 0
    ULONG ProductRevisionOffset;
    ULONG SerialNumberOffset;
    STORAGE_BUS_TYPE BusType;  // BusTypeVirtual (0x09 或其他)
    ULONG RawPropertiesLength;
    UCHAR RawDeviceProperties[1];
} STORAGE_DEVICE_DESCRIPTOR;
```

### 修改 B：實作 `fgetattr` callback + 更新 `raid_write` 檔案大小

#### 新增 `raid_fgetattr`：

與 `raid_getattr` 相同邏輯，但接收 `struct fuse_file_info* fi`。透過 `fi->fh`（或路徑查詢檔案表）回傳正確的 `st_size`。

#### 修改 `raid_write`：

在每次寫入成功後，更新 `g_open_files[idx].size`：
```c
// 在 raid_write 中，寫入完成後：
uint64_t write_end = (uint64_t)offset + total_written;
file_table_lock_init();
EnterCriticalSection(&g_file_table_lock);
int idx = find_open_file_locked(strip_path(path));
if (idx >= 0 && write_end > g_open_files[idx].size)
    g_open_files[idx].size = write_end;
LeaveCriticalSection(&g_file_table_lock);
```

### 非修改：WinFsp mount option 調整（可選）

在 `fuse_mount_volume` 的 mount options 中，目前使用 `FileSystemName=NTFS`。CrystalDiskMark 對 FS type 無特殊要求，此設定可保留。

### 不修改項目

| 模組 | 原因 |
|------|------|
| `stripe_engine.c` | RAID 演算法，不修改 |
| `ram_cache.c` | Cache，不修改 |
| `volume_manager.c` | Mapping/logic，不修改 |
| `gui.cpp` | GUI，不修改 |
| 任何 thread 相關 | Thread，不修改 |

### 總結

| 修改 | 檔案 | 行數估計 | 功能 |
|------|------|---------|------|
| A: 新增 `ioctl` callback | `fuse_bridge.c` | ~50 行 | 使 Volume 測試模式可查詢容量及設備屬性 |
| B: 新增 `fgetattr` + 更新寫入大小 | `fuse_bridge.c` | ~30 行 | 使 File 測試模式可正確取得檔案大小 |

總計約 80 行新增，不影響任何現有核心邏輯。
