# RAIDTEST v1.0 RC4 — Capstone Presentation Script

**Target:** 10–12 minutes total  
**Format:** Slides + live demo  
**Audience:** Professor / Review Committee  
**Language:** Chinese (with English technical terms)

---

## Section 1 — 30-Second Project Introduction (30 sec)

**Slide:** Project title, logo, one-line tagline

**Speaker:**
"各位教授好，我的專題是 RAIDTEST — 一個在 Windows 上原生運作的軟體 RAID 系統。

RAIDTEST 可以將多顆實體硬碟合併成一個虛擬磁碟區，透過條帶化提升效能，或透過鏡像提供資料備援。

系統包含完整圖形介面、命令列工具、以及 39 項自動化測試，總共約 6000 行 C/C++ 原始碼。"

---

## Section 2 — Problem Background (1 min)

**Slide:** Data growth → storage challenges → need for RAID

**Speaker:**
"隨著資料量持續增長，個人與中小企業面臨幾個儲存困境：

第一，一顆大容量 SSD 的價格遠高於多顆小容量 SSD 的組合。

第二，單一硬碟故障可能導致全部資料遺失。

第三，作業系統內建工具不足——Windows 的磁碟管理員只能建立軟體 RAID0 和 RAID1，但功能有限且缺乏現代化介面。

RAID 技術就是為了解決這些問題而設計——透過將多顆磁碟組合成陣列，同時提升效能與可靠度。"

---

## Section 3 — Why Windows Software RAID? (1 min)

**Slide:** Comparison table — Hardware RAID vs Software RAID vs Windows built-in

**Speaker:**
"那為什麼選擇在 Windows 上做軟體 RAID？

硬體 RAID 卡雖然效能好，但價格昂貴，而且綁定特定廠商——如果 RAID 卡故障，必須找同款晶片才能讀取資料。

作業系統內建的方案——像是 Windows 的動態磁碟或 Storage Spaces——各有其限制。動態磁碟是 20 年前的技術，缺乏現代化工具。Storage Spaces 雖然功能完整，但架構複雜，不易理解與客製化。

我們的目標是做一個輕量、透明、可理解的軟體 RAID 原型——原始碼全部開放，每一行都可以被審視與修改。這在大學教育與研究場景中特別有價值。"

---

## Section 4 — RAIDTEST Architecture (1.5 min)

**Slide:** 7-layer architecture diagram

**Speaker:**
"RAIDTEST 採用七層架構設計，從上到下分別是：

最上層是**前端層**，提供 GUI 和 CLI 兩種操作介面。GUI 使用 Dear ImGui 搭配 DirectX 11 渲染，CLI 則支援 31 個指令。

第二層是**服務層**，透過 30 個統一的 `raid_*()` API，讓 GUI 和 CLI 共享完全相同的後端邏輯。

第三層是**管理層**，包含 Volume Manager、Device Manager、Metadata Manager，負責磁碟與磁碟區的生命週期管理。

第四層是**引擎層**——這是我們的核心。Stripe Engine 實作非對稱條帶化演算法，Mirror Engine 實作 RAID1 鏡像與重建。

第五層是**儲存層**，包含 Write-Back Cache、Write-Ahead Journal、以及非同步 OVERLAPPED I/O。

第六層是**磁碟 I/O 層**，負責磁碟掃描、集區檔案管理、效能基準測試。

最底層是**作業系統介面**，透過 WinFsp FUSE 將 RAID 磁碟區掛載為 Windows 磁碟機代號。

每一層只依賴下一層，職責分明，易於測試與維護。"

---

## Section 5 — Asymmetric Stripe Algorithm (1.5 min)

**Slide:** Algorithm diagram with ratio/phase example

**Speaker:**
"RAIDTEST 的核心創新是**非對稱條帶化演算法**。

傳統 RAID0 要求所有磁碟容量與速度相同。如果你的陣列同時有 NVMe SSD 和傳統硬碟，傳統 RAID0 會讓 NVMe 被迫等待傳統硬碟，浪費效能。

我們的演算法解決了這個問題，流程如下：

第一步，**基準測試**——系統對每顆磁碟進行寫入速度測試，得到 MB/s 數據。

第二步，**比例計算**——將速度正規化為基於最大公因數的整數比例。例如 NVMe 2800 MB/s 對 SATA SSD 500 MB/s，比例為 28:5。

