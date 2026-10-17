# Scratch-only CLI regression checks. Does not install a scene or modify game data.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$stage=Join-Path $repo ('build/tests/curved-ground-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) {throw "risen failed: $args"}
}
function Entities([string]$Layer){@(Invoke-Risen entities $Layer --geometry --props --json | ConvertFrom-Json)}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
function Close-Array($A,$B,[double]$Tolerance=0.03) {
    if ($A.Count -ne $B.Count){throw 'Array length differs.'}
    for ($k=0;$k -lt $A.Count;$k++){if ([Math]::Abs([double]$A[$k]-[double]$B[$k]) -gt $Tolerance){throw "Coordinate mismatch $k"}}
}
function Check-SelectedProps($Original,$Placed) {
    $expected=$Original.sets | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $nav=@($expected | Where-Object {$_.class -eq 'gCNavigation_PS'})
    if ($nav.Count -eq 1) {
        $old=@($nav[0].props | Where-Object {$_.name -eq 'LastUseableNavigationPosition'})
        $new=@(($Placed.sets | Where-Object {$_.class -eq 'gCNavigation_PS'}).props | Where-Object {$_.name -eq 'LastUseableNavigationPosition'})
        if ($old.Count -ne 1 -or $new.Count -ne 1){throw 'Missing synchronized navigation position.'}
        Close-Array ($new[0].value | ConvertFrom-Json) $Placed.position
        $old[0].value=$new[0].value
    }
    if (($expected | ConvertTo-Json -Depth 20 -Compress) -cne ($Placed.sets | ConvertTo-Json -Depth 20 -Compress)){throw 'Unrelated selected properties changed.'}
}
$before=Snapshot
$before | Set-Content (Join-Path $stage 'game-before.sha256.txt')
$source=Entities RisenMods_NewLayer_Test
$spawned=Join-Path $stage 'chicken.lrent'
Invoke-Risen spawn Chicken --from-template --layer RisenMods_NewLayer_Test --to -37450 500 -5700 --yaw 37 --out $spawned
$original=Entities $spawned
$i=$original.Count-1
if ($original.Count -ne $source.Count+1 -or $original[$i].name -ne 'Chicken'){throw 'Expected appended original Chicken template.'}
$g=Invoke-Risen geometry $spawned $i --json | ConvertFrom-Json
if ('capsule' -notin @($g.collision_sets.shapes.kind)){throw 'Expected original capsule collider.'}
$plane=Join-Path $stage 'plane.lrent'
Invoke-Risen ground $spawned $i --collision --height 20 --gap -2 --samples 4 --out $plane |
    Tee-Object -FilePath (Join-Path $stage 'plane.txt')
$onPlane=Entities $plane
Close-Array $onPlane[$i].geometry.world_matrix[0..11] $original[$i].geometry.world_matrix[0..11] 0.00001
Check-SelectedProps $original[$i] $onPlane[$i]
$terrain=Join-Path $stage 'terrain.lrent'
Invoke-Risen ground $spawned $i --collision --align --samples 9 --surface-layer Levelmesh_Landscape_COL --gap -2 --out $terrain |
    Tee-Object -FilePath (Join-Path $stage 'terrain.txt')
$after=Entities $terrain
Check-SelectedProps $original[$i] $after[$i]
for ($j=1;$j -lt $i;$j++) {
    if (($original[$j] | ConvertTo-Json -Depth 20 -Compress) -cne ($after[$j] | ConvertTo-Json -Depth 20 -Compress)){throw "Other entity $j changed."}
}
$again=Join-Path $stage 'repeat.lrent'
Invoke-Risen ground $terrain $i --collision --align --samples 9 --surface-layer Levelmesh_Landscape_COL --gap -2 --out $again
$repeat=Entities $again
Close-Array $after[$i].geometry.world_matrix $repeat[$i].geometry.world_matrix
$dry=Join-Path $stage 'dry-run.lrent'
Invoke-Risen ground $spawned $i --collision --height 20 --out $dry --dry-run
if (Test-Path -LiteralPath $dry){throw 'Dry-run wrote output.'}
$bad=Join-Path $stage 'must-not-exist.lrent'
try {
    $ErrorActionPreference='Continue' # Windows PowerShell turns expected native stderr into errors.
    & $exe --game $game ground $spawned $i --collision --height 2000 --max-rise 1 --out $bad *> (Join-Path $stage 'rise-refusal.txt')
    $refusalCode=$LASTEXITCODE
} finally { $ErrorActionPreference='Stop' }
if ($refusalCode -eq 0 -or (Test-Path -LiteralPath $bad)){throw 'Rise refusal failed.'}
$board=Join-Path $stage 'board-control.lrent'
Invoke-Risen ground RisenMods_NewLayer_Test 2 --collision --surface-layer Levelmesh_Landscape_COL --gap -2 --out $board
$control=Entities $board
Close-Array $source[2].geometry.world_matrix $control[2].geometry.world_matrix
$sphere=Join-Path $stage 'gold-ore.lrent'
Invoke-Risen spawn Object_Interact_Pick_Ore_Gold --from-template --layer RisenMods_NewLayer_Test --to -37450 500 -5700 --yaw 73 --out $sphere
$sphereOriginal=Entities $sphere
$si=$sphereOriginal.Count-1
$sg=Invoke-Risen geometry $sphere $si --json | ConvertFrom-Json
if ('sphere' -notin @($sg.collision_sets.shapes.kind)){throw 'Expected original sphere collider.'}
$spherePlane=Join-Path $stage 'sphere-plane.lrent'
Invoke-Risen ground $sphere $si --collision --height 20 --gap -2 --samples 4 --out $spherePlane |
    Tee-Object -FilePath (Join-Path $stage 'sphere-plane.txt')
$spherePlaced=Entities $spherePlane
Check-SelectedProps $sphereOriginal[$si] $spherePlaced[$si]
Close-Array $sphereOriginal[$si].geometry.world_matrix[0..11] $spherePlaced[$si].geometry.world_matrix[0..11] 0.00001
for ($j=1;$j -lt $si;$j++) {
    if (($sphereOriginal[$j] | ConvertTo-Json -Depth 20 -Compress) -cne ($spherePlaced[$j] | ConvertTo-Json -Depth 20 -Compress)){throw "Other sphere-scene entity $j changed."}
}
$end=Snapshot
$end | Set-Content (Join-Path $stage 'game-after.sha256.txt')
if (($before -join "`n") -cne ($end -join "`n")){throw 'Game data/INI/DLL changed.'}
@{stage=$stage;template='Chicken';sphere_template='Object_Interact_Pick_Ore_Gold';sphere_plane=$spherePlaced[$si].position;
    entity=$i;plane=$onPlane[$i].position;terrain=$after[$i].position;
    game_hashes_unchanged=$true;installed=$false;cli_build=(Get-Item $exe).LastWriteTime.ToString('o')} |
    ConvertTo-Json -Depth 6 | Set-Content (Join-Path $stage 'result.json')
Write-Host "PASS: original capsule CLI/terrain/plane/repeat/refusal/dry-run; game hashes unchanged. $stage"
