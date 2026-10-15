# Verify explicit rigid-body policy on the installed shield; optionally install the tested edit.
param([switch]$Install)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$installed = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$stage = Join-Path $repo ('build/tests/physics-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $stage | Out-Null
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
$before = Snapshot
$original = Invoke-Risen entities RisenMods_NewLayer_Test --geometry --props --json | ConvertFrom-Json
if ($original.Count -ne 7 -or $original[6].guid -ne '{5877E235-280B-4F03-AF10-F16DF4E82114}') { throw 'Wrong installed contact test.' }
$info = Invoke-Risen geometry RisenMods_NewLayer_Test 6 --json | ConvertFrom-Json
if ($info.body_flags -ne 126 -or !$info.motion.frozen -or $info.wake_up_counter -ne 0) { throw 'Expected the frozen template shield.' }
$dynamic = Join-Path $stage 'dynamic-shield.lrent'
Invoke-Risen physics RisenMods_NewLayer_Test 6 --mode dynamic --dry-run
if (Compare-Object $before (Snapshot)) { throw 'Dry-run changed game files.' }
Invoke-Risen physics RisenMods_NewLayer_Test 6 --mode dynamic --out $dynamic
$after = Invoke-Risen entities $dynamic --geometry --props --json | ConvertFrom-Json
$expected = $original | ConvertTo-Json -Depth 12 | ConvertFrom-Json
$rigid = $expected[6].sets | Where-Object { $_.class -eq 'eCRigidBody_PS' }
($rigid.props | Where-Object name -eq BodyFlag).value = '0 (eERigidbody_Flag)'
($rigid.props | Where-Object name -eq WakeUpCounter).value = '1'
if (($after | ConvertTo-Json -Depth 12 -Compress) -cne ($expected | ConvertTo-Json -Depth 12 -Compress)) { throw 'Unexpected entity/property/geometry changes.' }
$info = Invoke-Risen geometry $dynamic 6 --json | ConvertFrom-Json
if ($info.body_flags -ne 0 -or $info.motion.frozen -or $info.motion.gravity_disabled -or !$info.physics_enabled -or $info.wake_up_counter -ne 1) { throw 'Wrong dynamic policy.' }
$frozen = Join-Path $stage 'refrozen.lrent'
Invoke-Risen physics $dynamic 6 --mode frozen --out $frozen
if ((Get-FileHash -LiteralPath $frozen).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Refrozen file is not byte-identical to the original.' }
$refused = Join-Path $stage 'must-not-exist.lrent'
$failure = @(& $exe --game $game physics RisenMods_NewLayer_Test 2 --mode dynamic --out $refused 2>&1)
if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $refused) -or ($failure -join ' ') -notlike '*exactly one existing eCRigidBody_PS*') { throw 'Static board was not refused safely.' }
if (Compare-Object $before (Snapshot)) { throw 'Offline tests changed game files.' }
Write-Output "PHYSICS CLI PASSED; staged $dynamic; game data/INI/DLL SHA256 unchanged."
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen before installing; staged edit remains available.' }
    $backup = $null
    try {
        $result = @(Invoke-Risen physics RisenMods_NewLayer_Test 6 --mode dynamic)
        $result | Write-Output
        $backup = ($result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1).Substring(8)
        if ((Get-FileHash -LiteralPath $dynamic).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed file differs from verified staging.' }
        $changes = @(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed + ' ') }).Count) { throw 'Unrelated game files changed.' }
        [pscustomobject]@{ backup = $backup; layer = $installed; staged = $dynamic; shield = $info } | ConvertTo-Json -Depth 10 |
            Set-Content -LiteralPath (Join-Path $stage 'installed-test.json') -Encoding utf8
    } catch {
        if ($backup) { Invoke-Risen world-restore $backup }
        throw
    }
    Write-Output "INSTALLED: only shield [6] unlocked, all placements retained. BACKUP: $backup"
}
