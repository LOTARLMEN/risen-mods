# Read-only collision corpus + CLI export checks. Optional independent PhysX 2.x comparison.
param([string]$PhysXDirectory)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$stage = Join-Path $repo ('build/tests/xcom-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
$env:RUSTUP_HOME = 'D:\Rust\rustup'
$env:CARGO_HOME = 'D:\Rust\cargo'
$cargo = 'D:\Rust\cargo\bin\cargo.exe'
function Invoke-Risen {
    $result = & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
    $result
}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' -or $_.Name -eq 'physics.pak' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
$before = Snapshot
Push-Location (Join-Path $repo 'rust')
try {
    $pak = Join-Path $game 'data/common/physics.pak'
    $corpus = Join-Path $stage 'corpus'
    & $cargo run -p risen-formats --example xcom_check -- $pak --extract $corpus
    if ($LASTEXITCODE -ne 0) { throw 'Collision corpus check/extraction failed.' }
    if ($PhysXDirectory) {
        & cmd /c (Join-Path $repo 'tools/build-xcom-native.bat')
        if ($LASTEXITCODE -ne 0) { throw 'Native reference tool build failed.' }
        & (Join-Path $repo 'build/recon/xcom-native-check.exe') $PhysXDirectory $corpus
        if ($LASTEXITCODE -ne 0) { throw 'Native reference read failed.' }
        & $cargo run -p risen-formats --example xcom_check -- $pak $corpus
        if ($LASTEXITCODE -ne 0) { throw 'Native geometry comparison failed.' }
    }
} finally { Pop-Location }
function Check-Obj([string]$Source, [string]$Name) {
    $obj = Join-Path $stage ($Name + '.obj')
    $report = Invoke-Risen geometry $Source --json --obj $obj | ConvertFrom-Json
    if ($report.error) { throw "Resource refused: $($report.error)" }
    $vertices = 0; $triangles = 0; $groups = 0
    foreach ($line in [IO.File]::ReadLines($obj)) {
        if ($line.StartsWith('v ')) { $vertices++ }
        elseif ($line.StartsWith('f ')) {
            $triangles++
            foreach ($index in $line.Substring(2).Split(' ')) {
                if ([int]$index -lt 1 -or [int]$index -gt $report.vertices) { throw 'OBJ face index outside vertices.' }
            }
        } elseif ($line.StartsWith('o ')) { $groups++ }
    }
    if ($vertices -ne $report.vertices -or $triangles -ne $report.triangles -or $groups -ne $report.meshes.Count) {
        throw "OBJ counts differ for $Source"
    }
    if ($report.units -ne 'cm' -or $report.up_axis -ne 'Y') { throw 'Coordinate convention differs.' }
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $stage ($Name + '.json'))
    Write-Host "$Source -> $vertices vertices / $triangles triangles / $groups groups"
    $report
}
$triangle = Check-Obj 'DonCamp_01_L01_COL._xcom' 'multi-triangle'
if ($triangle.meshes.Count -lt 2) { throw 'Expected multiple triangle meshes.' }
$convex = Check-Obj 'Obj_Dec_Ship_02_COL_CV._xcom' 'convex'
if ($convex.meshes[0].kind -ne 'convex_hull' -or $convex.vertices -ne 64 -or $convex.triangles -ne 124) {
    throw 'Convex resource differs.'
}
$template = Invoke-Risen geometry Obj_Nat_DeadTree_Root_01 --json | ConvertFrom-Json
$selected = $template.collision_sets[0].shapes[0].resource.selected_mesh
if ($selected.vertices -ne 14 -or $selected.triangles -ne 24 -or $selected.resource_index -ne 0) {
    throw 'Template collision resource/index resolution differs.'
}
$obj = Join-Path $stage 'convex.obj'
$objHash = (Get-FileHash -LiteralPath $obj).Hash
& $exe --game $game geometry Obj_Dec_Ship_02_COL_CV._xcom --obj $obj *> (Join-Path $stage 'overwrite-refusal.txt')
if ($LASTEXITCODE -eq 0 -or (Get-FileHash -LiteralPath $obj).Hash -ne $objHash) { throw 'OBJ overwrite was not refused.' }
$broken = Join-Path $stage 'broken._xcom'
$badObj = Join-Path $stage 'broken.obj'
$bytes = [IO.File]::ReadAllBytes((Join-Path $stage 'corpus/Obj_Dec_Ship_02_COL_CV._xcom'))
[IO.File]::WriteAllBytes($broken, $bytes[0..($bytes.Length-2)])
& $exe --game $game geometry $broken --obj $badObj *> (Join-Path $stage 'truncation-refusal.txt')
if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $badObj)) { throw 'Truncated xcom produced an export.' }
$boardObj = Join-Path $stage 'board-render.obj'
$board = Invoke-Risen geometry RisenMods_NewLayer_Test 2 --json --obj $boardObj | ConvertFrom-Json
if ($board.collision_sets[0].shapes[0].kind -ne 'box' -or $board.resources[0].kind -ne 'static_render_mesh') {
    throw 'Existing primitive/render inspection changed.'
}
$after = Snapshot
if (Compare-Object $before $after) { throw 'Game data/INI/DLL/physics.pak SHA256 changed.' }
@{
    stage = $stage; native_comparison = [bool]$PhysXDirectory
    game_hashes_unchanged = $true; game_launched = $false
    cli = (Get-Item -LiteralPath $exe).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'result.json')
Write-Output "XCOM corpus and CLI checks PASSED: $stage"
