# Prepare two board/barrel groups near Sara in the already registered shield test layer.
# All edits are staged first. One verified world-restore backup covers the installed layer.
param([switch]$StageOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$relative = 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$installed = Join-Path $game $relative
$stage = Join-Path $repo ('build/tests/nested-yaw-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null

function Closed {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) {
        throw 'Close Risen before preparing or installing this test.'
    }
}
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Hash $_.FullName) })
}
Closed
$before = Snapshot
$base = Invoke-Risen entities $installed --geometry --json | ConvertFrom-Json
if ($base.Count -ne 2 -or $base[1].name -ne 'It_Shield_Round') { throw 'Expected the unchanged shield-only test layer.' }
$originalHash = Hash $installed
$layer = Join-Path $stage 'RisenMods_NewLayer_Test.lrent'
Invoke-Risen copy Monastery_Dyn 1625 --into $installed --to -37450 120 -5700 --out $layer
$group = Invoke-Risen entities $layer --geometry --json | ConvertFrom-Json
if ($group.Count -ne 4 -or $group[2].name -ne 'Obj_Furn_Plank_Board_01' -or
    $group[3].name -ne 'Obj_Dec_Barrel_01' -or $group[3].parent -ne 2) { throw 'Wrong source subtree.' }
# Slide the barrel 140 cm along the board's local X, leaving the board fixed.
$m = $group[2].geometry.world_matrix
$offset = @(0,1,2 | ForEach-Object { ([double]$m[$_] * 140).ToString('R',[Globalization.CultureInfo]::InvariantCulture) })
Invoke-Risen move $layer 3 --by @offset --out $layer
Invoke-Risen spawn Obj_Furn_Plank_Board_01 --layer $layer --to -36850 120 -5700 --yaw 90 --out $layer
$entities = Invoke-Risen entities $layer --geometry --json | ConvertFrom-Json
if ($entities.Count -ne 6 -or $entities[3].parent -ne 2 -or $entities[5].parent -ne 4) { throw 'Hierarchy changed.' }
for ($i = 0; $i -lt 2; $i++) {
    if (($base[$i] | ConvertTo-Json -Depth 8 -Compress) -cne ($entities[$i] | ConvertTo-Json -Depth 8 -Compress)) {
        # Root geometry must expand, but its identity/position and the shield must be preserved.
        if ($i -eq 1 -or $base[0].guid -ne $entities[0].guid) { throw 'Existing shield/root changed.' }
    }
}
for ($k = 0; $k -lt 3; $k++) {
    if ($entities[2].position[$k] -ne $group[2].position[$k]) { throw 'Moving the barrel moved its parent.' }
    $delta = $entities[3].position[$k] - $group[3].position[$k]
    if ([Math]::Abs($delta - [double]$offset[$k]) -gt 0.02) { throw 'Nested displacement mismatch.' }
}
if ([Math]::Abs($entities[4].geometry.world_matrix[10]) -gt 0.0001 -or
    $entities[4].geometry.world_matrix[8] -lt 0.99) { throw 'Yaw is not 90 degrees.' }
for ($k = 0; $k -lt 16; $k++) {
    if ([Math]::Abs($entities[3].geometry.local_matrix[$k] - $entities[5].geometry.local_matrix[$k]) -gt 0.001) {
        throw 'Child local transform was lost during spawn/yaw.'
    }
}
if (Compare-Object $before (Snapshot)) { throw 'Staging modified game files.' }
$backup = Join-Path $repo ('backup/world-edits/' + [guid]::NewGuid().ToString().ToUpperInvariant())
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $installed -Destination (Join-Path $backup '0.before')
Copy-Item -LiteralPath $layer -Destination (Join-Path $backup '0.after')
if ((Hash (Join-Path $backup '0.before')) -ne $originalHash -or (Hash (Join-Path $backup '0.after')) -ne (Hash $layer)) {
    throw 'Backup verification failed.'
}
# Match risen world-restore's existing manifest format; UTF-8 without BOM.
$manifest = [ordered]@{ root = $game; files = @([ordered]@{ path = $relative; existed = $true }) } | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $backup 'manifest.json'), $manifest, [Text.UTF8Encoding]::new($false))
# Exercise the restore on a staging destination before touching the game.
$stageTarget = Join-Path $stage $relative
New-Item -ItemType Directory -Force -Path (Split-Path $stageTarget -Parent) | Out-Null
Copy-Item -LiteralPath $layer -Destination $stageTarget
$stageManifest = [ordered]@{ root = $stage; files = @([ordered]@{ path = $relative; existed = $true }) } | ConvertTo-Json -Depth 4
$trial = Join-Path $stage 'restore-trial'
New-Item -ItemType Directory -Path $trial | Out-Null
Copy-Item -LiteralPath (Join-Path $backup '0.before'),(Join-Path $backup '0.after') -Destination $trial
[IO.File]::WriteAllText((Join-Path $trial 'manifest.json'), $stageManifest, [Text.UTF8Encoding]::new($false))
Invoke-Risen world-restore $trial
if ((Hash $stageTarget) -ne $originalHash) { throw 'Staging restore failed.' }
if ($StageOnly) { Write-Output "STAGE + RESTORE PASSED; game files unchanged. Output: $layer"; return }
Closed
if ((Hash $installed) -ne $originalHash) { throw 'Game layer changed during preparation.' }
$temporary = $installed + '.nested-yaw-tmp'
try {
    Copy-Item -LiteralPath $layer -Destination $temporary
    # Windows PowerShell converts $null to an empty string for this overload.
    [IO.File]::Replace($temporary, $installed, (Join-Path $backup 'replaced-original.lrent'))
    if ((Hash $installed) -ne (Hash $layer)) { throw 'Installed layer mismatch.' }
    $after = Snapshot
    $changed = @(Compare-Object $before $after)
    if ($changed.Count -ne 2 -or @($changed | Where-Object { !$_.InputObject.StartsWith($installed + ' ') }).Count) {
        throw 'Unexpected game-file change.'
    }
} catch {
    Invoke-Risen world-restore $backup
    throw
}
$entities | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'installed-entities.json') -Encoding utf8
Write-Output "READY: board+barrel A (-37450,120,-5700), B (-36850,120,-5700), B yaw=90. Shield retained."
Write-Output "BACKUP: $backup"
Write-Output "RESTORE: & '$exe' world-restore '$backup'"
Write-Output 'TEST: start NEW game; find both boards, barrels near an end, boards perpendicular; save/load and check again.'
