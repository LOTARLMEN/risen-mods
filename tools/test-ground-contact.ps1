# Diagnose sampled placement using full geometry. All layer outputs are scratch-only.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$stage=Join-Path $repo ('build/tests/ground-contact-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0){throw "risen failed: $args"}
}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object {$_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash})
}
function Close-Array($A,$B,[double]$Tolerance=0.000001) {
    if ($A.Count -ne $B.Count){throw 'Array length differs.'}
    for ($k=0;$k -lt $A.Count;$k++) {
        if ([Math]::Abs([double]$A[$k]-[double]$B[$k]) -gt $Tolerance){throw "Coordinate differs: $k"}
    }
}
function Diagnose([string]$Name,$Arguments) {
    $dryOut=Join-Path $stage ($Name+'-dry.lrent')
    $raw=Invoke-Risen ground @Arguments --check-contact --json --dry-run --out $dryOut
    $raw | Set-Content (Join-Path $stage ($Name+'.json'))
    if (Test-Path -LiteralPath $dryOut){throw 'Diagnostic dry-run wrote a layer.'}
    $q=$raw | ConvertFrom-Json
    if ($q.placement_corrected -or -not $q.dry_run -or $q.contact.candidates.Count -lt 1){throw 'Invalid diagnostic metadata.'}
    $nearest=[double]::PositiveInfinity
    $intersections=0
    foreach ($c in $q.contact.candidates) {
        if ($c.distance_cm -lt 0 -or [double]::IsNaN($c.distance_cm) -or [double]::IsInfinity($c.distance_cm)){throw 'Invalid distance.'}
        $nearest=[Math]::Min($nearest,$c.distance_cm)
        if ($c.intersects){$intersections++}
        $witness=0.
        for ($k=0;$k -lt 3;$k++) {$witness += [Math]::Pow($c.point_a[$k]-$c.point_b[$k],2)}
        if ([Math]::Abs([Math]::Sqrt($witness)-$c.distance_cm) -gt 0.00001){throw 'Witness distance differs.'}
    }
    if ($nearest -ne $q.contact.nearest_distance_cm -or $intersections -ne $q.contact.contact_or_overlap_count){throw 'Aggregate differs from candidate results.'}
    $q
}
function Refuse([string]$Name,$Arguments) {
    $out=Join-Path $stage ($Name+'-refused.lrent')
    try {
        $ErrorActionPreference='Continue'
        & $exe --game $game ground @Arguments --out $out *> (Join-Path $stage ($Name+'-refusal.txt'))
        $code=$LASTEXITCODE
    } finally {$ErrorActionPreference='Stop'}
    if ($code -eq 0 -or (Test-Path -LiteralPath $out)){throw "Expected atomic refusal: $Name"}
}
$before=Snapshot
$before | Set-Content (Join-Path $stage 'game-before.sha256.txt')
$layer='RisenMods_NewLayer_Test'
$land='Levelmesh_Landscape_COL'
$board=Diagnose board @($layer,2,'--collision','--surface-layer',$land,'--gap',-2)
if (-not $board.intentional_embedding_requested -or $board.contact.contact_or_overlap_count -lt 1){throw 'Expected inset board contact.'}
# Enabling diagnostics must not change the serialized placement or unrelated entities.
$plain=Join-Path $stage 'plain.lrent'
$checked=Join-Path $stage 'checked.lrent'
Invoke-Risen ground $layer 2 --collision --surface-layer $land --gap -2 --out $plain | Set-Content (Join-Path $stage 'plain.txt')
Invoke-Risen ground $layer 2 --collision --surface-layer $land --gap -2 --check-contact --out $checked | Set-Content (Join-Path $stage 'checked.txt')
if ((Get-FileHash -LiteralPath $plain).Hash -ne (Get-FileHash -LiteralPath $checked).Hash){throw 'Diagnostics changed placement bytes.'}
$placed=Invoke-Risen entities $checked --geometry --json | ConvertFrom-Json
# `entities` serializes f32 directly; the diagnostic embeds f32 as JSON Value/f64.
# Compare parsed coordinates within the decimal formatting error, plus exact file hashes above.
Close-Array $board.planned_world_matrix $placed[2].geometry.world_matrix 0.00002
$guid=Diagnose board-guid @($layer,$placed[2].guid,'--collision','--surface-layer',$land,'--gap',-2)
Close-Array $guid.planned_world_matrix $board.planned_world_matrix
foreach ($c in $board.contact.candidates) {
    $external=Invoke-Risen contact $checked 2 --against $c.guid --target-layer $land --json | ConvertFrom-Json
    if ([Math]::Abs($external.distance_cm-$c.distance_cm) -gt 0.00001 -or $external.intersects -ne $c.intersects){throw 'Ground diagnosis differs from standalone contact.'}
}
$steep=Diagnose steep @($layer,33,'--collision','--align','--samples',9,'--max-rise',500,'--surface-layer',$land,'--gap',0)
if ($steep.sampled.slope_degrees -lt 20){throw 'Expected stronger slope fixture.'}
$capsuleLayer=Join-Path $stage 'chicken.lrent'
Invoke-Risen spawn Chicken --from-template --layer $layer --to -37450 500 -5700 --yaw 37 --out $capsuleLayer | Set-Content (Join-Path $stage 'spawn-chicken.txt')
$entities=Invoke-Risen entities $capsuleLayer --json | ConvertFrom-Json
$i=$entities.Count-1
$capsule=Diagnose capsule-terrain @($capsuleLayer,$i,'--collision','--align','--samples',9,'--surface-layer',$land,'--gap',-2)
foreach ($fixture in @(@('capsule',$capsuleLayer),@('sphere',(Join-Path $stage 'ore.lrent')))) {
    if ($fixture[0] -eq 'sphere') {
        Invoke-Risen spawn Object_Interact_Pick_Ore_Gold --from-template --layer $layer --to -37450 500 -5700 --out $fixture[1] | Set-Content (Join-Path $stage 'spawn-ore.txt')
    }
    $positive=Diagnose ($fixture[0]+'-plane-clear') @($fixture[1],$i,'--collision','--height',20,'--gap',2)
    if ($positive.contact.contact_or_overlap_count -ne 0 -or [Math]::Abs($positive.contact.nearest_distance_cm-2) -gt 0.0001){throw 'Expected analytic 2 cm plane clearance.'}
    $near=Diagnose ($fixture[0]+'-plane-near') @($fixture[1],$i,'--collision','--height',20,'--gap',2,'--contact-tolerance',3)
    if ($near.contact.candidates[0].intersects -or $near.contact.candidates[0].status -ne 'near'){throw 'Tolerance changed intersection classification.'}
    Close-Array $near.planned_world_matrix $positive.planned_world_matrix
    $inset=Diagnose ($fixture[0]+'-plane-inset') @($fixture[1],$i,'--collision','--height',20,'--gap',-2)
    if (-not $inset.intentional_embedding_requested -or $inset.contact.contact_or_overlap_count -ne 1){throw 'Expected analytic plane intersection.'}
}
Refuse missing-collision @($layer,2,'--check-contact','--dry-run')
Refuse json-without-dry @($layer,2,'--collision','--height',20,'--check-contact','--json')
Refuse negative-tolerance @($layer,2,'--collision','--height',20,'--check-contact','--contact-tolerance=-1')
Refuse nonfinite-tolerance @($layer,2,'--collision','--height',20,'--check-contact','--contact-tolerance','NaN')
Refuse empty-body @($layer,0,'--collision','--height',20,'--check-contact')
Refuse impossible-rise @($layer,2,'--collision','--height',2000,'--max-rise',1,'--check-contact')
$after=Snapshot
$after | Set-Content (Join-Path $stage 'game-after.sha256.txt')
if (($before -join "`n") -cne ($after -join "`n")){throw 'Game data/INI/DLL changed.'}
@{stage=$stage;board_nearest_cm=$board.contact.nearest_distance_cm;steep_nearest_cm=$steep.contact.nearest_distance_cm;
    steep_contact_count=$steep.contact.contact_or_overlap_count;capsule_nearest_cm=$capsule.contact.nearest_distance_cm;
    placement_bytes_unchanged=$true;game_hashes_unchanged=$true;installed=$false;
    cli_build=(Get-Item $exe).LastWriteTime.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $stage 'result.json')
Write-Host "PASS: ground full-contact diagnostics, native terrain/curves/plane, JSON/GUID/witnesses/tolerance/refusals; bytes/hashes unchanged. $stage"
