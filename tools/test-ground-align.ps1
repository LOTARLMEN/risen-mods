# Verify dense slope placement and nested transforms; install only board A + its barrel.
param([switch]$Install)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$installed = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$stage = Join-Path $repo ('build/tests/ground-align-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
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
function Close-Array($A,$B,[double]$Tolerance=0.02) {
    if ($A.Count -ne $B.Count) { throw 'Array length differs.' }
    for ($k=0;$k -lt $A.Count;$k++) { if ([Math]::Abs([double]$A[$k]-[double]$B[$k]) -gt $Tolerance) { throw "Matrix/geometry mismatch at $k." } }
}
$before=Snapshot
$originalHash=(Get-FileHash -LiteralPath $installed).Hash
$original=Entities 'RisenMods_NewLayer_Test'
if ($original.Count -ne 33 -or $original[2].guid -ne '{5FCF3282-5E4B-4CAC-84F7-893A8AA57E46}' -or $original[3].parent -ne 2) {
    throw 'Expected the confirmed 33-entity height ladder setup.'
}
$layer=Join-Path $stage 'aligned-board-a.lrent'
Invoke-Risen ground RisenMods_NewLayer_Test 2 --align --surface-layer Levelmesh_Landscape --gap -2 --dry-run
$report=@(Invoke-Risen ground RisenMods_NewLayer_Test 2 --align --surface-layer Levelmesh_Landscape --gap -2 --out $layer)
$report | Write-Output
$after=Entities $layer
if ($after.Count -ne $original.Count) { throw 'Entity count changed.' }
foreach ($i in @(1)+@(4..32)) {
    if (($after[$i] | ConvertTo-Json -Depth 14 -Compress) -cne ($original[$i] | ConvertTo-Json -Depth 14 -Compress)) { throw "Unrelated entity [$i] changed." }
}
if (($after[3].geometry.local_matrix -join ',') -cne ($original[3].geometry.local_matrix -join ',')) { throw 'Child local pose changed.' }
foreach ($i in @(2,3)) {
    if (($after[$i].sets | ConvertTo-Json -Depth 12 -Compress) -cne ($original[$i].sets | ConvertTo-Json -Depth 12 -Compress)) { throw 'Static object properties changed.' }
    $matrix=$after[$i].geometry.world_matrix
    if ([Math]::Abs($matrix[5]) -gt 0.9999) { throw 'Expected nonzero surface tilt.' }
    foreach ($r in 0..2) {
        $old=$original[$i].geometry.world_matrix
        $length=[Math]::Sqrt($matrix[4*$r]*$matrix[4*$r]+$matrix[4*$r+1]*$matrix[4*$r+1]+$matrix[4*$r+2]*$matrix[4*$r+2])
        $was=[Math]::Sqrt($old[4*$r]*$old[4*$r]+$old[4*$r+1]*$old[4*$r+1]+$old[4*$r+2]*$old[4*$r+2])
        if ([Math]::Abs($length-$was) -gt 0.0001) { throw 'Scale changed.' }
    }
}
$local=$after[3].geometry.local_matrix
$parent=$after[2].geometry.world_matrix
$expected=New-Object double[] 16
foreach ($r in 0..3) { foreach ($c in 0..3) { foreach ($k in 0..3) { $expected[4*$r+$c]+=$local[4*$r+$k]*$parent[4*$k+$c] } } }
Close-Array $after[3].geometry.world_matrix $expected
$again=Join-Path $stage 'aligned-again.lrent'
Invoke-Risen ground $layer 2 --align --surface-layer Levelmesh_Landscape --gap -2 --out $again
$repeat=Entities $again
foreach ($i in @(2,3)) { Close-Array $repeat[$i].geometry.world_matrix $after[$i].geometry.world_matrix }
# Different orientation/terrain, for scratch validation only: its independent old supports stay installed.
Invoke-Risen ground RisenMods_NewLayer_Test 4 --align --surface-layer Levelmesh_Landscape --gap -2 --out (Join-Path $stage 'board-b-offline-only.lrent')
Invoke-Risen ground RisenMods_NewLayer_Test 2 --samples 9 --surface-layer Levelmesh_Landscape --out (Join-Path $stage 'dense-without-tilt.lrent')
$dense=Entities (Join-Path $stage 'dense-without-tilt.lrent')
Close-Array $dense[2].geometry.world_matrix[0..11] $original[2].geometry.world_matrix[0..11] 0.00001
$refused=Join-Path $stage 'must-not-exist.lrent'
$failure=@(& $exe --game $game ground RisenMods_NewLayer_Test 2 --align --max-slope 1 --surface-layer Levelmesh_Landscape --out $refused 2>&1)
if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $refused) -or ($failure -join ' ') -notlike '*exceeds limit*') { throw 'Steep slope limit was not refused safely.' }
if (Compare-Object $before (Snapshot)) { throw 'Staging/dry-run changed game files.' }
Write-Output "GROUND ALIGN STAGING PASSED: subtree, scale, repeatability, dense-only mode, slope refusal and other entities/hashes. $stage"
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen before installing.' }
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $originalHash) { throw 'Installed source changed since staging.' }
    $backup=$null
    try {
        # Existing ON tower flag is already true: use the verified project transaction to
        # install the exact staging bytes without recomputing another floating-point transform.
        $result=@(Invoke-Risen climbable $layer 14 --enabled true --out $installed)
        $result | Write-Output
        $line=$result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1
        if (!$line) { throw 'Missing transaction backup.' }
        $backup=$line.Substring(8)
        if ((Get-FileHash -LiteralPath $layer).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed bytes differ from staging.' }
        $actual=Entities 'RisenMods_NewLayer_Test'
        if (($actual | ConvertTo-Json -Depth 14 -Compress) -cne ($after | ConvertTo-Json -Depth 14 -Compress)) { throw 'Installed VFS differs.' }
        $changes=@(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed+' ') }).Count) { throw 'Unrelated game files changed.' }
        [pscustomobject]@{ backup=$backup; layer=$installed; staged=$layer; report=$report; entities=$actual } |
            ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json') -Encoding utf8
    } catch {
        if ($backup) { Invoke-Risen world-restore $backup }
        throw
    }
    Write-Output "INSTALLED: board A + barrel tilted to sampled terrain; all towers and board B/supports retained. BACKUP: $backup"
}
