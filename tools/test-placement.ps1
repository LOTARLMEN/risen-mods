# Offline verification of geometry exports, surface grounding and face snapping.
# Every output is under build/tests. No game file is installed by this script.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$stage = Join-Path $repo ('build/tests/placement-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Entities([string]$Layer) { Invoke-Risen entities $Layer --geometry --json | ConvertFrom-Json }
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
$before = Snapshot
try {
    $info = Invoke-Risen geometry Obj_Furn_Plank_Board_01 --json --obj (Join-Path $stage 'board.obj') | ConvertFrom-Json
    if ($info.rigid_body -or $info.resources[0].triangles -ne 60 -or $info.collision_sets[0].shapes[0].kind -ne 'box') {
        throw 'Wrong board geometry/collision metadata.'
    }
    $wolf = Invoke-Risen geometry Wolf_Black --json --obj (Join-Path $stage 'wolf-bind-pose.obj') | ConvertFrom-Json
    if (!$wolf.rigid_body -or $wolf.resources[0].nodes.Count -lt 10 -or $wolf.resources[0].triangles -lt 100) {
        throw 'Wrong actor geometry metadata.'
    }
    $original = Entities 'RisenMods_NewLayer_Test'
    if ($original.Count -ne 6) { throw 'Expected the installed nested/yaw test layer.' }
    $layer = Join-Path $stage 'grounded.lrent'
    Invoke-Risen ground RisenMods_NewLayer_Test 2 --surface-layer Levelmesh_Landscape --gap -2 --out $layer
    $afterA = Entities $layer
    $shiftA = $afterA[2].position[1] - $original[2].position[1]
    if ($shiftA -ge -1) { throw 'Ground did not lower the hovering board.' }
    if ([Math]::Abs($afterA[3].position[1] - $original[3].position[1] - $shiftA) -gt 0.001) { throw 'Barrel did not follow board.' }
    foreach ($i in @(1,4,5)) {
        if (($afterA[$i] | ConvertTo-Json -Depth 8 -Compress) -cne ($original[$i] | ConvertTo-Json -Depth 8 -Compress)) {
            throw 'Ground modified an unrelated entity.'
        }
    }
    Invoke-Risen ground $layer 4 --surface-layer Levelmesh_Landscape --gap -2 --out $layer
    $after = Entities $layer
    $shiftB = $after[4].position[1] - $original[4].position[1]
    if ([Math]::Abs($after[5].position[1] - $original[5].position[1] - $shiftB) -gt 0.001) { throw 'Second barrel did not follow board.' }
    $snapped = Join-Path $stage 'snapped.lrent'
    Invoke-Risen snap RisenMods_NewLayer_Test 3 --against 2 --side top --gap -5 --out $snapped
    $snappedEntities = Entities $snapped
    $newBottom = $snappedEntities[3].geometry.world_box[1]
    $boardTop = $snappedEntities[2].geometry.world_box[4]
    if ([Math]::Abs($newBottom - ($boardTop - 5)) -gt 0.02) { throw 'Negative-gap contact mismatch.' }
    Invoke-Risen ground RisenMods_NewLayer_Test 2 --height 40 --out (Join-Path $stage 'plane.lrent')
    $plane = Entities (Join-Path $stage 'plane.lrent')
    if ([Math]::Abs($plane[2].geometry.world_box[1] - 40) -gt 0.01) { throw 'Plane height mismatch.' }
    Invoke-Risen ground RisenMods_NewLayer_Test 2 --dry-run
    Write-Output "GEOMETRY + GROUND + SNAP PASSED. Grounded layer: $layer; A shift $shiftA cm, B shift $shiftB cm."
} finally {
    if (Compare-Object $before (Snapshot)) { throw 'Game data/INI/DLL hashes changed.' }
    Write-Output 'Game data, INI and DLL SHA256 unchanged.'
}
