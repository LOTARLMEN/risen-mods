# Install only the developer DLL/INI, with the game closed and an explicit rollback manifest.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$gameExe=Join-Path $game 'bin/Risen.exe'
if(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $gameExe}){throw 'Close Risen before deployment.'}
if(!(Test-Path -LiteralPath (Join-Path $repo 'backup/bin-original'))){throw 'Original bin backup missing.'}
$source=Join-Path $repo 'build/bin/mods/world_debug.dll'
if(!(Test-Path -LiteralPath $source)){throw 'Build world_debug.dll first.'}
$backup=Join-Path $repo ('backup/world-debug-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
$paths=@('bin/mods/world_debug.dll','bin/mods/world_debug.ini')
$manifest=@()
foreach($relative in $paths){
    $target=Join-Path $game $relative
    $existed=Test-Path -LiteralPath $target
    if($existed){Copy-Item -LiteralPath $target -Destination (Join-Path $backup (Split-Path $target -Leaf))}
    $manifest+=@{path=$relative;existed=$existed;hash=if($existed){(Get-FileHash -LiteralPath $target).Hash}else{$null}}
}
@{files=$manifest;created=(Get-Date -Format o)} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json')
$other=@(Get-ChildItem (Join-Path $game 'bin') -Recurse -File | Where-Object {
    $_.Extension -in '.dll','.ini' -and $_.Name -notin 'world_debug.dll','world_debug.ini'
} | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName).Hash})
try {
    Copy-Item -LiteralPath $source -Destination (Join-Path $game $paths[0])
    # Preserve existing speed, bindings and enable state on updates. Only a first test install
    # is explicitly enabled here; the DLL's source default remains disabled.
    if(!(Test-Path -LiteralPath (Join-Path $game $paths[1]))) {
        @('[Debug]','Enabled=1','SelectKey=NUMPAD7','PhysicsKey=NUMPAD5','LiftKey=NUMPAD9',
          'FreecamKey=NUMPAD0','LiftCm=100','FreecamSpeed=300','FreecamSlowerKey=-','FreecamFasterKey==') | Set-Content -LiteralPath (Join-Path $game $paths[1]) -Encoding ascii
    }
    if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath (Join-Path $game $paths[0])).Hash){throw 'Deployed DLL differs.'}
    $iniRecord=$manifest | Where-Object {$_.path -eq $paths[1]}
    if($iniRecord.existed -and (Get-FileHash -LiteralPath (Join-Path $game $paths[1])).Hash -ne $iniRecord.hash){throw 'Existing user INI changed.'}
    $after=@(Get-ChildItem (Join-Path $game 'bin') -Recurse -File | Where-Object {
        $_.Extension -in '.dll','.ini' -and $_.Name -notin 'world_debug.dll','world_debug.ini'
    } | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName).Hash})
    if(Compare-Object $other $after){throw 'Unrelated DLL/INI changed.'}
} catch {
    foreach($record in $manifest){
        $target=Join-Path $game $record.path
        if($record.existed){Copy-Item -LiteralPath (Join-Path $backup (Split-Path $target -Leaf)) -Destination $target -Force}
        elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
    }
    throw
}
Write-Output "DEPLOYED: $((Get-Item -LiteralPath $source).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')); only world_debug.dll + world_debug.ini. BACKUP: $backup"
