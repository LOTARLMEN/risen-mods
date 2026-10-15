# Install a previously staged/verified grounded layer, plus a shield above the first board.
# The CLI backs up the one changed game layer and supports world-restore.
param([Parameter(Mandatory=$true)][string]$StagedLayer)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust/target/release/risen.exe'
$installed = Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
if (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $game 'bin/Risen.exe') }) { throw 'Close Risen first.' }
$before = Snapshot
$source = Invoke-Risen entities $StagedLayer --geometry --json | ConvertFrom-Json
if ($source.Count -ne 6 -or $source[3].parent -ne 2 -or $source[5].parent -ne 4) { throw 'Wrong staged test layer.' }
$stage = Join-Path (Split-Path $StagedLayer -Parent) 'grounded-with-shield.lrent'
$y = ([float]($source[2].position[1] + 100)).ToString('R',[Globalization.CultureInfo]::InvariantCulture)
Invoke-Risen spawn It_Shield_Round --from-template --layer $StagedLayer --to -37450 $y -5700 --out $stage
$entities = Invoke-Risen entities $stage --geometry --json | ConvertFrom-Json
if ($entities.Count -ne 7 -or $entities[6].name -ne 'It_Shield_Round') { throw 'Wrong physics test item.' }
$plane = ([float]($entities[2].position[1] + $entities[2].geometry.local_box[1])).ToString('R',[Globalization.CultureInfo]::InvariantCulture)
$backup = $null
try {
    # This is a zero-offset placement; --out points into game data, so save_placement calls the
    # verified project transaction rather than the ordinary scratch writer.
    $result = @(Invoke-Risen ground $stage 2 --height $plane --out $installed)
    $result | Write-Output
    $backup = ($result | Where-Object { $_ -like 'BACKUP: *' } | Select-Object -Last 1).Substring(8)
    $actual = Invoke-Risen entities RisenMods_NewLayer_Test --geometry --json | ConvertFrom-Json
    if (($actual | ConvertTo-Json -Depth 8 -Compress) -cne ($entities | ConvertTo-Json -Depth 8 -Compress)) { throw 'Installed entities differ.' }
    $changes = @(Compare-Object $before (Snapshot))
    if ($changes.Count -ne 2 -or @($changes | Where-Object { !$_.InputObject.StartsWith($installed + ' ') }).Count) {
        throw 'Unrelated game data changed.'
    }
    [pscustomobject]@{ backup = $backup; layer = $installed; entities = $actual } | ConvertTo-Json -Depth 9 |
        Set-Content -LiteralPath (Join-Path (Split-Path $stage -Parent) 'installed-test.json') -Encoding utf8
} catch {
    if ($backup) { Invoke-Risen world-restore $backup }
    throw
}
Write-Output 'READY: both board/barrel groups grounded on landscape with -2 cm inset; one new shield spawns 1 m above board A.'
Write-Output 'TEST: new game, inspect contact, walk on/against boards, temporarily disable AutoLoot to observe shield resting on board, then save/load.'
Write-Output "BACKUP: $backup"
