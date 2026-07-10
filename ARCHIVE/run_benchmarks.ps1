param(
    [string]$TestName = "TestA",
    [int]$Runs = 5,
    [int]$SizeMB = 4096,
    [int]$BlockKB = 1024
)

Set-Location -LiteralPath "C:\Users\Yu\Desktop\raidv3"

function Run-DaemonCommand {
    param([string[]]$Commands, [int[]]$DelaysAfter)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = ".\raidtest_winfsp.exe"
    $psi.Arguments = "--daemon"
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $p = [System.Diagnostics.Process]::Start($psi)
    Start-Sleep -Seconds 8  # Wait for daemon to load volume

    for ($i = 0; $i -lt $Commands.Length; $i++) {
        $cmd = $Commands[$i]
        $delay = if ($i -lt $DelaysAfter.Length) { $DelaysAfter[$i] } else { 3 }
        Write-Host "Sending: $cmd" -ForegroundColor Cyan
        $p.StandardInput.WriteLine($cmd)
        Start-Sleep -Seconds $delay
    }

    # Wait for process to exit (with timeout)
    if (-not $p.WaitForExit(60000)) {
        Write-Host "Daemon did not exit in 60s, killing..." -ForegroundColor Red
        $p.Kill()
    }

    $stdout = $p.StandardOutput.ReadToEnd()
    $stderr = $p.StandardError.ReadToEnd()
    $p.Dispose()
    return $stdout + $stderr
}

function Extract-BenchResults {
    param([string]$Output)

    $lines = $Output -split "`r`n|`n"
    $rawWriteLines = @()
    $rawReadLines = @()
    $fsWriteLines = @()
    $fsReadLines = @()
    $inRawSection = $false
    $inFsSection = $false

    foreach ($line in $lines) {
        if ($line -match "RawDisk") { $inRawSection = $true; $inFsSection = $false }
        elseif ($line -match "BenchFS") { $inFsSection = $true; $inRawSection = $false }

        if ($line -match "Raw Write.*=\s*([0-9]+)\s*MB/s") {
            $rawWriteLines += [double]$Matches[1]
        }
        if ($line -match "Raw Read.*=\s*([0-9]+)\s*MB/s") {
            $rawReadLines += [double]$Matches[1]
        }
        if ($line -match "Write.*=\s*([0-9]+)\s*MB/s" -and $inFsSection) {
            $fsWriteLines += [double]$Matches[1]
        }
        if ($line -match "Read.*=\s*([0-9]+)\s*MB/s" -and $inFsSection) {
            $fsReadLines += [double]$Matches[1]
        }
    }

    return @{
        RawWrite = $rawWriteLines
        RawRead  = $rawReadLines
        FsWrite  = $fsWriteLines
        FsRead   = $fsReadLines
    }
}

# Generate benchraw commands
$benchrawCmds = @()
for ($i = 1; $i -le $Runs; $i++) {
    $benchrawCmds += "benchraw $SizeMB $BlockKB"
}
$benchrawCmds += "exit"

# Run benchraw
Write-Host "`n=== $TestName : benchraw $Runs runs ===`n" -ForegroundColor Green
$rawOutput = Run-DaemonCommand -Commands $benchrawCmds -DelaysAfter (@(35) * $Runs + @(2))
Write-Host $rawOutput

$rawResults = Extract-BenchResults -Output $rawOutput

# Run benchfs with cache off
$benchfsCmds = @("cache off")
for ($i = 1; $i -le $Runs; $i++) {
    $benchfsCmds += "benchfs $SizeMB $BlockKB"
}
$benchfsCmds += "exit"

Write-Host "`n=== $TestName : benchfs (cache off) $Runs runs ===`n" -ForegroundColor Green
$fsOutput = Run-DaemonCommand -Commands $benchfsCmds -DelaysAfter (@(2) + @(35) * $Runs + @(2))
Write-Host $fsOutput

$fsResults = Extract-BenchResults -Output $fsOutput

# Print summary
Write-Host "`n================================================================" -ForegroundColor Yellow
Write-Host "  $TestName SUMMARY" -ForegroundColor Yellow
Write-Host "================================================================" -ForegroundColor Yellow
Write-Host ""

if ($rawResults.RawWrite.Count -gt 0) {
    Write-Host "  BenchRAW Write (MB/s):" -ForegroundColor Cyan
    $sum = 0.0
    foreach ($v in $rawResults.RawWrite) {
        Write-Host "    $v"
        $sum += $v
    }
    $avg = $sum / $rawResults.RawWrite.Count
    Write-Host "    --------------------"
    Write-Host "    Average: $([math]::Round($avg, 0)) MB/s" -ForegroundColor Green
}

if ($rawResults.RawRead.Count -gt 0) {
    Write-Host "  BenchRAW Read (MB/s):" -ForegroundColor Cyan
    $sum = 0.0
    foreach ($v in $rawResults.RawRead) {
        Write-Host "    $v"
        $sum += $v
    }
    $avg = $sum / $rawResults.RawRead.Count
    Write-Host "    --------------------"
    Write-Host "    Average: $([math]::Round($avg, 0)) MB/s" -ForegroundColor Green
}

if ($fsResults.FsWrite.Count -gt 0) {
    Write-Host "  BenchFS (cache off) Write (MB/s):" -ForegroundColor Cyan
    $sum = 0.0
    foreach ($v in $fsResults.FsWrite) {
        Write-Host "    $v"
        $sum += $v
    }
    $avg = $sum / $fsResults.FsWrite.Count
    Write-Host "    --------------------"
    Write-Host "    Average: $([math]::Round($avg, 0)) MB/s" -ForegroundColor Green
}

if ($fsResults.FsRead.Count -gt 0) {
    Write-Host "  BenchFS (cache off) Read (MB/s):" -ForegroundColor Cyan
    $sum = 0.0
    foreach ($v in $fsResults.FsRead) {
        Write-Host "    $v"
        $sum += $v
    }
    $avg = $sum / $fsResults.FsRead.Count
    Write-Host "    --------------------"
    Write-Host "    Average: $([math]::Round($avg, 0)) MB/s" -ForegroundColor Green
}

Write-Host "`n================================================================" -ForegroundColor Yellow
