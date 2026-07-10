param(
    [string]$TestLabel = "TestA_2NVMe",
    [string[]]$DiskIds = @("1", "2"),
    [string]$InitArgs = "1:4096 2:4096",
    [int]$Runs = 5,
    [int]$SizeMB = 4096,
    [int]$BlockKB = 1024,
    [switch]$SkipClean = $false,
    [switch]$DestroyAfter = $true
)

Set-Location -LiteralPath "C:\Users\Yu\Desktop\raidv3"

# Clean up old pool files
if (-not $SkipClean) {
    foreach ($did in $DiskIds) {
        $drive = switch ($did) {
            "1" { "D" }; "2" { "E" }; "3" { "F" }; "4" { "G" }
        }
        if ($drive) {
            Remove-Item -Path "${drive}:\RAIDTEST" -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "  Cleaned ${drive}:\RAIDTEST"
        }
    }
}

# Build command list
$cmds = @()

# Part 1: Setup
$cmds += "scan"
$cmds += "select $($DiskIds -join ' ')"
$cmds += "init $InitArgs"
$cmds += "create"

# Part 2: benchraw x Runs
for ($i = 1; $i -le $Runs; $i++) {
    $cmds += "benchraw $SizeMB $BlockKB"
}

# Part 3: cache off + benchfs x Runs
$cmds += "cache off"
for ($i = 1; $i -le $Runs; $i++) {
    $cmds += "benchfs $SizeMB $BlockKB"
}

# Optional: destroy
if ($DestroyAfter) { $cmds += "destroy" }

$cmds += "exit"

# Pipe all commands
Write-Host "`n==============================================" -ForegroundColor Yellow
Write-Host "  $TestLabel" -ForegroundColor Yellow
Write-Host "  Disks: $($DiskIds -join ', ')" -ForegroundColor Yellow
Write-Host "  Init: $InitArgs" -ForegroundColor Yellow
Write-Host "  benchraw x$Runs + benchfs(cache off) x$Runs" -ForegroundColor Yellow
Write-Host "==============================================`n" -ForegroundColor Yellow

$output = $($cmds -join "`n") | .\raid_cli.exe 2>&1
Write-Host $output

# Parse results
$lines = $output -split "`r`n|`n"
$rawWrite = @(); $rawRead = @()
$fsWrite = @(); $fsRead = @()
$ratioLines = @()
$benchLines = @()
$inRatio = $false
$inBench = $false

foreach ($line in $lines) {
    if ($line -match "Speed ratios:") { $inRatio = $true; continue }
    if ($line -match "Virtual volume created" -or $line -match "Superblock written") { $inRatio = $false }
    if ($inRatio -and $line -match "Disk\d:.*ratio=(\d+)") { $ratioLines += $Matches[1] }

    if ($line -match "RawDisk:") { $inBench = $true }
    if ($line -match "BenchFS:") { $inBench = $true }
    if ($line -match "Goodbye!" -or $line -match "> ") { $inBench = $false }

    if ($line -match "Raw Write.*=\s*(\d+)\s*MB/s") { $rawWrite += [double]$Matches[1] }
    if ($line -match "Raw Read.*=\s*(\d+)\s*MB/s") { $rawRead += [double]$Matches[1] }
    if ($line -match "Write.*=\s*(\d+)\s*MB/s" -and -not ($line -match "Raw")) { $fsWrite += [double]$Matches[1] }
    if ($line -match "Read.*=\s*(\d+)\s*MB/s" -and -not ($line -match "Raw")) { $fsRead += [double]$Matches[1] }
}

# Extract disk speeds from scan output
$diskSpeeds = @()
foreach ($line in $lines) {
    if ($line -match '\[\d{2}\]\s+\S+\s+\S+\s+\S+\s+(\d+)\s+(\d+)\s+\S:') {
        $diskSpeeds += @{Read=[double]$Matches[1]; Write=[double]$Matches[2]}
    }
}

Write-Host "`n==============================================" -ForegroundColor Yellow
Write-Host "  $TestLabel - RESULTS" -ForegroundColor Yellow
Write-Host "==============================================" -ForegroundColor Yellow

Write-Host "`n  DISK SPEEDS (from scan):"
for ($i = 0; $i -lt $diskSpeeds.Count; $i++) {
    Write-Host "    Disk $($DiskIds[$i]): Write=$($diskSpeeds[$i].Write) MB/s, Read=$($diskSpeeds[$i].Read) MB/s"
}

Write-Host "`n  RATIO: $($ratioLines -join ':')"

if ($rawWrite.Count -gt 0) {
    $avg = [math]::Round(($rawWrite | Measure-Object -Average).Average, 0)
    Write-Host "`n  BENCHRAW Write (MB/s): $([string]::Join(', ', $rawWrite))"
    Write-Host "    Average: $avg MB/s"
}
if ($rawRead.Count -gt 0) {
    $avg = [math]::Round(($rawRead | Measure-Object -Average).Average, 0)
    Write-Host "  BENCHRAW Read (MB/s): $([string]::Join(', ', $rawRead))"
    Write-Host "    Average: $avg MB/s"
}
if ($fsWrite.Count -gt 0) {
    $avg = [math]::Round(($fsWrite | Measure-Object -Average).Average, 0)
    Write-Host "`n  BENCHFS (cache off) Write (MB/s): $([string]::Join(', ', $fsWrite))"
    Write-Host "    Average: $avg MB/s"
}
if ($fsRead.Count -gt 0) {
    $avg = [math]::Round(($fsRead | Measure-Object -Average).Average, 0)
    Write-Host "  BENCHFS (cache off) Read (MB/s): $([string]::Join(', ', $fsRead))"
    Write-Host "    Average: $avg MB/s"
}

Write-Host "`n==============================================" -ForegroundColor Yellow
Write-Host ""
