# QUICK START

```
下載
 ├─ 下載 MinGW-w64 (msys2.org)
 └─ 下載 WinFsp (winfsp.dev/rel/)

↓

安裝
 ├─ 安裝 MinGW-w64 並確認 gcc 可用
 └─ 安裝 WinFsp (預設路徑即可)

↓

Build
 └─ 執行 build.bat
       └─ 輸出 raidtest_winfsp.exe + raidtest_tests.exe

↓

建立 RAID
 ├─ raidtest_winfsp.exe scan             ← 掃描實體磁碟
 ├─ raidtest_winfsp.exe init 0:1024 1:1024  ← 建立 pool 檔案 (磁碟0:1GB, 磁碟1:1GB)
 └─ raidtest_winfsp.exe create            ← 建立 RAID0 磁碟區

↓

Mount
 └─ raidtest_winfsp.exe mount G           ← 掛載至 G:

↓

使用
 ├─ 在檔案總管中存取 G:\
 ├─ 新增／編輯／刪除檔案
 └─ 效能測試：raidtest_winfsp.exe bench G

↓

Unmount
 ├─ raidtest_winfsp.exe unmount
 └─ G:\ 從系統中移除

↓

Delete
 └─ raidtest_winfsp.exe destroy           ← 刪除磁碟區及 pool 檔案
```