第三步，**相位映射**——虛擬 LBA 空間被劃分為循環，每個循環中的相位數量等於比例總和。速度快的磁碟在每個循環中服務更多相位。

結果是：28:5 比例的兩顆磁碟，NVMe 承擔約 85% 的 I/O，SATA SSD 承擔 15%，充分利用兩者的頻寬。

這項演算法讓我們能混搭不同速度的磁碟，而不受限於最慢的那一顆。"

---

## Section 6 — Cache + Journal + Recovery (1.5 min)

**Slide:** Data path diagram showing write flow

**Speaker:**
"資料寫入 RAID 磁碟區時，會經過三層保護機制：

第一層是 **Write-Back Cache**。

當應用程式寫入資料，資料先進入記憶體快取——以 64KB 為單位分割，用 dirty map 記錄哪些區塊尚未寫回磁碟。寫入立即回傳，應用程式不需等待磁碟 I/O。

快取有獨立的背景執行緒，每秒掃描 dirty map，將髒資料批次寫入磁碟。

第二層是 **Write-Ahead Journal**。

這是我們的當機復原機制。在快取將資料寫入磁碟之前，Journal 會先記錄一筆 WAL 日誌，格式包含 BEGIN、DATA、COMMIT 三種條目，每筆條目都有 CRC32 檢查碼。日誌寫入後立即呼叫 `FlushFileBuffers()` 確保資料確實抵達磁碟。

第三層是 **Crash Recovery**。

系統啟動時，Journal Recovery 會掃描日誌檔案。如果發現 BEGIN 條目但沒有對應的 COMMIT，表示上次寫入過程中系統當機。

此時 Recovery 會重新播放所有 DATA 條目，確保資料完整寫入後，再寫入新的 COMMIT。如果所有條目都完整（有 COMMIT），則直接清除日誌。

這三層機制確保：快取提供效能，日誌提供當機保護，復原流程確保資料在最糟情況下仍能恢復。"

---

## Section 7 — WinFsp FUSE Architecture (1 min)

**Slide:** WinFsp FUSE layer diagram showing callback flow

**Speaker:**
"要讓 RAID 磁碟區在 Windows 中像一般磁碟機一樣被存取，我們使用 **WinFsp**——這是一個開放原始碼的 Windows FUSE 實作。

WinFsp 提供一個**核心層級**的檔案系統驅動程式，讓使用者空間的程式可以實作檔案系統回呼函數。

RAIDTEST 實作了 13 個 FUSE 回呼——包括 getattr、readdir、read、write、create、mkdir、rmdir、rename、unlink、open、flush、release、statfs。

當使用者在檔案總管中操作 G:\ 磁碟機時，流程是：

檔案總管 → Windows 核心 → WinFsp 驅動程式 → fuse_bridge.c 回呼 → Stripe Engine 或 Mirror Engine → Cache → Journal → 非同步磁碟 I/O

這意味著任何 Windows 應用程式——記事本、Visual Studio、甚至遊戲——都可以直接讀寫 RAID 磁碟區，不需要任何特殊設定。

WinFsp 的選擇讓我們避免了開發核心驅動程式的複雜性與風險，同時仍然提供完整的檔案系統體驗。"

---

## Section 8 — Demo Operation Flow (2 min)

**Slide:** 8-step demo flow infographic (optional: live demo)

**Speaker:**
"接下來我將簡要說明展示流程——實際操作會在示範環節進行。

整個展示約 5 到 6 分鐘，包含八個步驟：

**步驟一：啟動**（30 秒）
執行 `raidtest_winfsp.exe`，進入三模式 GUI——Beginner、Advanced、Developer。

**步驟二：掃描磁碟**（30 秒）
點擊 Scan，透過 Windows IOCTL 偵測實體磁碟，顯示型號、序號、容量、測速結果。

**步驟三：建立 RAID1**（1 分鐘）
勾選兩顆磁碟，點擊 Mirror。系統建立鏡像磁碟區，每筆資料同時寫入兩顆磁碟。RAID0 雖然能展示條帶化效能，但無法展示故障復原；本次展示重點是可靠度，因此選擇 RAID1。

**步驟四：掛載**（1 分鐘）
點擊 Mount。WinFsp FUSE 註冊為 G:\。快取初始化，Journal 回放未完成的 WAL 條目。

