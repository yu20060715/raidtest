param([int]$WaitSeconds = 15)

Set-Location "C:\Users\Yu\Desktop\raidv3"

Write-Host "Starting daemon, will send command after $WaitSeconds seconds..."

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$pwd\raidtest_winfsp.exe"
$psi.Arguments = "--daemon"
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true

$p = [System.Diagnostics.Process]::Start($psi)
Write-Host "PID: $($p.Id)"
Write-Host "HasExited: $($p.HasExited)"
Start-Sleep -Seconds 3
Write-Host "After 3s - HasExited: $($p.HasExited)" 

if (-not $p.HasExited) {
    Write-Host "Writing command to stdin..."
    try {
        $p.StandardInput.WriteLine("benchraw 4096 1024")
        Write-Host "Command written, waiting..."
    } catch {
        Write-Host "Error writing: $_"
    }
}

Start-Sleep -Seconds $WaitSeconds
Write-Host "After wait - HasExited: $($p.HasExited) Alive: $(-not $p.HasExited)"

if (-not $p.HasExited) {
    $p.Kill()
    Write-Host "Process killed"
}

$stdout = try { $p.StandardOutput.ReadToEnd() } catch { "no stdout" }
$stderr = try { $p.StandardError.ReadToEnd() } catch { "no stderr" }
Write-Host "STDOUT: $stdout"
Write-Host "STDERR: $stderr"
$p.Dispose()
