param([string]$Tag = '')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $project = [IO.File]::ReadAllText((Join-Path (Get-Location) 'CMakeLists.txt'))
    if ($project -notmatch 'project\(CSP-Grayscale-Viewer VERSION (\d+\.\d+\.\d+)') {
        throw 'Cannot determine the CMake project version.'
    }
    $version = $Matches[1]
    if ($Tag -and $Tag -cne "v$version") { throw "Tag $Tag does not match project version v$version." }
    $escaped = [regex]::Escape($version)
    $resource = [IO.File]::ReadAllText((Join-Path (Get-Location) 'resources/app.rc'))
    $numeric = $version.Replace('.', ',') + ',0'
    foreach ($field in @('FILEVERSION', 'PRODUCTVERSION')) {
        if ($resource -notmatch "(?m)^\s*$field $numeric\s*$") { throw "$field does not match $version." }
    }
    foreach ($field in @('FileVersion', 'ProductVersion')) {
        if ($resource -notmatch ('VALUE "' + $field + '", "' + $escaped + '\\0"')) {
            throw "$field does not match $version."
        }
    }
    $installer = [IO.File]::ReadAllText((Join-Path (Get-Location) 'installer.iss'))
    if ($installer -notmatch "(?m)^AppVersion=$escaped\r?$") { throw 'Installer version does not match.' }
    [xml]$manifest = [IO.File]::ReadAllText((Join-Path (Get-Location) 'resources/app.manifest'))
    if ($manifest.assembly.assemblyIdentity.version -ne "$version.0") { throw 'Manifest version does not match.' }
    $changelog = [IO.File]::ReadAllText((Join-Path (Get-Location) 'CHANGELOG.md'))
    if ($changelog -notmatch "(?ms)^## $escaped - \d{4}-\d{2}-\d{2}\r?\n(.*?)(?=^## |\z)") {
        throw "No changelog entry for $version."
    }
    $notes = $Matches[1].Trim() + [Environment]::NewLine
    New-Item -ItemType Directory -Path 'build/native' -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path (Get-Location) 'build/native/release-notes.md'),
        $notes, [Text.UTF8Encoding]::new($false))
    Write-Output "Release v${version}: versions agree; notes prepared from CHANGELOG.md."
} finally {
    Pop-Location
}