**步驟五：寫入檔案**（30 秒）
在 G:\ 建立文字檔案，寫入內容。資料流經 FUSE → Mirror Engine → Cache → Journal → 磁碟 I/O。

**步驟六：模擬故障**（30 秒）
切換到 Developer 模式，使用 Simulation Controls 注入其中一顆鏡像磁碟故障。觀察狀態變為 DEGRADED。由於 RAID1 具有備援，系統仍可正常讀寫。

**步驟七：重建**（30 秒）
執行 Rebuild Wizard，從健康磁碟複製資料到替代磁碟。完成後狀態恢復為 MOUNTED，備援能力恢復。

**步驟八：驗證資料**（30 秒）
開啟檔案，確認資料在故障與重建過程中完整無損。

如果 GUI 因環境問題無法執行，我們有完整的 CLI 備援方案。"

---

## Section 9 — Test Results (30 sec)

**Slide:** Test summary table with PASS/FAIL indicators

**Speaker:**
"專案包含完整的測試套件，總共 39 項測試案例，全部通過：

Superblock 測試 12 項——驗證中繼資料的讀寫、CRC32 檢查、損毀偵測、向前相容性。

Cache 測試 10 項——驗證 Write-Back 行為、dirty block 追蹤、flush 正確性。

Journal 測試 4 項——驗證 WAL 的 BEGIN/DATA/COMMIT 循環、CRC 驗證、Crash Recovery。

Mirror Engine 測試 6 項——RAID1 建立、降級讀取、Write-to-all、64MB chunk 重建。

Stripe Engine 測試 8 項——RAID0 建立、非對稱比例計算、相位映射、容量擴充。

此外還有 5 項壓力測試：並發 I/O、隨機存取、中繼資料損毀、電源中斷復原、長時間穩定度測試——全部 PASS。

所有測試使用真實磁碟 I/O，沒有 mock 或模擬。"

---

## Section 10 — Limitations (Honest) (1 min)

**Slide:** Limitation categories — platform, RAID levels, cache, FUSE

**Speaker:**
"最後，我必須誠實說明這個系統目前的限制。

**平台方面**：這是一個 Windows-only 的實作，依賴 WinFsp 和 Windows API，無法在 Linux 或 macOS 上執行。執行時需要系統管理員權限。

**RAID 等級方面**：目前只實作 RAID0 和 RAID1。沒有 RAID5、RAID6，沒有抹除碼或分散式同位檢查。RAID10 僅有容量估算，無法實際建立。

**快取與日誌方面**：Write-Back Cache 的髒資料在斷電時會遺失，最多可能損失 1 秒的寫入資料。Journal 沒有循環緩衝區，WAL 檔案會持續增長直到重新啟動。

**FUSE 方面**：檔案表是 64 個項目的固定陣列，線性搜尋，滿時會靜默覆寫。不支援符號連結、ACL、或延伸屬性。同一時間只能掛載一個磁碟區。

**程式碼品質方面**：全域狀態 `g_state` 在 24 個 raid_service 函數和 14 個 FUSE 回呼中未正確加鎖，這是已知的 P0 問題。非同步 I/O 的 OVERLAPPED 結構配置在堆疊上——目前因為 blocking wait 才安全，但需要改為堆積配置。

這些限制都在 `KNOWN_LIMITATIONS.md` 中有完整記錄，也是未來改善的方向。

**總結：RAIDTEST 是一個功能完整、可運作的大學專題 Prototype，誠實面對限制，持續改進中。**

謝謝各位教授，歡迎提問。"

---

## Appendix — Slide Checklist

| Section | Est. Time | Slides Needed |
|---------|-----------|---------------|
| 1. Introduction | 30 sec | Title slide |
| 2. Problem Background | 1 min | Problem statement |
| 3. Why Windows RAID | 1 min | Comparison table |
| 4. Architecture | 1.5 min | 7-layer diagram |
| 5. Asymmetric Stripe | 1.5 min | Algorithm + example |
| 6. Cache/Journal/Recovery | 1.5 min | Data flow diagram |
| 7. WinFsp FUSE | 1 min | FUSE callback diagram |
| 8. Demo Flow | 2 min | Steps infographic / live demo |
| 9. Test Results | 30 sec | Test summary table |
| 10. Limitations | 1 min | Limitation categories |
| **Total** | **11-12 min** | **10+ slides** |
