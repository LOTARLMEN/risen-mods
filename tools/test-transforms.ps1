# Read-only game checks; every edited layer is written under build/tests/transforms.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$game = Split-Path $repo -Parent
$cli = Join-Path $repo 'rust/target/release/risen.exe'
$scratch = Join-Path $repo 'build/tests/transforms'
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object { $_.Extension -in '.lrent','.tple','.tab','.sec','.wrl','.wrldatasc','.dll','.ini' } |
        Sort-Object FullName | ForEach-Object { '{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
}
function RunCli([string[]]$Arguments) {
    & $cli @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CLI failed: $Arguments" }
}
function Entities([string]$Layer) {
    RunCli -Arguments @('entities', $Layer, '--geometry', '--json') | ConvertFrom-Json
}
function CheckYaw([string]$File, [int]$First, [double]$Degrees, [int]$Count) {
    $entities = Entities $File
    if ($entities.Count -ne $First + $Count) { throw 'Wrong entity count' }
    for ($i = $First; $i -lt $entities.Count; $i++) {
        $m = $entities[$i].geometry.world_matrix
        $heading = [Math]::Atan2($m[8], $m[10])
        $expected = $Degrees * [Math]::PI / 180
        if ([Math]::Abs([Math]::Sin($heading) - [Math]::Sin($expected)) -gt 0.0001 -or
            [Math]::Abs([Math]::Cos($heading) - [Math]::Cos($expected)) -gt 0.0001) { throw 'Wrong heading' }
    }
}
$before = Snapshot
try {
    $original = Entities 'Swamp_Outdoor_Dyn'
    $move = Join-Path $scratch 'nested-move.lrent'
    RunCli -Arguments @('move','Swamp_Outdoor_Dyn','9','--by','64','-32','16','--out',$move)
    $moved = Entities $move
    for ($k = 0; $k -lt 3; $k++) {
        if ([Math]::Abs($moved[9].position[$k] - $original[9].position[$k] - @(64,-32,16)[$k]) -gt 0.01) { throw 'Wrong move' }
    }
    $return = Join-Path $scratch 'nested-return.lrent'
    RunCli -Arguments (@('move',$move,'9','--to') + @($original[9].position | ForEach-Object { $_.ToString([Globalization.CultureInfo]::InvariantCulture) }) + @('--out',$return))
    $returned = Entities $return
    for ($k = 0; $k -lt 3; $k++) {
        if ([Math]::Abs($returned[9].position[$k] - $original[9].position[$k]) -gt 0.01) { throw 'Wrong --to move' }
    }
    RunCli -Arguments @('move','Dungeon_Titan_Events','125','--by','10','20','30','--out',(Join-Path $scratch 'door-move.lrent'))
    $cases = @(
        @{ Template = 'It_Axe_Ogre'; Layer = 'Game_Start_Item'; Yaw = 90; Extra = @('--from-template'); Name = 'template' },
        @{ Template = 'It_Shield_Round'; Layer = 'RisenMods_NewLayer_Test'; Yaw = -90; Extra = @(); Name = 'instance' },
        @{ Template = 'Wolf_Black'; Layer = 'RisenMods_NewLayer_Test'; Yaw = 180; Extra = @(); Name = 'import' }
    )
    $newTemplate = Get-ChildItem (Join-Path $game 'data/common/templates') -Recurse -Filter It_Axe_RisenMods_Test.tple | Select-Object -First 1
    if ($newTemplate) { $cases += @{ Template = $newTemplate.FullName; Layer = 'Game_Start_Item'; Yaw = 450; Extra = @(); Name = 'file' } }
    foreach ($case in $cases) {
        $first = (Entities $case.Layer).Count
        $out = Join-Path $scratch ($case.Name + '.lrent')
        RunCli -Arguments (@('spawn',$case.Template,'--layer',$case.Layer,'--to','-36950','200','-5100','--count','2','--spread','100',
            '--yaw',[string]$case.Yaw,'--out',$out) + $case.Extra)
        CheckYaw $out $first $case.Yaw 2
    }
    $baseline = Entities 'RisenMods_NewLayer_Test'
    $defaultOut = Join-Path $scratch 'no-yaw.lrent'
    RunCli -Arguments @('spawn','It_Shield_Round','--layer','RisenMods_NewLayer_Test','--to','-36950','200','-5100','--out',$defaultOut)
    $default = Entities $defaultOut
    for ($k = 0; $k -lt 12; $k++) {
        if ($default[-1].geometry.world_matrix[$k] -ne $baseline[1].geometry.world_matrix[$k]) { throw 'Default rotation changed' }
    }
    $invalidOut = Join-Path $scratch 'invalid-yaw.lrent'
    $ErrorActionPreference = 'Continue'
    & $cli spawn It_Axe_Ogre --to 0 0 0 --yaw NaN --out $invalidOut 2>$null | Out-Null
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -eq 0 -or (Test-Path -LiteralPath $invalidOut)) { throw 'NaN yaw was accepted' }
    Write-Host 'Nested --by/--to, rounded door matrix, template/instance/import/file yaw, count/spread, default rotation and invalid yaw: OK'
} finally {
    $after = Snapshot
    if (Compare-Object $before $after) { throw 'Game data or DLL hashes changed' }
    Write-Host 'Game data, INI and DLL SHA256 unchanged.'
}
