param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [ValidateRange(3, 60)][int]$Seconds = 10
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "build/native/$Configuration/CSP-Grayscale-Viewer.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw 'Run scripts/test.ps1 first.' }
$profileDir = Join-Path $projectRoot 'build/native/idle-measurement'
New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
$profilePath = Join-Path $profileDir 'settings.ini'
@'
[App]
Version=1
[General]
RefreshRate=60
TargetApps=
'@ | Set-Content -LiteralPath $profilePath -Encoding utf8
$child = Start-Process -FilePath $executable -ArgumentList @('--settings', ('"' + $profilePath + '"')) -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 2
    $child.Refresh()
    if ($child.HasExited) { throw 'Measurement instance exited; check whether another measurement is running.' }
    $initialCpu = $child.TotalProcessorTime.TotalMilliseconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds $Seconds
    $clock.Stop()
    $child.Refresh()
    if ($child.HasExited) { throw 'Measurement instance exited unexpectedly.' }
    [pscustomobject]@{
        Configuration = $Configuration
        DurationSeconds = [math]::Round($clock.Elapsed.TotalSeconds, 2)
        WorkingSetMiB = [math]::Round($child.WorkingSet64 / 1MB, 2)
        PrivateMemoryMiB = [math]::Round($child.PrivateMemorySize64 / 1MB, 2)
        CpuPercentOfOneCore = [math]::Round(100 * ($child.TotalProcessorTime.TotalMilliseconds - $initialCpu) / $clock.Elapsed.TotalMilliseconds, 3)
        ExecutableKiB = [math]::Round((Get-Item -LiteralPath $executable).Length / 1KB, 1)
    } | ConvertTo-Json
} finally {
    if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
    $child.Dispose()
}
