# Two identical static three-crate towers, with only ledge-climbing permissions differing.
param([switch]$Install)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$installed = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$stage = Join-Path $repo ('build/tests/climbable-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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
$before = Snapshot
$originalHash = (Get-FileHash -LiteralPath $installed).Hash
$original = Entities 'RisenMods_NewLayer_Test'
if ($original.Count -ne 9 -or $original[7].name -ne 'Obj_Dec_Barrel_01' -or $original[8].name -ne 'Obj_Dec_Barrel_01') {
    throw 'Expected the nine-entity snap setup.'
}
$layer = Join-Path $stage 'towers-unflagged.lrent'
$source = 'RisenMods_NewLayer_Test'
for ($tower = 0; $tower -lt 2; $tower++) {
    $x = if ($tower -eq 0) { -37850 } else { -37450 }
    $base = 9 + 3 * $tower
    for ($level = 0; $level -lt 3; $level++) {
        $index = $base + $level
        Invoke-Risen spawn Obj_Dec_Crate_01 --from-template --layer $source --to $x 100 -5300 --yaw 0 --out $layer
        $source = $layer
        if ($level -eq 0) {
            Invoke-Risen ground $layer $index --surface-layer Levelmesh_Landscape --gap -2 --max-rise 500 --out $layer
        } else {
            Invoke-Risen snap $layer $index --against ($index - 1) --side top --gap 0 --out $layer
        }
        Invoke-Risen climbable $layer $index --enabled false --out $layer
    }
}
$baseline = Entities $layer
if ($baseline.Count -ne 15) { throw 'Wrong staged tower count.' }
# Toggle exactly one byte and prove full-layer byte identity after undo, including class data.
$toggle = Join-Path $stage 'one-toggle.lrent'
$undo = Join-Path $stage 'one-undo.lrent'
Invoke-Risen climbable $layer 14 --enabled true --dry-run
Invoke-Risen climbable $layer 14 --enabled true --out $toggle
Invoke-Risen climbable $toggle 14 --enabled false --out $undo
if ((Get-FileHash -LiteralPath $layer).Hash -ne (Get-FileHash -LiteralPath $undo).Hash) { throw 'Climbing flag undo was not byte-identical.' }
$a = [IO.File]::ReadAllBytes($layer)
$b = [IO.File]::ReadAllBytes($toggle)
if ($a.Length -ne $b.Length) { throw 'Flag edit changed file size.' }
$diffs = 0
for ($i = 0; $i -lt $a.Length; $i++) { if ($a[$i] -ne $b[$i]) { $diffs++ } }
if ($diffs -ne 1) { throw 'Flag edit changed more than one byte.' }
$refused = Join-Path $stage 'must-not-exist.lrent'
$failure = @(& $exe --game $game climbable $layer 0 --enabled true --out $refused 2>&1)
if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $refused) -or ($failure -join ' ') -notlike '*exactly one existing eCCollisionShape_PS*') {
    throw 'Non-collidable root was not refused safely.'
}
$flagged = Join-Path $stage 'climbable-towers.lrent'
Copy-Item -LiteralPath $layer -Destination $flagged
foreach ($index in 12..14) { Invoke-Risen climbable $flagged $index --enabled true --out $flagged }
$actual = Entities $flagged
foreach ($i in 1..8) {
    if (($actual[$i] | ConvertTo-Json -Depth 14 -Compress) -cne ($original[$i] | ConvertTo-Json -Depth 14 -Compress)) { throw "Existing entity [$i] changed." }
}
$expected = $baseline | ConvertTo-Json -Depth 14 | ConvertFrom-Json
foreach ($i in 12..14) {
    $set = $expected[$i].sets | Where-Object class -eq 'eCCollisionShape_PS'
    ($set.props | Where-Object name -eq 'IsClimbable').value = '1'
}
if (($actual | ConvertTo-Json -Depth 14 -Compress) -cne ($expected | ConvertTo-Json -Depth 14 -Compress)) { throw 'Unexpected flagged tower changes.' }
foreach ($i in 9..14) {
    $info = Invoke-Risen geometry $flagged $i --json | ConvertFrom-Json
    if ($info.rigid_body -or !$info.entity_collision_enabled -or $info.collision_sets[0].shapes[0].kind -ne 'box' -or
        $info.collision_sets[0].is_climbable -ne ($i -ge 12)) { throw 'Wrong tower collision/flag.' }
    if ($i -in @(10,11,13,14) -and [Math]::Abs($actual[$i].geometry.world_box[1] - $actual[$i - 1].geometry.world_box[4]) -gt 0.02) {
        throw 'Crate tower contact mismatch.'
    }
    if (($actual[$i].geometry.local_box -join ',') -ne ($actual[9].geometry.local_box -join ',')) { throw 'Crate models differ.' }
}
if (Compare-Object $before (Snapshot)) { throw 'Staging/dry-run changed game files.' }
Write-Output "CLIMBABLE STAGING PASSED: 15 entities; exact one-byte edit/undo, refusal, crate contact and unchanged old entities. $flagged"
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen before installing.' }
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $originalHash) { throw 'Installed source changed since staging.' }
    $backup = $null
    try {
        $result = @(Invoke-Risen climbable $flagged 14 --enabled true --out $installed)
        $result | Write-Output
        $line = $result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1
        if (!$line) { throw 'Missing transaction backup.' }
        $backup = $line.Substring(8)
        if ((Get-FileHash -LiteralPath $flagged).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed bytes differ from staging.' }
        $readback = Entities 'RisenMods_NewLayer_Test'
        if (($readback | ConvertTo-Json -Depth 14 -Compress) -cne ($actual | ConvertTo-Json -Depth 14 -Compress)) { throw 'Installed VFS differs.' }
        $changes = @(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed + ' ') }).Count) { throw 'Unrelated game files changed.' }
        [pscustomobject]@{ backup = $backup; layer = $installed; staged = $flagged; entities = $readback } |
            ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json') -Encoding utf8
    } catch {
        if ($backup) { Invoke-Risen world-restore $backup }
        throw
    }
    Write-Output "INSTALLED: OFF tower [9..11] x=-37850; ON tower [12..14] x=-37450; both z=-5300. BACKUP: $backup"
}
