# Stage, verify, then install a separate world layer with one round shield near Sara.
# layer-create keeps a verified backup and rolls back partial writes. No DLLs are changed.
param([switch]$StageOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust\target\debug\risen.exe'
$name = 'RisenMods_NewLayer_Test'
$layerRelative = "data\common\projects\World\RisenMods\$name.lrent"
$sectorRelative = "data\common\projects\World\RisenMods\$name.sec"
$dataRelative = 'data\common\projects\World\World.wrldatasc'
if (Get-Process Risen -ErrorAction SilentlyContinue) { throw 'Close Risen before preparing the test.' }
if (!(Test-Path -LiteralPath $exe)) { throw 'Build risen-cli (debug) first.' }
if (Test-Path -LiteralPath (Join-Path $game $layerRelative)) { throw 'New-layer test already exists; restore its backup first.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stage = Join-Path $repo "build\tests\new-layer-$stamp"
New-Item -ItemType Directory -Path $stage | Out-Null

function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Assert-Layer([string]$path) {
    $entities = Invoke-Risen entities $path --json | ConvertFrom-Json
    if ($entities.Count -ne 2 -or $entities[0].name -ne 'DRE' -or $entities[1].name -ne 'It_Shield_Round') {
        throw 'Expected a new DRE root and exactly one round shield.'
    }
    if ($entities[1].position[0] -ne -36950 -or $entities[1].position[1] -ne 200 -or $entities[1].position[2] -ne -5100) {
        throw 'Wrong test shield position.'
    }
    return $entities[1].guid
}

# Prove construction and restoration in the staging tree before installing anything.
$beforeSectors = Invoke-Risen world-sectors --json | ConvertFrom-Json
$staged = @(Invoke-Risen layer-create $name --spawn It_Shield_Round --to -36950 200 -5100 --out-dir $stage)
$staged | Write-Output
$stageBackup = ($staged | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1).Substring(8)
Assert-Layer (Join-Path $stage $layerRelative) | Out-Null
$afterSectors = Invoke-Risen world-sectors (Join-Path $stage $dataRelative) --json | ConvertFrom-Json
if ($afterSectors.Count -ne $beforeSectors.Count + 1 -or $afterSectors[-1].name -ne $name -or !$afterSectors[-1].enabled) {
    throw 'New sector was not registered exactly once.'
}
for ($i = 0; $i -lt $beforeSectors.Count; $i++) {
    if ($beforeSectors[$i].name -cne $afterSectors[$i].name -or $beforeSectors[$i].enabled -ne $afterSectors[$i].enabled) {
        throw 'An existing sector changed.'
    }
}
Invoke-Risen world-restore $stageBackup
foreach ($rel in @($layerRelative, $sectorRelative, $dataRelative)) {
    if (Test-Path -LiteralPath (Join-Path $stage $rel)) { throw "Stage rollback left $rel" }
}
if ($StageOnly) { Write-Output 'STAGE + RESTORE PASSED; game files unchanged.'; return }

# Record every existing loose project file: only the registry may change.
$projectRoot = Join-Path $game 'data\common\projects'
$existing = @{}
if (Test-Path -LiteralPath $projectRoot) {
    Get-ChildItem -LiteralPath $projectRoot -File -Recurse | ForEach-Object {
        $existing[$_.FullName] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
}
$backup = $null
try {
    $installed = @(Invoke-Risen layer-create $name --spawn It_Shield_Round --to -36950 200 -5100)
    $installed | Write-Output
    $backup = ($installed | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1).Substring(8)
    $guid = Assert-Layer (Join-Path $game $layerRelative)
    $sectors = Invoke-Risen world-sectors --json | ConvertFrom-Json
    if (@($sectors | Where-Object name -eq $name).Count -ne 1) { throw 'Installed sector missing or duplicated.' }
    $changed = @((Join-Path $game $dataRelative), (Join-Path $game 'data\common\projects\World\World_local.wrldatasc'))
    foreach ($path in $existing.Keys) {
        if ($path -notin $changed -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $existing[$path]) {
            throw "Unrelated loose project file changed: $path"
        }
    }
    [pscustomobject]@{ layer = $name; item = 'It_Shield_Round'; guid = $guid; position = @(-36950, 200, -5100); backup = $backup } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json') -Encoding utf8
} catch {
    if ($backup) { Invoke-Risen world-restore $backup --force }
    throw
}
Write-Output "READY: round shield in NEW layer $name at (-36950, 200, -5100), near Sara. Start a NEW game."
Write-Output 'TEST: check the shield, save/load before pickup, pick it up, save/load again.'
Write-Output "BACKUP: $backup"
Write-Output "RESTORE: & `"$exe`" world-restore `"$backup`""
