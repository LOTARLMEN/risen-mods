# Prepares TODO 5.21's new-template/string-key test. Run while the game is closed.
# Every changed game file is backed up first; the backup contains restore.ps1.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$exe = Join-Path $repo 'rust\target\debug\risen.exe'
$name = 'It_Axe_RisenMods_Test'
$relative = @(
    "data\common\templates\Items\Weapons_Axes\$name.tple",
    'data\common\projects\World\Outdoor\Game_Start\Game_Start_Item.lrent',
    'data\compiled\strings\focus.tab',
    'data\compiled\strings\items.tab'
)
if (Get-Process Risen -ErrorAction SilentlyContinue) { throw 'Close Risen before preparing the test.' }
if (Test-Path -LiteralPath (Join-Path $game $relative[0])) { throw 'Test template already exists; restore the previous test first.' }
if (!(Test-Path -LiteralPath $exe)) { throw 'Build risen-cli (debug) first.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stage = Join-Path $repo "build\tests\new-key-$stamp"
$backup = "D:\RisenModding\backup\new-key-$stamp"
New-Item -ItemType Directory -Path $stage, $backup | Out-Null

function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0) { throw "risen failed: $args" }
}

# Build and read back the template and layer before touching the game's files.
$template = Join-Path $stage "$name.tple"
$layer = Join-Path $stage 'Game_Start_Item.lrent'
Invoke-Risen template-copy It_Axe_Ogre $name --out $template
Invoke-Risen spawn $template --layer Game_Start_Item --to -37150 200 -5100 --out $layer
$entities = Invoke-Risen entities $layer --json | ConvertFrom-Json
if (@($entities | Where-Object name -eq $name).Count -ne 1) { throw 'Expected exactly one new test item.' }

$manifest = foreach ($rel in $relative) {
    $source = Join-Path $game $rel
    $exists = Test-Path -LiteralPath $source
    $sha = $null
    if ($exists) {
        $dest = Join-Path $backup $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
        Copy-Item -LiteralPath $source -Destination $dest
        $sha = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash -ne $sha) { throw "Backup mismatch: $rel" }
    }
    [pscustomobject]@{ path = $rel; existed = $exists; sha256 = $sha }
}
[pscustomobject]@{ game = $game; created = (Get-Date -Format o); files = @($manifest) } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
$restore = @'
$ErrorActionPreference = 'Stop'
if (Get-Process Risen -ErrorAction SilentlyContinue) { throw 'Close Risen before restoring the backup.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest.files) {
    $dest = Join-Path $manifest.game $file.path
    if ($file.existed) {
        $source = Join-Path $PSScriptRoot $file.path
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256) { throw "Damaged backup: $source" }
        Copy-Item -LiteralPath $source -Destination $dest -Force
        if ((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash -ne $file.sha256) { throw "Restore mismatch: $dest" }
    } elseif (Test-Path -LiteralPath $dest) {
        Remove-Item -LiteralPath $dest
    }
}
'@
$restore | Set-Content -LiteralPath (Join-Path $backup 'restore.ps1') -Encoding utf8

if (Get-Process Risen -ErrorAction SilentlyContinue) { throw 'Risen started; backup saved, game data unchanged.' }
try {
    New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $game $relative[0]) -Parent) | Out-Null
    Copy-Item -LiteralPath $template -Destination (Join-Path $game $relative[0])
    Copy-Item -LiteralPath $layer -Destination (Join-Path $game $relative[1]) -Force
    Invoke-Risen tab-set focus "FO_$name" 'Test club [new template]' --lang en
    Invoke-Risen tab-set focus "FO_$name" 'Тестовая дубина [новый шаблон]' --lang ru
    Invoke-Risen tab-set items "ITEMDESC_$name" 'A new template with its own text key. Risen Mods test.' --lang en
    Invoke-Risen tab-set items "ITEMDESC_$name" 'Новый шаблон с собственным текстовым ключом. Проверка Risen Mods.' --lang ru
    Invoke-Risen tab focus "FO_$name" --lang 'ru,en'
    Invoke-Risen tab items "ITEMDESC_$name" --lang 'ru,en'
} catch {
    & (Join-Path $backup 'restore.ps1')
    throw
}
Write-Output "READY: $name at (-37150, 200, -5100), near Sara; start a NEW game."
Write-Output "BACKUP: $backup"
Write-Output "RESTORE: powershell -NoProfile -File `"$backup\restore.ps1`""
