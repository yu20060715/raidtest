# Hardware Validation Matrix — RC1

區分哪些功能只有 Unit Test、哪些已完成真機驗證。
目標：釐清 RC1 真正的穩定度。

---

## 驗證層級定義

| 層級 | 說明 | 圖示 |
|------|------|------|
| **Unit** | 僅有自動化 Unit Test | 🧪 |
| **Manual** | 有手動測試流程 (REAL_TEST.md) | 🖐️ |
| **Real** | 已在真機 SSD/NVMe 上驗證 | ✅ |
| **N/A** | 不適用或未實作 | ➖ |

---

## 完整驗證矩陣

### Disk Management

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| Scan physical disks | 🧪 | ✅ | 已在真機 SSD 上掃描成功 |
| Filter system drive | 🧪 | ✅ | 自動跳過 C: |
| Detect NVMe vs SATA | 🧪 | ✅ | 透過 IOCTL_STORAGE_QUERY_PROPERTY |
| Read disk serial | 🧪 | ✅ | 真機回傳正確序號 |
| Disk benchmark (speed) | 🧪 | ✅ | 真機寫入/讀取測速 |
| Pool file create/open | 🧪 | ✅ | 真機檔案建立成功 |
| Pool file delete | 🧪 | ✅ | 真機檔案刪除成功 |

### Volume Operations

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| RAID0 Create (2 disks) | 🧪 | ✅ | 真機建立 2/3/4 碟 |
| RAID0 Create (3 disks) | 🧪 | ✅ | 非對稱 mapping 驗證 |
| RAID1 Create (2 disks) | 🧪 | ➖ | 尚未在真機上鏡像 |
| Volume Destroy | 🧪 | ✅ | 真機銷毀成功 |
| Volume Load | 🖐️ | ✅ | 重啟後載入成功 |
| Volume Expand | 🧪 | ➖ | 僅 Unit Test |

### Mount / Filesystem

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| WinFsp FUSE mount | 🧪 | ✅ | 掛載為 G:/H: 磁碟 |
| WinFsp unmount | 🧪 | ✅ | 卸載成功 |
| Windows Explorer access | 🖐️ | ✅ | 可瀏覽目錄 |
| File copy (small files) | 🖐️ | ✅ | <1MB 檔案正常 |
| File copy (large files) | 🖐️ | ✅ | >1GB 檔案正常 |
| File delete | 🖐️ | ✅ | 刪除正常 |
| Directory create/delete | 🖐️ | ✅ | 目錄操作正常 |
| Long filename (>260 chars) | 🖐️ | ➖ | 未測試 |
| NTFS streams / ACL | ➖ | ➖ | 不支援 |

### Cache

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| Cache init (RAM) | 🧪 | ✅ | 真機分配數 GB RAM |
| Cache write-back | 🧪 | ✅ | 寫入快取後 flush |
| Cache flush to disk | 🧪 | ✅ | 髒塊定時回寫 |
| Cache read hit/miss | 🧪 | ➖ | 未驗證 hit rate |
| Cache destroy | 🧪 | ✅ | 正常釋放 |

### Journal

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| Journal BEGIN/DATA/COMMIT | 🧪 | ➖ | 僅 Unit Test |
| Journal replay (crash recovery) | 🧪 | ➖ | **未在真機驗證** |
| Journal with partial power loss | ➖ | ➖ | 需硬體模擬 |

### Reliability

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| Superblock CRC check | 🧪 | ✅ | 真機驗證 CRC 匹配 |
| Superblock write atomic | 🧪 | ✅ | Rename 方式寫入 |
| Disk failure detection | 🧪 | ➖ | 未模擬真機熱插拔 |
| Crash recovery (journal) | 🧪 | ➖ | **尚未在真機測試** |
| Power failure (simulated) | 🧪 | ➖ | 僅 Stress Test |
| Task Manager kill → recover | 🖐️ | ➖ | **需手動驗證** |

### Benchmark

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| Sequential Read | 🧪 | ✅ | 真機 256MB |
| Sequential Write | 🧪 | ✅ | 真機 256MB |
| Random 4K IOPS | ➖ | ➖ | 需 DiskSpd |
| Mixed workload | ➖ | ➖ | 需 DiskSpd |
| Latency measurement | 🧪 | ✅ | 粗略計算 |
| Multi-threaded IO | ➖ | ➖ | 需 DiskSpd |

### Edge Cases

| 功能 | Unit Test | 真機驗證 | 備註 |
|------|-----------|----------|------|
| Zero-byte pool (B4 fix) | 🧪 | ✅ | 回歸測試通過 |
| Partial write (B7 fix) | 🧪 | ➖ | 僅 Unit Test |
| Cache destroy race (B10 fix) | 🧪 | ➖ | 僅 Unit Test |
| _WIN32_WINNT warning | 🧪 | ✅ | 0 warnings |

---

## 真機測試環境 (建議)

| 項目 | 最低要求 | 建議 |
|------|---------|------|
| OS | Windows 10/11 x64 | Windows 11 23H2+ |
| CPU | 4 cores | 8+ cores |
| RAM | 8 GB | 32 GB |
| SSD (RAID) | 2 × 256 GB | 2–4 × NVMe 1TB |
| WinFsp | 2023.2+ | Latest |
| Bench tool | DiskSpd | CrystalDiskMark optional |

---

## 未驗證項目 (風險)

高優先級未驗證項目（需要在 RC2 前完成真機測試）：

1. **Journal crash recovery** — 模擬程式 crash 後 journal replay
2. **Power failure** — 模擬電源中斷後資料一致性
3. **Disk full** — Volume 滿載時行為
4. **Long-term stability** — 連續 24h+ 讀寫
5. **Random 4K IOPS** — 真實資料庫模擬
6. **Multi-client access** — 多執行緒同時存取
