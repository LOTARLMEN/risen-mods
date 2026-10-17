# Scratch-only custom mesh/cache/cooking proof. Never installs files or launches the game.
param([string]$PhysXDirectory = 'D:\RisenModding\ed_new\Risen Editor')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$game = Split-Path -Parent $repo
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$stage = Join-Path $repo ('build/tests/asset-probe-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
$env:RUSTUP_HOME = 'D:\Rust\rustup'
$env:CARGO_HOME = 'D:\Rust\cargo'
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Snapshot {
    @(Get-ChildItem -LiteralPath (Join-Path $game 'bin'),(Join-Path $game 'data') -File -Recurse |
        Where-Object { $_.Extension -in '.dll','.ini','.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.bin','._xmsh','._xcom' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
function Extract-Mounted([string]$Name,[string]$Destination) {
    $found = @(Invoke-Risen vfs $Name --json | ConvertFrom-Json | Where-Object { (Split-Path $_.path -Leaf) -eq $Name })
    if ($found.Count -ne 1) { throw "Expected one mounted resource: $Name" }
    $source = $found[0].source
    if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination $Destination }
    elseif ($source -match '^(.*\.p(?:ak|[0-9]{2})):(.+)$') { Invoke-Risen pak-extract $Matches[1] $Matches[2] $Destination }
    else { throw "Unsupported VFS source: $source" }
}
$before = Snapshot
$before | Set-Content -LiteralPath (Join-Path $stage 'game-before.sha256.txt')
Extract-Mounted 'compiled_meshes.bin' (Join-Path $stage 'compiled_meshes.original.bin')
Extract-Mounted 'compiled_collision_meshes.bin' (Join-Path $stage 'compiled_collision_meshes.original.bin')
Extract-Mounted 'Obj_Furn_Plank_Board_01._xmsh' (Join-Path $stage 'donor._xmsh')
python -I (Join-Path $repo 'tools/probe-mesh-assets.py') prepare $stage
if ($LASTEXITCODE -ne 0) { throw 'Asset preparation failed.' }
$manifest = Get-Content -LiteralPath (Join-Path $stage 'assets.json') -Raw | ConvertFrom-Json
$material = @(Invoke-Risen vfs $manifest.material --json | ConvertFrom-Json | Where-Object { $_.path.EndsWith('/'+$manifest.material) })
if ($material.Count -ne 1) { throw 'Referenced native material is not mounted uniquely.' }
& (Join-Path $repo 'tools/build-mesh-cook-check.bat')
if ($LASTEXITCODE -ne 0) { throw 'Cooking helper build failed.' }
& (Join-Path $repo 'tools/build-xcom-native.bat')
if ($LASTEXITCODE -ne 0) { throw 'Native reader build failed.' }
foreach ($asset in $manifest.assets) {
    & (Join-Path $repo 'build/recon/mesh-cook-check.exe') (Join-Path $PhysXDirectory 'PhysXCooking.dll') (Join-Path $stage ($asset.name+'.input')) (Join-Path $stage ($asset.name+'.cooked'))
    if ($LASTEXITCODE -ne 0) { throw "Cooking failed: $($asset.name)" }
}
python -I (Join-Path $repo 'tools/probe-mesh-assets.py') finish $stage
if ($LASTEXITCODE -ne 0) { throw 'Collision wrapping failed.' }
python -I (Join-Path $repo 'tools/probe-mesh-assets.py') collision-cache $stage
if ($LASTEXITCODE -ne 0) { throw 'Collision cache preparation failed.' }
& (Join-Path $repo 'build/recon/xcom-native-check.exe') $PhysXDirectory $stage
if ($LASTEXITCODE -ne 0) { throw 'Native reference reading failed.' }
python -I (Join-Path $repo 'tools/probe-mesh-assets.py') verify $stage
if ($LASTEXITCODE -ne 0) { throw 'Authored/native triangle comparison failed.' }
Push-Location (Join-Path $repo 'rust')
try {
    foreach ($file in 'compiled_meshes.original.bin','compiled_meshes.bin','compiled_collision_meshes.original.bin','compiled_collision_meshes.bin') {
        & D:\Rust\cargo\bin\cargo.exe run -p risen-formats --example mesh_cache_check -- $game (Join-Path $stage $file)
        if ($LASTEXITCODE -ne 0) { throw 'Mesh cache round trip failed.' }
    }
    & D:\Rust\cargo\bin\cargo.exe run -p risen-formats --example mesh_asset_check -- $stage
    if ($LASTEXITCODE -ne 0) { throw 'Independent Rust visual/physical check failed.' }
} finally { Pop-Location }
$after = Snapshot
if (Compare-Object $before $after) { throw 'Game assets changed during scratch-only check.' }
$after | Set-Content -LiteralPath (Join-Path $stage 'game-after.sha256.txt')
$originalEntries = [BitConverter]::ToUInt32([IO.File]::ReadAllBytes((Join-Path $stage 'compiled_meshes.original.bin')),18)
$newEntries = [BitConverter]::ToUInt32([IO.File]::ReadAllBytes((Join-Path $stage 'compiled_meshes.bin')),18)
$triangleCount = ($manifest.assets | ForEach-Object { $_.faces.Count } | Measure-Object -Sum).Sum
@{stage=$stage;assets=$manifest.assets.Count;nativeTriangles=$triangleCount;originalCacheEntries=$originalEntries;newCacheEntries=$newEntries;gameUnchanged=$true;installed=$false;handsOnConfirmed=$false} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'result.json') -Encoding utf8
Write-Output "PASSED: $stage/result.json"
