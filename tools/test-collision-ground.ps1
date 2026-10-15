# Stage physical placement on the confirmed 35-entity scene; optionally install with verified backup.
param([switch]$Install)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$installed=Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$stage=Join-Path $repo ('build/tests/collision-ground-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) {throw "risen failed: $args"}
}
function Entities([string]$Layer){Invoke-Risen entities $Layer --geometry --props --json | ConvertFrom-Json}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
function Close-Array($A,$B,[double]$Tolerance=0.03) {
    if ($A.Count -ne $B.Count){throw 'Array length differs.'}
    for ($k=0;$k -lt $A.Count;$k++){if ([Math]::Abs([double]$A[$k]-[double]$B[$k]) -gt $Tolerance){throw "Coordinate mismatch $k"}}
}
$before=Snapshot
$sourceHash=(Get-FileHash -LiteralPath $installed).Hash
$original=Entities RisenMods_NewLayer_Test
if ($original.Count -ne 35 -or $original[3].parent -ne 2 -or $original[34].parent -ne 33 -or
    $original[2].guid -ne '{5FCF3282-5E4B-4CAC-84F7-893A8AA57E46}') {throw 'Expected the confirmed 35-entity slope scene.'}
$flat=Join-Path $stage 'board-a.lrent'
$final=Join-Path $stage 'physical-ground.lrent'
$flatReport=@(Invoke-Risen ground RisenMods_NewLayer_Test 2 --collision --surface-layer Levelmesh_Landscape_COL --gap -2 --out $flat)
$flatReport | Tee-Object -FilePath (Join-Path $stage 'board-a.txt')
$steepReport=@(Invoke-Risen ground $flat 33 --collision --align --samples 9 --max-rise 500 --surface-layer Levelmesh_Landscape_COL --gap -2 --out $final)
$steepReport | Tee-Object -FilePath (Join-Path $stage 'steep.txt')
$after=Entities $final
if ($after.Count -ne 35){throw 'Entity count changed.'}
foreach ($i in 1..34) {
    if ($i -notin 2,3,33,34 -and ($original[$i] | ConvertTo-Json -Depth 14 -Compress) -cne ($after[$i] | ConvertTo-Json -Depth 14 -Compress)){throw "Unrelated entity $i changed."}
}
foreach ($pair in @(@(2,3),@(33,34))) {
    $parent=$pair[0];$child=$pair[1]
    if (($original[$child].geometry.local_matrix -join ',') -cne ($after[$child].geometry.local_matrix -join ',')){throw 'Child local pose changed.'}
    foreach ($i in $pair) {
        if (($original[$i].sets | ConvertTo-Json -Depth 14 -Compress) -cne ($after[$i].sets | ConvertTo-Json -Depth 14 -Compress)){throw 'Collider/other property bytes changed.'}
    }
    $expected=New-Object double[] 16
    foreach ($r in 0..3){foreach ($c in 0..3){foreach ($k in 0..3){$expected[4*$r+$c]+=$after[$child].geometry.local_matrix[4*$r+$k]*$after[$parent].geometry.world_matrix[4*$k+$c]}}}
    Close-Array $expected $after[$child].geometry.world_matrix
}
Close-Array $after[2].geometry.world_matrix[0..11] $original[2].geometry.world_matrix[0..11] 0.00001
Close-Array $after[2].position @(-37450,65.28201,-5700)
Close-Array $after[33].position @(-37650,325.83588,-4300) 0.06
$repeatA=Join-Path $stage 'repeat-a.lrent'
$repeat=Join-Path $stage 'repeat-steep.lrent'
Invoke-Risen ground $final 2 --collision --surface-layer Levelmesh_Landscape_COL --gap -2 --out $repeatA
Invoke-Risen ground $repeatA 33 --collision --align --samples 9 --max-rise 500 --surface-layer Levelmesh_Landscape_COL --gap -2 --out $repeat
$again=Entities $repeat
foreach ($i in 2,3,33,34){Close-Array $again[$i].geometry.world_matrix $after[$i].geometry.world_matrix 0.06}
$visual=Join-Path $stage 'visual-control.lrent'
Invoke-Risen ground RisenMods_NewLayer_Test 2 --align --surface-layer Levelmesh_Landscape --gap -2 --out $visual
$oldMode=Entities $visual
Close-Array $oldMode[2].geometry.world_matrix $original[2].geometry.world_matrix
$refused=Join-Path $stage 'must-not-exist.lrent'
& $exe --game $game ground RisenMods_NewLayer_Test 33 --collision --align --samples 9 --max-rise 500 --max-slope 1 --surface-layer Levelmesh_Landscape_COL --out $refused *> (Join-Path $stage 'slope-refusal.txt')
if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $refused)){throw 'Slope refusal failed.'}
& $exe --game $game ground RisenMods_NewLayer_Test 2 --collision --surface-layer Levelmesh_Landscape --out $refused *> (Join-Path $stage 'no-visual-fallback.txt')
if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $refused)){throw 'Missing physical surface silently fell back to render geometry.'}
if (Compare-Object $before (Snapshot)){throw 'Staging changed game files.'}
@{stage=$stage;entities=$after;game_hashes_unchanged=$true;cli=(Get-Item $exe).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')} |
    ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $stage 'staging-test.json')
Write-Output "COLLISION GROUND STAGING PASSED: $stage"
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $game 'bin/Risen.exe')}){throw 'Close Risen before installation.'}
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $sourceHash){throw 'Game layer changed since staging.'}
    $backup=$null
    try {
        # The ON tower is already climbable: retain its exact bytes and use the shared transaction.
        $report=@(Invoke-Risen climbable $final 14 --enabled true --out $installed)
        $report | Write-Output
        $backupLine=$report | Where-Object {$_ -like 'BACKUP: *'} | Select-Object -Last 1
        if (!$backupLine){throw 'Missing verified backup.'}
        $backup=$backupLine.Substring(8)
        if ((Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $final).Hash){throw 'Installed bytes differ.'}
        $actual=Entities RisenMods_NewLayer_Test
        if (($actual | ConvertTo-Json -Depth 14 -Compress) -cne ($after | ConvertTo-Json -Depth 14 -Compress)){throw 'Named VFS readback differs.'}
        $changes=@(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object {!$_.InputObject.StartsWith($installed+' ')}).Count){throw 'Unrelated game files changed.'}
        @{backup=$backup;stage=$stage;layer=$installed;staged=$final;entities=$actual;manual_test='pending'} |
            ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json')
    } catch {if ($backup){Invoke-Risen world-restore $backup};throw}
    Write-Output "INSTALLED: physical placement of boards [2]/[33], children [3]/[34] follow. BACKUP: $backup"
}
