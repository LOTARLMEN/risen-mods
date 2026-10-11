# Packs each release mod into dist\Risen<Name>-<version>.zip with the layout of the game folder:
#   bin\version.dll, bin\mods\<mod>.dll, README.txt
# The version is taken from the first line of the mod's README.txt ("Risen Fast Start 1.1").
# Run build.bat first. Usage: powershell -File tools\package.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

foreach ($dir in Get-ChildItem (Join-Path $root 'mods') -Directory) {
    $readme = Join-Path $dir.FullName 'README.txt'
    if (-not (Test-Path $readme)) { continue } # developer-only mods have no README
    $title = (Get-Content $readme -TotalCount 1).Trim()
    if ($title -notmatch '^(.+?)\s+([\d.]+)$') { throw "README title '$title' must end with a version" }
    $zipName = ($Matches[1] -replace '\s', '') + '-' + $Matches[2] + '.zip'
    $dll = $dir.Name -replace '-', '_'

    $stage = Join-Path $env:TEMP ("risen-pack-" + $dll)
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force (Join-Path $stage 'bin\mods') | Out-Null
    Copy-Item (Join-Path $root 'build\bin\version.dll') (Join-Path $stage 'bin')
    Copy-Item (Join-Path $root "build\bin\mods\$dll.dll") (Join-Path $stage 'bin\mods')
    Copy-Item $readme $stage

    $zip = Join-Path $dist $zipName
    Remove-Item $zip -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
    Remove-Item $stage -Recurse -Force
    "packed $zipName"
}
