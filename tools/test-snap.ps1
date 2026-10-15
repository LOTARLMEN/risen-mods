# Stage two static barrel supports under board B; optionally install with verified rollback.
param([switch]$Install)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$installed = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$stage = Join-Path $repo ('build/tests/snap-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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
$originalHash = (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash
$original = Entities 'RisenMods_NewLayer_Test'
if ($original.Count -ne 7 -or $original[6].guid -ne '{5877E235-280B-4F03-AF10-F16DF4E82114}' -or
    $original[4].name -ne 'Obj_Furn_Plank_Board_01') { throw 'Expected the seven-entity dynamic shield setup.' }
$layer = Join-Path $stage 'supports.lrent'
Invoke-Risen spawn Obj_Dec_Barrel_01 --from-template --layer RisenMods_NewLayer_Test --to -36825 100 -5800 --yaw 0 --out $layer
Invoke-Risen spawn Obj_Dec_Barrel_01 --from-template --layer $layer --to -36825 100 -5600 --yaw 0 --out $layer
Invoke-Risen snap $layer 7 --against 4 --side bottom --gap 0 --out $layer
Invoke-Risen snap $layer 8 --against 4 --side bottom --gap -8 --out $layer
$after = Entities $layer
if ($after.Count -ne 9) { throw 'Wrong entity count.' }
foreach ($i in 1..6) {
    if (($after[$i] | ConvertTo-Json -Depth 14 -Compress) -cne ($original[$i] | ConvertTo-Json -Depth 14 -Compress)) {
        throw "Existing entity [$i] changed."
    }
}
foreach ($i in @(7,8)) {
    $embed = if ($i -eq 7) { 0 } else { 8 }
    if ($after[$i].name -ne 'Obj_Dec_Barrel_01' -or $null -ne $after[$i].parent -or
        [Math]::Abs($after[$i].geometry.world_box[4] - $after[4].geometry.world_box[1] - $embed) -gt 0.02) {
        throw "Support [$i] contact/parent mismatch."
    }
    $info = Invoke-Risen geometry $layer $i --json | ConvertFrom-Json
    if ($info.rigid_body -or !$info.entity_collision_enabled -or $info.collision_sets[0].shapes[0].kind -ne 'box') {
        throw 'Expected a static collidable barrel support.'
    }
    Invoke-Risen ground $layer $i --surface-layer Levelmesh_Landscape --dry-run
}
if (Compare-Object $before (Snapshot)) { throw 'Staging changed game files.' }
Write-Output "SNAP STAGING PASSED: exact contact [7], 8 cm overlap [8]; existing entities unchanged. $layer"
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen before installing.' }
    if ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $originalHash) { throw 'Installed source changed since staging.' }
    $backup = $null
    try {
        # Reapplying the verified snap is a zero-offset operation and routes installation
        # through project::apply's game-closed check, verified backup and atomic replacement.
        $result = @(Invoke-Risen snap $layer 7 --against 4 --side bottom --gap 0 --out $installed)
        $result | Write-Output
        $line = $result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1
        if (!$line) { throw 'Missing transaction backup.' }
        $backup = $line.Substring(8)
        if ((Get-FileHash -LiteralPath $layer).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed bytes differ from staging.' }
        $actual = Entities 'RisenMods_NewLayer_Test'
        if (($actual | ConvertTo-Json -Depth 14 -Compress) -cne ($after | ConvertTo-Json -Depth 14 -Compress)) { throw 'VFS installed readback differs.' }
        $changes = @(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed + ' ') }).Count) { throw 'Unrelated game files changed.' }
        [pscustomobject]@{ backup = $backup; layer = $installed; staged = $layer; entities = $actual } |
            ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json') -Encoding utf8
    } catch {
        if ($backup) { Invoke-Risen world-restore $backup }
        throw
    }
    Write-Output "INSTALLED: only test layer changed. BACKUP: $backup"
}
