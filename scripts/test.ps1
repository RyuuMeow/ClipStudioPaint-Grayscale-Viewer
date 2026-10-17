param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Smoke,
    [switch]$Renderers
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
    if ($Renderers) {
        & "./build/native/$Configuration/csp_renderer_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Renderer integration tests failed.' }
    }
    if ($Smoke) {
        $testProfile = Join-Path (Get-Location) 'build/native/smoke/settings.ini'
        $executable = Join-Path (Get-Location) "build/native/$Configuration/CSP-Grayscale-Viewer.exe"
        $child = Start-Process -FilePath $executable -ArgumentList @('--settings', ('"' + $testProfile + '"'), '--smoke-test') -WindowStyle Hidden -PassThru
        if (-not $child.WaitForExit(15000)) {
            $child.Kill()
            throw 'Tray smoke test timed out.'
        }
        if ($child.ExitCode -ne 0) { throw "Tray smoke test failed: $($child.ExitCode)" }
    }
} finally {
    Pop-Location
}
