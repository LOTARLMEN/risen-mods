param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$game = Split-Path -Parent $repo
$bin = Join-Path $game 'bin'
$exe = Join-Path $bin 'Risen.exe'
function Assert-Closed {
    if (Get-Process Risen -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }) {
        throw 'Close the actual game before deploying diagnostics.'
    }
}
Assert-Closed
if (!(Test-Path -LiteralPath (Join-Path $repo 'backup/bin-original/Engine.dll'))) { throw 'Original game backup is missing.' }
$source = Join-Path $repo 'build/bin/version.dll'
if (!(Test-Path -LiteralPath $source)) { throw 'Build version.dll first.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $repo "backup/world-trace-$stamp"
New-Item -ItemType Directory -Path $backup | Out-Null
$targets = @('version.dll', 'mods/loader.ini')
function Inventory {
    $result = @{}
    foreach ($root in @($bin, (Join-Path $game 'data'))) {
        foreach ($file in Get-ChildItem -LiteralPath $root -File -Recurse) {
            # Logs/cache may be written independently; protect deployed assets and every INI.
            if ($file.Extension -in @('.dll', '.ini', '.lrent')) {
                $relative = [IO.Path]::GetRelativePath($game, $file.FullName)
                $result[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            }
        }
    }
    return $result
}
$before = Inventory
$entries = @()
foreach ($relative in $targets) {
    $target = Join-Path $bin $relative
    $copy = Join-Path $backup $relative
    $exists = Test-Path -LiteralPath $target
    if ($exists) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $copy) | Out-Null
        Copy-Item -LiteralPath $target -Destination $copy
        if ((Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $target).Hash) { throw 'Backup hash mismatch.' }
    }
    $entries += [pscustomobject]@{ relative = $relative; existed = $exists; backup = $copy }
}
# Preserve the exact loose scene accompanying this diagnostic build.
$scene = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
if (Test-Path -LiteralPath $scene) { Copy-Item -LiteralPath $scene -Destination (Join-Path $backup 'test-layer.lrent') }
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class WorldTraceIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
}
'@
try {
    Assert-Closed
    Copy-Item -LiteralPath $source -Destination (Join-Path $bin 'version.dll') -Force
    $ini = Join-Path $bin 'mods/loader.ini'
    foreach ($key in @('WorldTrace', 'DumpFirstChance')) {
        if (![WorldTraceIni]::WritePrivateProfileString('Diagnostics', $key, '1', $ini)) { throw "Cannot write $key" }
    }
    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath (Join-Path $bin 'version.dll')).Hash) { throw 'Deployed DLL mismatch.' }
    $after = Inventory
    $allowed = @('bin\version.dll', 'bin\mods\loader.ini')
    foreach ($path in @($before.Keys) + @($after.Keys) | Sort-Object -Unique) {
        if ($before[$path] -ne $after[$path] -and $path -notin $allowed) { throw "Unexpected changed asset: $path" }
    }
    $manifest = [ordered]@{
        time = (Get-Date).ToString('o'); version = 'Risen mod loader 1.2 diagnostics';
        dll = (Get-Item -LiteralPath (Join-Path $bin 'version.dll')).LastWriteTime.ToString('o');
        sha256 = (Get-FileHash -LiteralPath $source).Hash; entries = $entries;
        before = $before; after = $after
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
    Write-Output "Deployed loader only; backup: $backup"
    Get-Item -LiteralPath $source, (Join-Path $bin 'version.dll') | Select-Object FullName, LastWriteTime, Length
} catch {
    $failure = $_
    Assert-Closed
    foreach ($entry in $entries) {
        $target = Join-Path $bin $entry.relative
        if ($entry.existed) { Copy-Item -LiteralPath $entry.backup -Destination $target -Force }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target }
    }
    throw $failure
}
