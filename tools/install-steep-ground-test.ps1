# Install a staged natural-slope board/barrel pair after checking the complete original scene.
param([Parameter(Mandatory=$true)][string]$StagedLayer)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$installed=Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Entities([string]$Layer) { Invoke-Risen entities $Layer --geometry --props --json | ConvertFrom-Json }
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen before installing.' }
$before=Snapshot
$original=Entities 'RisenMods_NewLayer_Test'
$after=Entities $StagedLayer
if ($original.Count -ne 33 -or $after.Count -ne 35 -or $after[34].parent -ne 33 -or
    $after[33].name -ne 'Obj_Furn_Plank_Board_01' -or $after[34].name -ne 'Obj_Dec_Barrel_01') { throw 'Wrong steep test scene.' }
foreach ($i in 1..32) {
    if (($original[$i] | ConvertTo-Json -Depth 14 -Compress) -cne ($after[$i] | ConvertTo-Json -Depth 14 -Compress)) { throw "Original entity [$i] changed." }
}
if (($after[34].geometry.local_matrix -join ',') -cne ($original[3].geometry.local_matrix -join ',')) { throw 'Child local matrix changed.' }
foreach ($pair in @(@(33,2),@(34,3))) {
    if (($after[$pair[0]].sets | ConvertTo-Json -Depth 12 -Compress) -cne ($original[$pair[1]].sets | ConvertTo-Json -Depth 12 -Compress)) { throw 'Static copy properties differ.' }
}
if (@($after.guid | Select-Object -Unique).Count -ne 35) { throw 'Duplicate entity GUID.' }
$matrix=$after[33].geometry.world_matrix
$scale=[Math]::Sqrt($matrix[4]*$matrix[4]+$matrix[5]*$matrix[5]+$matrix[6]*$matrix[6])
$tilt=[Math]::Acos($matrix[5]/$scale)*180/[Math]::PI
if ($tilt -lt 25 -or $tilt -gt 35 -or [Math]::Abs($after[33].position[0]+37650) -gt 0.02 -or
    [Math]::Abs($after[33].position[2]+4300) -gt 0.02) { throw 'Wrong steep slope/position.' }
$child=$after[34].geometry.local_matrix
$expected=New-Object double[] 16
foreach ($r in 0..3) { foreach ($c in 0..3) { foreach ($k in 0..3) { $expected[4*$r+$c]+=$child[4*$r+$k]*$matrix[4*$k+$c] } } }
foreach ($k in 0..15) {
    if ([Math]::Abs($expected[$k]-$after[34].geometry.world_matrix[$k]) -gt 0.02) { throw 'Child world matrix differs from local*parent.' }
}
$backup=$null
try {
    $result=@(Invoke-Risen climbable $StagedLayer 14 --enabled true --out $installed)
    $result | Write-Output
    $line=$result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1
    if (!$line) { throw 'Missing transaction backup.' }
    $backup=$line.Substring(8)
    if ((Get-FileHash -LiteralPath $StagedLayer).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed bytes differ.' }
    $actual=Entities 'RisenMods_NewLayer_Test'
    if (($actual | ConvertTo-Json -Depth 14 -Compress) -cne ($after | ConvertTo-Json -Depth 14 -Compress)) { throw 'Installed VFS differs.' }
    $changes=@(Compare-Object $before (Snapshot))
    if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed+' ') }).Count) { throw 'Unrelated game files changed.' }
    [pscustomobject]@{ backup=$backup; layer=$installed; staged=$StagedLayer; tilt=$tilt; entities=$actual } |
        ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path (Split-Path $StagedLayer -Parent) 'installed-test.json') -Encoding utf8
} catch {
    if ($backup) { Invoke-Risen world-restore $backup }
    throw
}
Write-Output "INSTALLED: natural slope $tilt degrees, new board [33]/barrel [34], old scene unchanged. BACKUP: $backup"
