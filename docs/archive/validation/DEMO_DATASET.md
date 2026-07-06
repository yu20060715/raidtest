# Demo Dataset Planning — RC1

規劃 Demo 使用的測試資料，適用於老師/客戶展示。

---

## 場景

展示 RAID0 將多顆 SSD 疊加為大容量高速 Volume，
並透過 Windows Explorer 直接存取。

---

## Dataset 類型

### Type A: 大檔案 (Sequential)

| 檔案 | 大小 | 數量 | 總計 | 用途 |
|------|------|------|------|------|
| video_4k.mp4 | 10 GB | 1 | 10 GB | 展示連續讀寫效能 |
| iso_backup.iso | 50 GB | 1 | 50 GB | 展示大檔案複製 |
| disk_image.img | 100 GB | 1 | 100 GB | 展示超大檔案 |

**預期行為：** 連續 I/O → 充分發揮 RAID0 頻寬

### Type B: 小檔案 (Random 4K)

| 檔案 | 大小 | 數量 | 總計 | 用途 |
|------|------|------|------|------|
| photos/*.jpg | 2–10 MB | 1000 | ~5 GB | 展示大量小檔讀寫 |
| docs/*.pdf | 0.5–5 MB | 500 | ~1 GB | 混合大小小檔 |
| config/*.json | 1–50 KB | 2000 | ~20 MB | 極小檔效能 |

**預期行為：** 隨機 I/O → 測試 metadata / cache 效能

### Type C: 混合工作負載

| 項目 | 說明 |
|------|------|
| Steam Game 目錄 | ~50GB, 混合大小 |
| SQLite 資料庫 | 5GB, 隨機 4K 為主 |
| VM 磁碟檔 | 40GB, 混合連續+隨機 |

---

## 準備腳本

```powershell
# === Generate demo dataset ===
param(
    [string]$MountPoint = "G:",
    [int]$LargeFileCount = 1,
    [int]$SmallFileCount = 1000
)

$demoDir = "$MountPoint\DEMO"
New-Item -ItemType Directory -Force -Path $demoDir

# Large files (sequential)
1..$LargeFileCount | ForEach-Object {
    $sizeGB = 10 * $_
    $file = "$demoDir\large_file_$($sizeGB)GB.bin"
    Write-Host "Creating $file ($sizeGB GB)..."
    $stream = [System.IO.File]::OpenWrite($file)
    $stream.SetLength($sizeGB * 1GB)
    $stream.Close()
}

# Small files (random)
1..$SmallFileCount | ForEach-Object {
    $sizeKB = Get-Random -Minimum 4 -Maximum 1024
    $file = "$demoDir\small_file_$_.dat"
    $buffer = New-Object byte[] ($sizeKB * 1KB)
    (New-Object Random).NextBytes($buffer)
    [System.IO.File]::WriteAllBytes($file, $buffer)
}

Write-Host "Dataset created in $demoDir"
Write-Host "Total files: $($LargeFileCount + $SmallFileCount)"
```

---

## Demo Script

### 展示流程 (10 分鐘)

| 時間 | 步驟 | 講解重點 |
|------|------|----------|
| 0:00 | 開啟 GUI → Scan | 展示自動偵測 SSD |
| 1:00 | Select 2–4 disks | 解釋 RAID0 概念 |
| 2:00 | Create Volume + Cache | 展示 Cache 設定 |
| 3:00 | Mount → Explorer 出現 | WinFsp 掛載 |
| 4:00 | Copy Type A (10GB) | 展示高速連續寫入 |
| 5:00 | Copy Type B (1000 small files) | 展示 metadata 效能 |
| 6:00 | Run DiskSpd benchmark | 展示 IOPS 數據 |
| 7:00 | Windows 複製速度觀察 | 與單碟比較 |
| 8:00 | Destroy + Cleanup | 一鍵清除 |
| 9:00 | 總結 | RAID0 適用場景 |

---

## 注意事項

1. **預先建立 Dataset：** 在系統碟上建立好測試檔案再複製
2. **避免壓縮：** 使用隨機 binary data（不可壓縮，真實反映效能）
3. **溫度監控：** NVMe 長時間連續寫入可能過熱降速
4. **備份對照：** 先紀錄單碟 CrystalDiskMark 數據，再測 RAID0
5. **Volume 大小：** 建議建立至少 500GB 以上的 Volume
