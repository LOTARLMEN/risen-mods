# Stage/verify a height ladder. Game writes use the CLI project transaction and verified backup.
param([switch]$Install)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$installed = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
$stage = Join-Path $repo ('build/tests/climb-heights-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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
$originalHash = (Get-FileHash -LiteralPath $installed).Hash
$originalEntities = Invoke-Risen entities RisenMods_NewLayer_Test --geometry --props --json | ConvertFrom-Json
Push-Location (Join-Path $repo 'rust')
try {
    & cargo run --offline -p risen-formats --example climb_height_stage -- $game $stage
    if ($LASTEXITCODE -ne 0) { throw 'Height ladder staging failed.' }
} finally { Pop-Location }
$layer = Join-Path $stage 'climb-heights.lrent'
$heights = Import-Csv -LiteralPath (Join-Path $stage 'heights.csv')
$entities = Invoke-Risen entities $layer --geometry --props --json | ConvertFrom-Json
if ($entities.Count -ne 33 -or $heights.Count -ne 5) { throw 'Wrong staged height ladder.' }
foreach ($i in 1..14) {
    if (($entities[$i] | ConvertTo-Json -Depth 14 -Compress) -cne ($originalEntities[$i] | ConvertTo-Json -Depth 14 -Compress)) {
        throw "Existing entity [$i] changed."
    }
}
foreach ($row in $heights) {
    $first = [int]$row.first
    $last = [int]$row.last
    foreach ($i in $first..$last) {
        $set = $entities[$i].sets | Where-Object class -eq 'eCCollisionShape_PS'
        if (($set.props | Where-Object name -eq 'IsClimbable').value -ne '1') { throw 'Missing climbing permission.' }
    }
}
Invoke-Risen climbable $layer 32 --enabled true --dry-run
if (Compare-Object $before (Snapshot)) { throw 'Staging/dry-run changed game files.' }
Write-Output "HEIGHT LADDER STAGING PASSED: $stage"
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen before installing.' }
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $originalHash) { throw 'Installed source changed since staging.' }
    $backup = $null
    try {
        $result = @(Invoke-Risen climbable $layer 32 --enabled true --out $installed)
        $result | Write-Output
        $line = $result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1
        if (!$line) { throw 'Missing transaction backup.' }
        $backup = $line.Substring(8)
        if ((Get-FileHash -LiteralPath $layer).Hash -ne (Get-FileHash -LiteralPath $installed).Hash) { throw 'Installed bytes differ from staging.' }
        $actual = Invoke-Risen entities RisenMods_NewLayer_Test --geometry --props --json | ConvertFrom-Json
        if (($actual | ConvertTo-Json -Depth 14 -Compress) -cne ($entities | ConvertTo-Json -Depth 14 -Compress)) { throw 'Installed VFS differs.' }
        $changes = @(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed + ' ') }).Count) { throw 'Unrelated game files changed.' }
        [pscustomobject]@{ backup = $backup; layer = $installed; staged = $layer; heights = $heights; entities = $actual } |
            ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $stage 'installed-test.json') -Encoding utf8
    } catch {
        if ($backup) { Invoke-Risen world-restore $backup }
        throw
    }
    Write-Output "INSTALLED: height ladder (1,2,4,5,6 crates), automatic native climbing modes. BACKUP: $backup"
}
