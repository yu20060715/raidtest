# Real Hardware Test Procedure — RC1

完整人工測試流程，每一步列出預期結果與失敗條件。

---

## Prerequisites

- 2+ physical SSD/NVMe (非系統碟)
- Windows 10/11
- WinFsp installed
- RAIDTEST GUI compiled (`g++ gui.cpp ...`)

---

## 測試流程

### Step 1: Scan

**操作：** 開啟 GUI → 點擊 **Scan**

**預期結果：**
- Event Log 顯示 `[DISK_FOUND] <model>`
- Disk List 出現所有物理磁碟（不含 C:）
- 每個磁碟顯示型號、序號、容量、類型、Bus

**失敗條件：**
- 系統碟未被過濾（C: 出現在列表中）
- 任何 SSD 未出現
- 型號/序號亂碼

**驗證：** □ Pass □ Fail

---

### Step 2: Select Disks

**操作：** 勾選 2–4 顆 SSD 的 checkbox

**預期結果：**
- Planner 面板顯示：
  - Selected disks: 2/3/4
  - Total raw: X MB (X GB)
  - RAID0 capacity: X MB (X GB)
  - Efficiency R0: >95%

**失敗條件：**
- 無法勾選
- Planner 數值有誤

**驗證：** □ Pass □ Fail

---

### Step 3: Create RAID0 Volume

**操作：** 設定 Cache 大小 (建議 4096 MB) → 點擊 **Create**

**預期結果：**
- Progress bar 顯示進度
- Event Log:
  - `[VOLUME_CREATED] RAID0`
  - Pool file 建立成功
- Status: Volume is ready for mount

**失敗條件：**
- Create 失敗（事件記錄錯誤原因）
- Pool file 建立位置錯誤

**驗證：** □ Pass □ Fail

---

### Step 4: Mount

**操作：** 設定磁碟代號（預設 G:）→ 點擊 **Mount**

**預期結果：**
- Event Log: `[MOUNT]`
- Volume Info 顯示 Mounted: Yes (綠色)
- Windows 檔案總管出現 G: 磁碟
- G: 顯示正確的容量（約等於 RAID0 capacity）

**失敗條件：**
- Mount 失敗（WinFsp 未安裝？）
- 容量顯示為 0
- Mount 後無法存取

**驗證：** □ Pass □ Fail

---

### Step 5: Windows Explorer — Copy Large Files

**操作：**
1. 從系統碟複製 1 個 10GB+ 檔案到 G:
2. 從系統碟複製 1000 個小檔 (1KB–1MB) 到 G:
3. 從系統碟複製 1 個 50GB+ 檔案到 G:

**預期結果：**
- 複製過程無錯誤
- Event Log 顯示正常的 IO（bytes_written 增加）
- Volume Info 顯示 bytes_written 持續增加
- Windows 顯示正常的複製速度

**失敗條件：**
- 複製到一半出現「磁碟空間不足」
- Windows 出現「延遲寫入失敗」
- 複製速度異常慢（<50 MB/s 在 NVMe RAID0 上）
- 小檔複製特別慢

**驗證：** □ Pass □ Fail | 速度: _____ MB/s

---

### Step 6: Verify Data Integrity

**操作：**
1. 在複製的檔案上按右鍵 → 內容 → 檢查大小/檔案數
2. 使用 `fc /b <source> <dest>` 比較大檔案

**預期結果：**
- 檔案大小與來源一致
- `fc /b` 回傳 「沒有差異」
- 小檔全部存在、可讀取

**失敗條件：**
- 檔案大小不一致
- fc 回傳差異
- 部分檔案無法存取

**驗證：** □ Pass □ Fail

---

### Step 7: Unmount

**操作：** 點擊 **Unmount**

**預期結果：**
- Event Log: `[UNMOUNT]`
- Volume Info 顯示 Mounted: No
- G: 從檔案總管消失
- 所有寫入的資料仍在（Volume 未刪除）

**失敗條件：**
- Unmount 失敗
- Volume 被一併刪除

**驗證：** □ Pass □ Fail

---

### Step 8: Load (Re-open)

**操作：** 關閉 GUI → 重新開啟 GUI → 點擊 **Scan**

**預期結果：**
- Volume Info 顯示已存在的 Volume
- Mounted: No
- UUID/Generation 與先前一致

**失敗條件：**
- Volume 未出現（metadata 未正確儲存）
- UUID 變了

**驗證：** □ Pass □ Fail

---

### Step 9: Re-mount & Verify

**操作：** 再次 Mount → 檢查 G: 內容

**預期結果：**
- 所有先前複製的檔案仍在
- 檔案大小/內容正確
- `fc /b` 再次通過

**失敗條件：**
- 檔案消失
- 檔案內容錯誤
- 檔案系統損毀

**驗證：** □ Pass □ Fail

---

### Step 10: Destroy

**操作：** 點擊 **Destroy** → 確認 Destroy

**預期結果：**
- Event Log: `[VOLUME_DESTROYED]`
- Volume Info 顯示 No volume
- G: 磁碟已消失
- Pool file 被刪除

**失敗條件：**
- Destroy 失敗
- Pool file 殘留

**驗證：** □ Pass □ Fail

---

## Crash Recovery Test

### Task Manager Kill

**操作：**
1. Mount Volume (G:)
2. 正在複製檔案時 → 開啟 Task Manager → End Task 殺掉 raidtest.exe
3. 重新開啟 raidtest.exe
4. Scan → Load Volume
5. Mount
6. 檢查 G: 內容 + 檔案一致性

**預期結果：**
- Volume 可正常載入
- 檔案無損毀（或僅最後寫入部分丟失，由 journal 保護）
- 可重新 mount

**失敗條件：**
- Volume 無法載入（metadata 損毀）
- 檔案系統需 chkdsk
- 所有檔案遺失

**驗證：** □ Pass □ Fail

---

## Performance Test

### CrystalDiskMark (第三方驗證)

**操作：**
1. Mount Volume (G:)
2. 執行 CrystalDiskMark → 選 G:
3. 執行 Sequential Q32T1、Random 4K Q32T1、Sequential 1M Q1T1

**預期結果（2×NVMe SSD RAID0）：**
- Sequential Read: >5000 MB/s
- Sequential Write: >4000 MB/s
- Random 4K Read: >500K IOPS
- Random 4K Write: >300K IOPS

**失敗條件：**
- 效能低於單碟（表示 RAID0 疊加無效）
- 隨機 IO 極低（<100K IOPS）

**驗證：** □ Pass □ Fail

### DiskSpd (官方工具)

```powershell
# Sequential 1M
diskspd -b1M -o8 -t4 -w100 -d30 G:\testfile.dat
diskspd -b1M -o8 -t4 -w0 -d30 G:\testfile.dat

# Random 4K
diskspd -b4K -o32 -t4 -w100 -d30 G:\testfile.dat
diskspd -b4K -o32 -t4 -w0 -d30 G:\testfile.dat

# Mixed 70/30
diskspd -b4K -o32 -t4 -w30 -d30 G:\testfile.dat
```

**驗證：** □ Pass □ Fail

---

## 測試紀錄

| 日期 | 測試者 | 環境 | Steps | Pass/Fail | 備註 |
|------|--------|------|-------|-----------|------|
| | | | 1–10 | | |
