param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Smoke,
    [switch]$Renderers,
    [switch]$ProfileRenderers
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    cmake --preset windows
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    cmake --build build/native --config $Configuration --parallel 2 -- /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    ctest --test-dir build/native -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Regression tests failed.' }
    if ($Renderers -or $ProfileRenderers) {
        $rendererArgs = @()
        if ($ProfileRenderers) { $rendererArgs += '--profile' }
        & "./build/native/$Configuration/csp_renderer_tests.exe" @rendererArgs
        if ($LASTEXITCODE -ne 0) { throw 'Renderer integration tests failed.' }
    }
    if ($Smoke) {
        $testProfile = Join-Path (Get-Location) 'build/native/smoke/settings.ini'
        $executable = Join-Path (Get-Location) "build/native/$Configuration/CSP-Grayscale-Viewer.exe"
        $smokeStarted = [DateTime]::UtcNow
        $child = Start-Process -FilePath $executable -ArgumentList @('--settings', ('"' + $testProfile + '"'), '--smoke-test') -WindowStyle Hidden -PassThru
        if (-not $child.WaitForExit(15000)) {
            $child.Kill()
            throw 'Tray smoke test timed out.'
        }
        if ($child.ExitCode -ne 0) { throw "Tray smoke test failed: $($child.ExitCode)" }
        $diagnostics = Join-Path (Split-Path -Parent $testProfile) 'diagnostics.log'
        if (-not (Test-Path -LiteralPath $diagnostics) -or
            (Get-Item -LiteralPath $diagnostics).LastWriteTimeUtc -lt $smokeStarted -or
            [IO.File]::ReadAllText($diagnostics) -notmatch '\d{2}:\d{2}:\d{2}\.\d{3} \[INFO\]') {
            throw 'Tray exit did not preserve timestamped session diagnostics.'
        }
        $diagnosticText = [IO.File]::ReadAllText($diagnostics)
        if (-not $diagnosticText.Contains("Executable: $executable") -or
            $diagnosticText -notmatch '(?m)^Build: .+' -or
            -not $diagnosticText.Contains("Settings: $testProfile")) {
            throw 'Diagnostics did not identify the running build and settings profile.'
        }
    }
} finally {
    Pop-Location
}
