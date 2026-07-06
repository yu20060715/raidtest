# Performance Report Template — RC1

比較 Single SSD vs RAID0 效能的標準格式。

---

## 測試環境

| 項目 | 值 |
|------|-----|
| CPU | |
| RAM | |
| OS | |
| WinFsp | |
| RAIDTEST 版本 | |
| 測試日期 | |
| 測試者 | |

---

## 磁碟配置

### Single SSD

| 項目 | 值 |
|------|-----|
| 型號 | |
| 容量 | |
| 介面 | |
| 驅動 | |

### RAID0

| 項目 | 值 |
|------|-----|
| 磁碟數量 | |
| Stripe Unit | |
| Cache 大小 | |
| 總容量 | |

---

## Benchmark Results

### Sequential Read (1M)

| 指標 | Single SSD | RAID0 | 倍數 |
|------|-----------|-------|------|
| MB/s | | | |
| IOPS | | | |
| Latency (ms) | | | |

### Sequential Write (1M)

| 指標 | Single SSD | RAID0 | 倍數 |
|------|-----------|-------|------|
| MB/s | | | |
| IOPS | | | |
| Latency (ms) | | | |

### Random Read 4K

| 指標 | Single SSD | RAID0 | 倍數 |
|------|-----------|-------|------|
| IOPS | | | |
| MB/s | | | |
| Latency (ms) | | | |

### Random Write 4K

| 指標 | Single SSD | RAID0 | 倍數 |
|------|-----------|-------|------|
| IOPS | | | |
| MB/s | | | |
| Latency (ms) | | | |

### Mixed 70/30 (4K)

| 指標 | Single SSD | RAID0 | 倍數 |
|------|-----------|-------|------|
| IOPS | | | |
| Latency (ms) | | | |

---

## CPU Usage During Benchmark

| Workload | Single SSD | RAID0 |
|----------|-----------|-------|
| Sequential Read | | |
| Sequential Write | | |
| Random 4K Read | | |
| Random 4K Write | | |

---

## Analysis

### 是否達到預期？

- [ ] RAID0 Sequential 接近 N 倍單碟
- [ ] RAID0 Random 與單碟相當或更好
- [ ] Latency 未顯著增加

### 瓶頸分析

| 瓶頸 | 說明 |
|------|------|
| CPU | RAID0 是否需要更多 CPU？ |
| WinFsp | FUSE 轉換是否增加延遲？ |
| Cache | 大 cache 是否改善隨機寫入？ |
| Stripe | Stripe unit 是否最佳？ |

### 與其他 RAID 方案比較

| 方案 | Seq Read | Seq Write | Rand Read | Rand Write |
|------|----------|-----------|-----------|------------|
| RAIDTEST RAID0 | | | | |
| Windows Storage Spaces | | | | |
| Intel VROC | | | | |
| Soft RAID 實測 | | | | |

---

## 結論

- RAID0 增益: _____x sequential, _____x random
- 是否建議正式使用: □ Yes □ No
- 改善建議:

---

## Raw Data (DiskSpd CSV)

```csv
# Paste DiskSpd CSV output here
```
