# Scratch-only full-geometry height correction checks. Never installs or launches the game.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$stage=Join-Path $repo ('build/tests/height-correction-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0){throw "risen failed: $args"}
}
function Entities([string]$Layer){@(Invoke-Risen entities $Layer --geometry --props --json | ConvertFrom-Json)}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object {$_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash})
}
function Close-Array($A,$B,[double]$Tolerance=0.00005) {
    if ($A.Count -ne $B.Count){throw 'Array length differs.'}
    for ($k=0;$k -lt $A.Count;$k++) {
        if ([Math]::Abs([double]$A[$k]-[double]$B[$k]) -gt $Tolerance){throw "Coordinate differs: $k"}
    }
}
function Correct([string]$Name,$Arguments) {
    $out=Join-Path $stage ($Name+'-dry.lrent')
    $raw=Invoke-Risen ground @Arguments --collision --check-contact --correct-height --json --dry-run --out $out
    $raw | Set-Content (Join-Path $stage ($Name+'.json'))
    if (Test-Path -LiteralPath $out){throw 'Dry-run wrote an output layer.'}
    $q=$raw | ConvertFrom-Json
    $c=$q.height_correction
    if (-not $q.placement_corrected -or -not $c.native_pose_validated -or $c.iterations -gt $c.max_iterations){throw 'Missing validated correction.'}
    if ($c.zero_gap_distance_cm -le 0 -or $c.zero_gap_distance_cm -gt $c.tolerance_cm){throw 'Zero-gap pose is not clear and within tolerance.'}
    if ($q.planned_world_matrix[13] -lt $c.end_world_y_cm -or $q.planned_world_matrix[13] -gt $c.start_world_y_cm){throw 'Original search limits exceeded.'}
    if ($q.requested_vertical_gap_cm -ge 0 -and $q.contact.contact_or_overlap_count -ne 0){throw 'Nonnegative-gap correction left intersection.'}
    $q
}
function Refuse([string]$Name,$Arguments,[string]$Reason='') {
    $out=Join-Path $stage ($Name+'-refused.lrent')
    $log=Join-Path $stage ($Name+'-refusal.txt')
    try {
        $ErrorActionPreference='Continue'
        & $exe --game $game ground @Arguments --out $out *> $log
        $code=$LASTEXITCODE
    } finally {$ErrorActionPreference='Stop'}
    if ($code -eq 0 -or (Test-Path -LiteralPath $out)){throw "Expected atomic refusal: $Name"}
    if ($Reason -and -not ((Get-Content -LiteralPath $log -Raw).Contains($Reason))){throw "Unexpected refusal reason: $Name"}
}
function Check-SelectedProps($Original,$Placed) {
    $expected=$Original.sets | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $nav=@($expected | Where-Object {$_.class -eq 'gCNavigation_PS'})
    if ($nav.Count -eq 1) {
        $old=@($nav[0].props | Where-Object {$_.name -eq 'LastUseableNavigationPosition'})
        $new=@(($Placed.sets | Where-Object {$_.class -eq 'gCNavigation_PS'}).props | Where-Object {$_.name -eq 'LastUseableNavigationPosition'})
        if ($old.Count -ne 1 -or $new.Count -ne 1){throw 'Missing navigation position.'}
        Close-Array ($new[0].value | ConvertFrom-Json) $Placed.position 0.001
        $old[0].value=$new[0].value
    }
    if (($expected | ConvertTo-Json -Depth 20 -Compress) -cne ($Placed.sets | ConvertTo-Json -Depth 20 -Compress)){throw 'Unrelated selected properties changed.'}
}
$before=Snapshot
$before | Set-Content (Join-Path $stage 'game-before.sha256.txt')
$layer='RisenMods_NewLayer_Test'
$land='Levelmesh_Landscape_COL'
$original=Entities $layer
$board=Correct board @($layer,2,'--surface-layer',$land)
$steep=Correct steep @($layer,33,'--surface-layer',$land,'--align','--samples',9,'--max-rise',500)
$positive=Correct positive-gap @($layer,2,'--surface-layer',$land,'--gap',5)
$negative=Correct negative-gap @($layer,2,'--surface-layer',$land,'--gap',-2)
if ([Math]::Abs($positive.planned_world_matrix[13]-$board.planned_world_matrix[13]-5) -gt 0.0001){throw 'Positive gap is not a vertical translation after contact.'}
if ([Math]::Abs($negative.planned_world_matrix[13]-$board.planned_world_matrix[13]+2) -gt 0.0001 -or $negative.contact.contact_or_overlap_count -lt 1){throw 'Intentional embedding was not retained.'}
$reportOnly=Correct reporting-tolerance @($layer,2,'--surface-layer',$land,'--contact-tolerance',100)
Close-Array $board.planned_world_matrix $reportOnly.planned_world_matrix 0.000001
$fine=Correct finer-sweep @($layer,2,'--surface-layer',$land,'--sweep-tolerance',0.001)
if ($fine.height_correction.zero_gap_distance_cm -gt 0.001){throw 'Finer sweep tolerance ignored.'}
$placedLayer=Join-Path $stage 'board-placed.lrent'
Invoke-Risen ground $layer 2 --collision --check-contact --correct-height --surface-layer $land --out $placedLayer | Set-Content (Join-Path $stage 'board-placement.txt')
$placed=Entities $placedLayer
Close-Array $placed[2].geometry.world_matrix $board.planned_world_matrix
for ($j=1;$j -lt $original.Count;$j++) {
    if ($j -in 2,3) {
        Check-SelectedProps $original[$j] $placed[$j]
    } elseif (($original[$j] | ConvertTo-Json -Depth 20 -Compress) -cne ($placed[$j] | ConvertTo-Json -Depth 20 -Compress)){throw "Unrelated entity changed: $j"}
}
Close-Array $placed[3].geometry.local_matrix $original[3].geometry.local_matrix 0.000001
$expected=New-Object double[] 16
foreach ($r in 0..3){foreach ($c in 0..3){foreach ($k in 0..3){$expected[4*$r+$c]+=$placed[3].geometry.local_matrix[4*$r+$k]*$placed[2].geometry.world_matrix[4*$k+$c]}}}
Close-Array $expected $placed[3].geometry.world_matrix 0.01
$repeat=Correct repeat @($placedLayer,2,'--surface-layer',$land)
Close-Array $repeat.planned_world_matrix $board.planned_world_matrix 0.01
foreach ($c in $board.contact.candidates) {
    $q=Invoke-Risen contact $placedLayer 2 --against $c.guid --target-layer $land --json | ConvertFrom-Json
    if ($q.intersects -or [Math]::Abs($q.distance_cm-$c.distance_cm) -gt 0.000001){throw 'Serialized output differs from diagnostic contact.'}
}
$capsuleLayer=Join-Path $stage 'chicken.lrent'
Invoke-Risen spawn Chicken --from-template --layer $layer --to -37450 500 -5700 --yaw 37 --out $capsuleLayer | Set-Content (Join-Path $stage 'spawn-chicken.txt')
$chicken=Entities $capsuleLayer
$i=$chicken.Count-1
$capsule=Correct capsule-terrain @($capsuleLayer,$i,'--surface-layer',$land,'--align','--samples',9)
$capsuleOut=Join-Path $stage 'chicken-placed.lrent'
Invoke-Risen ground $capsuleLayer $i --collision --check-contact --correct-height --surface-layer $land --align --samples 9 --out $capsuleOut | Set-Content (Join-Path $stage 'capsule-placement.txt')
$chickenPlaced=Entities $capsuleOut
Check-SelectedProps $chicken[$i] $chickenPlaced[$i]
Close-Array $chickenPlaced[$i].geometry.world_matrix $capsule.planned_world_matrix
for ($j=1;$j -lt $i;$j++) {
    if (($chicken[$j] | ConvertTo-Json -Depth 20 -Compress) -cne ($chickenPlaced[$j] | ConvertTo-Json -Depth 20 -Compress)){throw 'Capsule placement changed another entity.'}
}
$oreLayer=Join-Path $stage 'ore.lrent'
Invoke-Risen spawn Object_Interact_Pick_Ore_Gold --from-template --layer $layer --to -37450 500 -5700 --out $oreLayer | Set-Content (Join-Path $stage 'spawn-ore.txt')
foreach ($fixture in @(@('capsule',$capsuleLayer),@('sphere',$oreLayer))) {
    $clear=Correct ($fixture[0]+'-plane') @($fixture[1],$i,'--height',20)
    $gap=Correct ($fixture[0]+'-plane-gap') @($fixture[1],$i,'--height',20,'--gap',2)
    if ([Math]::Abs($gap.planned_world_matrix[13]-$clear.planned_world_matrix[13]-2) -gt 0.0001){throw 'Plane gap translation differs.'}
}
# A crate above the old foot-only search range overlaps the upper starting body's top.
# Expanded full-path candidate selection must find it and refuse BEFORE writing.
$obstacle=Join-Path $stage 'overhead.lrent'
Invoke-Risen spawn Obj_Dec_Crate_01 --from-template --layer $land --to -37450 180 -5700 --out $obstacle | Set-Content (Join-Path $stage 'spawn-obstacle.txt')
Refuse initial-overhead @($layer,2,'--collision','--check-contact','--correct-height','--surface-layer',$obstacle) 'starts touching/overlapping'
$distant=Join-Path $stage 'distant.lrent'
Invoke-Risen move $layer 2 --to -37450 1000000 -5700 --out $distant | Set-Content (Join-Path $stage 'move-distant.txt')
Refuse native-precision @($distant,2,'--collision','--check-contact','--correct-height','--height',1000000,'--sweep-tolerance',0.000001) 'f32 precision'
Refuse missing-diagnostics @($layer,2,'--collision','--correct-height','--dry-run')
Refuse bad-tolerance @($layer,2,'--collision','--check-contact','--correct-height','--height',20,'--sweep-tolerance',0)
Refuse nonfinite-tolerance @($layer,2,'--collision','--check-contact','--correct-height','--height',20,'--sweep-tolerance','NaN')
Refuse empty-body @($layer,0,'--collision','--check-contact','--correct-height','--height',20)
$after=Snapshot
$after | Set-Content (Join-Path $stage 'game-after.sha256.txt')
if (($before -join "`n") -cne ($after -join "`n")){throw 'Game data/INI/DLL changed.'}
@{stage=$stage;board_distance_cm=$board.contact.nearest_distance_cm;board_queries=$board.height_correction.iterations;
    steep_distance_cm=$steep.contact.nearest_distance_cm;steep_queries=$steep.height_correction.iterations;
    capsule_distance_cm=$capsule.contact.nearest_distance_cm;game_hashes_unchanged=$true;installed=$false;
    cli_build=(Get-Item $exe).LastWriteTime.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $stage 'result.json')
Write-Host "PASS: height correction/native board/steep/capsule/sphere, signed vertical gaps, serialized contacts/subtrees/nav, full-path obstacles/precision/refusals; game hashes unchanged. $stage"
