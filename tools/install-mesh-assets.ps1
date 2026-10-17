# Prepare, exercise rollback in an isolated tree, then optionally install the four-resource scene.
param([Parameter(Mandatory=$true)][string]$Stage,[switch]$Install)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$game=Split-Path -Parent $repo
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$Stage=(Resolve-Path -LiteralPath $Stage).Path
$allowed=(Resolve-Path -LiteralPath (Join-Path $repo 'build/tests')).Path+[IO.Path]::DirectorySeparatorChar
if (-not $Stage.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Stage must be inside build/tests.'}
function Invoke-Risen {
    & $exe @args
    if($LASTEXITCODE -ne 0){throw "risen failed: $args"}
}
function Snapshot {
    @(Get-ChildItem -LiteralPath (Join-Path $game 'bin'),(Join-Path $game 'data') -File -Recurse |
        Where-Object {$_.Extension -in '.dll','.ini','.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.bin','._xmsh','._xcom'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash})
}
function Backup-FromOutput($Lines) {
    $line=@($Lines | Where-Object {$_ -like 'BACKUP: *'})
    if($line.Count -ne 1){throw 'Expected exactly one backup result.'}
    return $line[0].Substring(8)
}
if(!(Test-Path -LiteralPath (Join-Path $repo 'backup/bin-original/Engine.dll'))){throw 'Original game backup missing.'}
$before=Snapshot
if(!(Test-Path -LiteralPath (Join-Path $Stage 'scene.lrent'))){
    Invoke-Risen --game $game layer-create RisenMods_Assets_Test --out-dir (Join-Path $Stage 'world') | Set-Content (Join-Path $Stage 'scene-world-plan.txt')
    $env:RUSTUP_HOME='D:\Rust\rustup'
    $env:CARGO_HOME='D:\Rust\cargo'
    Push-Location (Join-Path $repo 'rust')
    try{
        & D:\Rust\cargo\bin\cargo.exe run -p risen-formats --example mesh_asset_scene -- $game $Stage
        if($LASTEXITCODE -ne 0){throw 'Scene staging failed.'}
    }finally{Pop-Location}
}
$oldScene=Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$oldSceneHash=(Get-FileHash -LiteralPath $oldScene).Hash
$bundle=Join-Path $Stage ('bundle-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $bundle | Out-Null
New-Item -ItemType Directory -Path (Join-Path $bundle 'payload'),(Join-Path $bundle 'baseline') | Out-Null
$world=Join-Path $bundle 'world-plan'
Invoke-Risen --game $game layer-create RisenMods_Assets_Test --out-dir $world | Set-Content (Join-Path $bundle 'world-plan.txt')
$manifest=Get-Content -LiteralPath (Join-Path $Stage 'assets.json') -Raw | ConvertFrom-Json
$entries=@()
function Add-Entry([string]$Relative,[string]$Source) {
    $index=$script:entries.Count
    $payload="payload/$index.bin"
    Copy-Item -LiteralPath $Source -Destination (Join-Path $bundle $payload)
    $target=Join-Path $game $Relative
    $baseline=$null
    if(Test-Path -LiteralPath $target){
        $baseline="baseline/$index.bin"
        Copy-Item -LiteralPath $target -Destination (Join-Path $bundle $baseline)
        if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $bundle $baseline)).Hash){throw 'Baseline changed during staging.'}
    }
    $script:entries+=@{path=$Relative;source=$payload;before=$baseline}
}
foreach($a in $manifest.assets){
    Add-Entry ('data/common/meshes/'+$a.name+'._xmsh') (Join-Path $Stage ($a.name+'._xmsh'))
    Add-Entry ('data/common/physics/'+$a.name+'._xcom') (Join-Path $Stage ($a.name+'._xcom'))
}
# Refuse a stale cache donor, including a newly introduced loose override.
$cache=@(Invoke-Risen --game $game vfs compiled_meshes.bin --json | ConvertFrom-Json)
if($cache.Count -ne 1){throw 'Expected one mounted compiled mesh cache.'}
$cacheFile=Join-Path $bundle 'current-cache.bin'
if(Test-Path -LiteralPath $cache[0].source){Copy-Item -LiteralPath $cache[0].source -Destination $cacheFile}
elseif($cache[0].source -match '^(.*\.p(?:ak|[0-9]{2})):(.+)$'){
    Invoke-Risen pak-extract $Matches[1] $Matches[2] $cacheFile | Out-Null
}else{throw 'Unsupported cache VFS source.'}
if((Get-FileHash -LiteralPath $cacheFile).Hash -ne (Get-FileHash -LiteralPath (Join-Path $Stage 'compiled_meshes.original.bin')).Hash){throw 'Mesh cache changed since probe generation.'}
Add-Entry 'data/common/meshes/compiled_meshes.bin' (Join-Path $Stage 'compiled_meshes.bin')
$collisionCache=@(Invoke-Risen --game $game vfs compiled_collision_meshes.bin --json | ConvertFrom-Json)
if($collisionCache.Count -ne 1){throw 'Expected one mounted collision cache.'}
$collisionFile=Join-Path $bundle 'current-collision-cache.bin'
if(Test-Path -LiteralPath $collisionCache[0].source){Copy-Item -LiteralPath $collisionCache[0].source -Destination $collisionFile}
elseif($collisionCache[0].source -match '^(.*\.p(?:ak|[0-9]{2})):(.+)$'){
    Invoke-Risen pak-extract $Matches[1] $Matches[2] $collisionFile | Out-Null
}else{throw 'Unsupported collision cache VFS source.'}
if((Get-FileHash -LiteralPath $collisionFile).Hash -ne (Get-FileHash -LiteralPath (Join-Path $Stage 'compiled_collision_meshes.original.bin')).Hash){throw 'Collision cache changed since generation.'}
Add-Entry 'data/common/physics/compiled_collision_meshes.bin' (Join-Path $Stage 'compiled_collision_meshes.bin')
foreach($file in Get-ChildItem -LiteralPath (Join-Path $world 'data') -File -Recurse){
    $relative=[IO.Path]::GetRelativePath($world,$file.FullName).Replace('\','/')
    $source=if($relative.EndsWith('.lrent')){Join-Path $Stage 'scene.lrent'}else{$file.FullName}
    Add-Entry $relative $source
}
$plan=Join-Path $bundle 'bundle.json'
@{files=$entries} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $plan -Encoding utf8
Invoke-Risen --game $game asset-install $plan --dry-run | Set-Content (Join-Path $bundle 'dry-run.txt')
# Exercise the actual CLI installer/restorer without touching the game.
$sandbox=Join-Path $bundle 'isolated-game'
New-Item -ItemType Directory -Path (Join-Path $sandbox 'bin'),(Join-Path $sandbox 'data/common') -Force | Out-Null
@('[Root]','RootPath=..','NoPhysical=false','NoPacked=true','[Physical]','data/common') | Set-Content -LiteralPath (Join-Path $sandbox 'bin/mountlist_packed.ini') -Encoding ascii
foreach($entry in $entries){if($entry.before){
    $target=Join-Path $sandbox $entry.path
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $bundle $entry.before) -Destination $target
}}
$testBackup=Backup-FromOutput @(Invoke-Risen --game $sandbox asset-install $plan)
foreach($entry in $entries){
    if((Get-FileHash -LiteralPath (Join-Path $sandbox $entry.path)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $bundle $entry.source)).Hash){throw 'Isolated install mismatch.'}
}
Invoke-Risen asset-restore $testBackup | Out-Null
Invoke-Risen asset-restore $testBackup | Out-Null
foreach($entry in $entries){
    $target=Join-Path $sandbox $entry.path
    if($entry.before){if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $bundle $entry.before)).Hash){throw 'Isolated restore mismatch.'}}
    elseif(Test-Path -LiteralPath $target){throw 'Isolated restore left a created asset.'}
}
if(Compare-Object $before (Snapshot)){throw 'Game changed during staging.'}
@{bundle=$plan;stage=$Stage;files=$entries.Count;isolatedInstallRestore=$true;oldSceneSha256=$oldSceneHash;installed=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'result.json')
Write-Output "STAGED: $plan"
if($Install){
    $backup=$null
    try{
        $backup=Backup-FromOutput @(Invoke-Risen --game $game asset-install $plan)
        foreach($entry in $entries){
            if((Get-FileHash -LiteralPath (Join-Path $game $entry.path)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $bundle $entry.source)).Hash){throw 'Installed file mismatch.'}
        }
        if((Get-FileHash -LiteralPath $oldScene).Hash -ne $oldSceneHash){throw 'Confirmed test scene changed.'}
        $after=Snapshot
        $targets=@($entries | ForEach-Object {(Join-Path $game $_.path)+' '})
        foreach($difference in @(Compare-Object $before $after)){
            $line=$difference.InputObject
            if(-not @($targets | Where-Object {$line.StartsWith($_,[StringComparison]::OrdinalIgnoreCase)}).Count){throw "Unrelated game asset changed: $line"}
        }
        Invoke-Risen --game $game entities RisenMods_Assets_Test --props --geometry --json | Set-Content -LiteralPath (Join-Path $bundle 'installed-entities.json')
        $actual=@(Get-Content -LiteralPath (Join-Path $bundle 'installed-entities.json') -Raw | ConvertFrom-Json)
        if($actual.Count -ne 5){throw 'Installed VFS scene readback failed.'}
        foreach($a in $manifest.assets){
            $visual=Invoke-Risen --game $game geometry ($a.name+'._xmsh') --json | ConvertFrom-Json
            $physical=Invoke-Risen --game $game geometry ($a.name+'._xcom') --json | ConvertFrom-Json
            if($visual.triangles -ne 8 -or $physical.meshes.Count -ne 1){throw 'Installed resource VFS readback failed.'}
        }
        $before | Set-Content -LiteralPath (Join-Path $bundle 'game-before.sha256.txt')
        $after | Set-Content -LiteralPath (Join-Path $bundle 'game-after.sha256.txt')
        @{bundle=$plan;stage=$Stage;backup=$backup;files=$entries.Count;oldSceneSha256=$oldSceneHash;installed=$true;manualTest='pending'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'installed-test.json')
    }catch{if($backup){Invoke-Risen asset-restore $backup | Out-Null};throw}
    Write-Output "INSTALLED: separate four-model scene. BACKUP: $backup"
    Write-Output "RESTORE: .\rust\target\release\risen.exe asset-restore '$backup'"
}
