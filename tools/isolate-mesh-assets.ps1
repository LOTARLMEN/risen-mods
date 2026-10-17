# Hide the old test sector and move the four custom meshes to its cleared tower site.
param([switch]$Install)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$game=Split-Path -Parent $repo
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$stage=Join-Path $repo ('build/tests/asset-isolated-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen{& $exe --game $game @args;if($LASTEXITCODE -ne 0){throw "risen failed: $args"}}
function Snapshot{
    @(Get-ChildItem -LiteralPath (Join-Path $game 'data'),(Join-Path $game 'bin') -File -Recurse |
        Where-Object {$_.Extension -in '.dll','.ini','.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.bin','._xmsh','._xcom'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName).Hash})
}
$before=Snapshot
$env:RUSTUP_HOME='D:\Rust\rustup';$env:CARGO_HOME='D:\Rust\cargo'
Push-Location (Join-Path $repo 'rust')
try{
    & D:\Rust\cargo\bin\cargo.exe run -p risen-formats --example isolate_asset_scene -- $game $stage
    if($LASTEXITCODE -ne 0){throw 'Sector staging failed.'}
}finally{Pop-Location}
$entries=@()
foreach($line in Get-Content -LiteralPath (Join-Path $stage 'changes.tsv')){
    $fields=$line.Split("`t")
    $entries+=@{path=$fields[0];source=$fields[1];before=if($fields[2]){$fields[2]}else{$null}}
}
$layer=Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_Assets_Test.lrent'
Copy-Item -LiteralPath $layer -Destination (Join-Path $stage 'scene.before.lrent')
$current=Join-Path $stage 'scene.before.lrent'
$sites=@(@(1,-37850,-4900),@(2,-37450,-4900),@(3,-37050,-4900),@(4,-37450,-4500))
foreach($site in $sites){
    $moved=Join-Path $stage ("moved-$($site[0]).lrent")
    $grounded=Join-Path $stage ("grounded-$($site[0]).lrent")
    Invoke-Risen move $current $site[0] --to $site[1] 200 $site[2] --out $moved | Out-Null
    Invoke-Risen ground $moved $site[0] --collision --surface-layer Levelmesh_Landscape_COL --samples 7 --gap 20 --max-rise 500 --out $grounded | Out-Null
    $current=$grounded
}
Copy-Item -LiteralPath $current -Destination (Join-Path $stage 'scene.after.lrent')
$entities=@(Invoke-Risen entities $current --geometry --json | ConvertFrom-Json)
if($entities.Count -ne 5){throw 'Expected only four custom entities plus root.'}
$previous=@(Invoke-Risen entities (Join-Path $stage 'scene.before.lrent') --geometry --json | ConvertFrom-Json)
if(($previous.guid -join ',') -ne ($entities.guid -join ',')){throw 'GUIDs changed.'}
foreach($i in 1..4){
    $g=Invoke-Risen geometry $current $i --json | ConvertFrom-Json
    if(-not $g.entity_collision_enabled -or $g.collision_sets[0].shapes[0].kind -ne 'triangle_mesh'){throw 'Expected active custom triangle collider.'}
    $matrix=$entities[$i].geometry.world_matrix
    if($matrix[0] -ne 1 -or $matrix[5] -ne 1 -or $matrix[10] -ne 1){throw 'Instance scale changed.'}
}
$entries+=@{path='data/common/projects/World/RisenMods/RisenMods_Assets_Test.lrent';source='scene.after.lrent';before='scene.before.lrent'}
$manifest=Join-Path $stage 'bundle.json'
@{files=$entries} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifest -Encoding utf8
Invoke-Risen asset-install $manifest --dry-run | Set-Content -LiteralPath (Join-Path $stage 'dry-run.txt')
if(Compare-Object $before (Snapshot)){throw 'Game files changed during staging.'}
$entities | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $stage 'scene-entities.json')
Write-Output "STAGED: $manifest"
if($Install){
    $output=@(Invoke-Risen asset-install $manifest)
    $backup=@($output | Where-Object {$_ -like 'BACKUP: *'})
    if($backup.Count -ne 1){throw 'Missing backup path.'}
    $backup=$backup[0].Substring(8)
    try{
        foreach($entry in $entries){
            if((Get-FileHash -LiteralPath (Join-Path $game $entry.path)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $entry.source)).Hash){throw 'Installed bytes differ.'}
        }
        $after=Snapshot
        foreach($difference in @(Compare-Object $before $after)){
            $line=$difference.InputObject
            if(-not @($entries | Where-Object {$line.StartsWith((Join-Path $game $_.path)+' ',[StringComparison]::OrdinalIgnoreCase)}).Count){throw 'Unrelated game asset changed.'}
        }
        $flags=@(Invoke-Risen world-sectors World --json | ConvertFrom-Json)
        if(($flags | Where-Object name -eq 'RisenMods_NewLayer_Test').enabled -ne $false -or
            ($flags | Where-Object name -eq 'RisenMods_Assets_Test').enabled -ne $true){throw 'Incorrect sector activation.'}
        @{stage=$stage;backup=$backup;files=$entries.Count;manualTest='pending';entities=$entities} | ConvertTo-Json -Depth 20 |
            Set-Content -LiteralPath (Join-Path $stage 'installed-test.json')
        Copy-Item -LiteralPath (Join-Path $game 'bin/mods/RisenMods.log') -Destination (Join-Path $stage 'previous-RisenMods.log')
    }catch{Invoke-Risen asset-restore $backup | Out-Null;throw}
    Write-Output "INSTALLED. RESTORE FIRST: risen asset-restore '$backup'"
}
