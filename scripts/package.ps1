param([switch]$Installer)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $executables = @('CSP-Grayscale-Viewer.exe', 'CSP-Grayscale-Viewer_Console.exe')
    foreach ($name in $executables) {
        if (-not (Test-Path -LiteralPath "build/native/Release/$name")) {
            throw 'Run scripts/test.ps1 -Configuration Release -Smoke before packaging.'
        }
    }
    New-Item -ItemType Directory -Path deploy -Force | Out-Null
    foreach ($name in $executables) {
        Copy-Item -LiteralPath "build/native/Release/$name" -Destination "deploy/$name"
    }
    Copy-Item -LiteralPath LICENSE -Destination deploy/LICENSE
    # List the files explicitly, so old Qt deployment files cannot enter the ZIP.
    $files = @('deploy/CSP-Grayscale-Viewer.exe', 'deploy/CSP-Grayscale-Viewer_Console.exe', 'deploy/LICENSE')
    $archive = 'build/native/CSP-Grayscale-Viewer-Native-Portable.zip'
    Compress-Archive -LiteralPath $files -DestinationPath $archive -Force
    Write-Output "Portable package: $archive"
    if ($Installer) {
        $compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'
        if (-not (Test-Path -LiteralPath $compiler)) { throw 'Inno Setup 6 is required to compile the installer.' }
        & $compiler /Q installer.iss
        if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
        Write-Output 'Installer: Output/CSP-Grayscale-Viewer_Setup.exe'
    }
} finally {
    Pop-Location
}
