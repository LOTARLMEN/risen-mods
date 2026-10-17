# Repair the first probe's inverted visual faces and remove intentional placement clearance.
param([switch]$Install)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$game=Split-Path -Parent $repo
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$stage=Join-Path $repo ('build/tests/asset-faces-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen{& $exe --game $game @args;if($LASTEXITCODE -ne 0){throw "risen failed: $args"}}
function Snapshot{
    @(Get-ChildItem -LiteralPath (Join-Path $game 'data'),(Join-Path $game 'bin') -File -Recurse |
        Where-Object {$_.Extension -in '.dll','.ini','.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.bin','._xmsh','._xcom'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName).Hash})
}
$before=Snapshot
$originalStage=Join-Path $repo 'build/tests/asset-probe-20261016-095221'
Copy-Item -LiteralPath (Join-Path $originalStage 'donor._xmsh') -Destination (Join-Path $stage 'donor._xmsh')
$entries=@()
foreach($suffix in 'Ramp','Stretch','Trim','Terrain'){
    $name='RisenMods_Probe_'+$suffix
    $path='data/common/meshes/'+$name+'._xmsh'
    Copy-Item -LiteralPath (Join-Path $game $path) -Destination (Join-Path $stage ($name+'._xmsh'))
    Copy-Item -LiteralPath (Join-Path $game ('data/common/physics/'+$name+'._xcom')) -Destination (Join-Path $stage ($name+'._xcom'))
    Copy-Item -LiteralPath (Join-Path $originalStage ($name+'._xcom.native')) -Destination (Join-Path $stage ($name+'._xcom.native'))
    $entries+=@{path=$path;source=$name+'.fixed._xmsh';before=$name+'._xmsh'}
}
python -I (Join-Path $repo 'tools/probe-mesh-assets.py') fix-winding $stage
if($LASTEXITCODE -ne 0){throw 'Winding repair refused.'}
$check=Join-Path $stage 'check'
New-Item -ItemType Directory -Path $check | Out-Null
Copy-Item -LiteralPath (Join-Path $stage 'donor._xmsh') -Destination (Join-Path $check 'donor._xmsh')
foreach($suffix in 'Ramp','Stretch','Trim','Terrain'){
    $name='RisenMods_Probe_'+$suffix
    Copy-Item -LiteralPath (Join-Path $stage ($name+'.fixed._xmsh')) -Destination (Join-Path $check ($name+'._xmsh'))
    foreach($ext in '._xcom','._xcom.native'){
        Copy-Item -LiteralPath (Join-Path $stage ($name+$ext)) -Destination (Join-Path $check ($name+$ext))
    }
}
$env:RUSTUP_HOME='D:\Rust\rustup';$env:CARGO_HOME='D:\Rust\cargo'
Push-Location (Join-Path $repo 'rust')
try{
    & D:\Rust\cargo\bin\cargo.exe run -p risen-formats --example mesh_asset_check -- $check
    if($LASTEXITCODE -ne 0){throw 'Independent native-winding/render/physics check failed.'}
}finally{Pop-Location}
$path='data/common/projects/World/RisenMods/RisenMods_Assets_Test.lrent'
Copy-Item -LiteralPath (Join-Path $game $path) -Destination (Join-Path $stage 'scene.before.lrent')
$current=Join-Path $stage 'scene.before.lrent'
foreach($i in 1..4){
    $out=Join-Path $stage ("ground-$i.lrent")
    Invoke-Risen ground $current $i --collision --surface-layer Levelmesh_Landscape_COL --samples 9 --check-contact --correct-height --gap -2 --max-rise 500 --out $out |
        Set-Content -LiteralPath (Join-Path $stage ("ground-$i.txt"))
    $current=$out
}
Copy-Item -LiteralPath $current -Destination (Join-Path $stage 'scene.after.lrent')
$old=@(Invoke-Risen entities (Join-Path $stage 'scene.before.lrent') --geometry --props --json | ConvertFrom-Json)
$new=@(Invoke-Risen entities $current --geometry --props --json | ConvertFrom-Json)
if($new.Count -ne 5 -or ($old.guid -join ',') -ne ($new.guid -join ',')){throw 'Scene GUID/count mismatch.'}
foreach($i in 1..4){
    if(($old[$i].sets | ConvertTo-Json -Depth 20 -Compress) -cne ($new[$i].sets | ConvertTo-Json -Depth 20 -Compress)){throw 'Collider/material/property sets changed.'}
    $matrix=$new[$i].geometry.world_matrix
    if($matrix[0] -ne 1 -or $matrix[5] -ne 1 -or $matrix[10] -ne 1){throw 'Instance scale changed.'}
    if($old[$i].position[0] -ne $new[$i].position[0] -or $old[$i].position[2] -ne $new[$i].position[2]){throw 'Placement moved horizontally.'}
}
$entries+=@{path=$path;source='scene.after.lrent';before='scene.before.lrent'}
$manifest=Join-Path $stage 'bundle.json'
@{files=$entries} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifest -Encoding utf8
Invoke-Risen asset-install $manifest --dry-run | Set-Content -LiteralPath (Join-Path $stage 'dry-run.txt')
if(Compare-Object $before (Snapshot)){throw 'Game changed during preparation.'}
Write-Output "STAGED: $manifest"
if($Install){
    $output=@(Invoke-Risen asset-install $manifest)
    $backups=@($output | Where-Object {$_ -like 'BACKUP: *'})
    if($backups.Count -ne 1){throw 'Missing backup path.'}
    $backup=$backups[0].Substring(8)
    try{
        foreach($entry in $entries){
            if((Get-FileHash -LiteralPath (Join-Path $game $entry.path)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $entry.source)).Hash){throw 'Installed file differs.'}
        }
        foreach($difference in @(Compare-Object $before (Snapshot))){
            $line=$difference.InputObject
            if(-not @($entries | Where-Object {$line.StartsWith((Join-Path $game $_.path)+' ',[StringComparison]::OrdinalIgnoreCase)}).Count){throw 'Unrelated game asset changed.'}
        }
        @{stage=$stage;backup=$backup;files=$entries.Count;manualTest='pending';geometryUnchanged=$true;renderIndicesReversed=$true;gapCm=-2;entities=$new} |
            ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json')
        Copy-Item -LiteralPath (Join-Path $game 'bin/mods/RisenMods.log') -Destination (Join-Path $stage 'previous-RisenMods.log')
    }catch{Invoke-Risen asset-restore $backup | Out-Null;throw}
    Write-Output "INSTALLED: corrected faces and grounded placement. BACKUP: $backup"
}
