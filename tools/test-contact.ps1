# Read-only contact queries plus scratch-only fixtures; never installs game data.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$stage=Join-Path $repo ('build/tests/contact-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0){throw "risen failed: $args"}
}
function Query([string]$Name,$Arguments) {
    $raw=Invoke-Risen contact @Arguments --json
    $raw | Set-Content (Join-Path $stage ($Name+'.json'))
    $q=$raw | ConvertFrom-Json
    if ($q.distance_cm -lt 0 -or [double]::IsNaN($q.distance_cm) -or [double]::IsInfinity($q.distance_cm)){throw 'Invalid contact distance.'}
    $length=0.
    for ($k=0;$k -lt 3;$k++) {$length += [Math]::Pow($q.point_a[$k]-$q.point_b[$k],2)}
    if ([Math]::Abs([Math]::Sqrt($length)-$q.distance_cm) -gt 0.00001){throw 'Witness distance differs.'}
    $q
}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object {$_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash})
}
function Refuse([string]$Name,$Arguments) {
    try {
        $ErrorActionPreference='Continue'
        & $exe --game $game contact @Arguments --json *> (Join-Path $stage ($Name+'.txt'))
        $code=$LASTEXITCODE
    } finally {$ErrorActionPreference='Stop'}
    if ($code -eq 0){throw "Expected refusal: $Name"}
}
$before=Snapshot
$before | Set-Content (Join-Path $stage 'game-before.sha256.txt')
$layer='RisenMods_NewLayer_Test'
$self=Query self @($layer,2,'--against',2)
if (-not $self.intersects -or $self.distance_cm -ne 0){throw 'Self-contact failed.'}
$land=Query board-landscape @($layer,2,'--against',113,'--target-layer','Levelmesh_Landscape_COL')
if (-not $land.intersects){throw 'Inset board should intersect physical landscape.'}
if ($land.solid_shapes_a[0] -ne $true -or $land.solid_shapes_b -contains $true){throw 'Native volume/surface semantics lost.'}
$raised=Query raised-board @($layer,2,'--against',113,'--target-layer','Levelmesh_Landscape_COL','--offset',0,100,0)
if ($raised.intersects -or $raised.distance_cm -le 0){throw 'Raised board still intersects landscape.'}
$near=Query raised-with-tolerance @($layer,2,'--against',113,'--target-layer','Levelmesh_Landscape_COL','--offset',0,100,0,'--tolerance',($raised.distance_cm+1).ToString([Globalization.CultureInfo]::InvariantCulture))
if (-not $near.within_tolerance -or $near.intersects){throw 'Tolerance must not change intersection classification.'}
$pair=Query board-barrel @($layer,2,'--against',3)
$reverse=Query barrel-board @($layer,3,'--against',2)
if ([Math]::Abs($pair.distance_cm-$reverse.distance_cm) -gt 0.00001 -or $pair.intersects -ne $reverse.intersects){throw 'Query is not symmetric.'}
$guid=Invoke-Risen entities $layer --json | ConvertFrom-Json
$byGuid=Query guid @($layer,$guid[2].guid,'--against',$guid[3].guid)
if ([Math]::Abs($pair.distance_cm-$byGuid.distance_cm) -gt 0.00001){throw 'GUID selection changed result.'}
$spawn=Join-Path $stage 'chicken.lrent'
Invoke-Risen spawn Chicken --from-template --layer $layer --to -37450 500 -5700 --out $spawn
$entities=Invoke-Risen entities $spawn --json | ConvertFrom-Json
$i=$entities.Count-1
$capsule=Query capsule-self @($spawn,$i,'--against',$i)
if (-not $capsule.intersects){throw 'Native capsule self-overlap failed.'}
$capsuleGap=Query capsule-offset @($spawn,$i,'--against',$i,'--offset',1000,0,0)
if ($capsuleGap.intersects -or $capsuleGap.distance_cm -le 0){throw 'Native capsule offset failed.'}
$sphereLayer=Join-Path $stage 'ore.lrent'
Invoke-Risen spawn Object_Interact_Pick_Ore_Gold --from-template --layer $layer --to -37450 500 -5700 --out $sphereLayer
$sphereSelf=Query sphere-self @($sphereLayer,$i,'--against',$i)
$sphereGap=Query sphere-offset @($sphereLayer,$i,'--against',$i,'--offset',1000,0,0)
if (-not $sphereSelf.intersects -or $sphereGap.intersects -or $sphereGap.distance_cm -le 0){throw 'Native sphere contact failed.'}
Refuse invalid-index @($layer,999999,'--against',2)
Refuse empty-root @($layer,0,'--against',2)
Refuse negative-tolerance @($layer,2,'--against',3,'--tolerance','-1')
Refuse nonfinite-offset @($layer,2,'--against',3,'--offset','NaN',0,0)
Refuse nonfinite-tolerance @($layer,2,'--against',3,'--tolerance','NaN')
$after=Snapshot
$after | Set-Content (Join-Path $stage 'game-after.sha256.txt')
if (($before -join "`n") -cne ($after -join "`n")){throw 'Game data/INI/DLL changed.'}
@{stage=$stage;board_landscape_intersects=$land.intersects;raised_distance_cm=$raised.distance_cm;
    board_barrel_distance_cm=$pair.distance_cm;capsule_offset_distance_cm=$capsuleGap.distance_cm;
    sphere_offset_distance_cm=$sphereGap.distance_cm;game_hashes_unchanged=$true;installed=$false;
    cli_build=(Get-Item $exe).LastWriteTime.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $stage 'result.json')
Write-Host "PASS: contact CLI, native box/capsule/sphere/XCOM, offsets/tolerance/GUID/refusals; game hashes unchanged. $stage"
