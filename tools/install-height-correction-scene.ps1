# Finish a scratch native-model A/B scene; optionally install through the verified transaction.
param([Parameter(Mandatory=$true)][string]$Stage,[switch]$Install)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game=Split-Path $repo -Parent
$exe=Join-Path $repo 'rust/target/release/risen.exe'
$Stage=(Resolve-Path -LiteralPath $Stage).Path
$allowed=(Resolve-Path -LiteralPath (Join-Path $repo 'build/tests')).Path+[IO.Path]::DirectorySeparatorChar
if (-not $Stage.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Stage must be inside build/tests.'}
$installed=Join-Path $game 'data/common/projects/World/RisenMods/RisenMods_NewLayer_Test.lrent'
function Invoke-Risen {
    & $exe --game $game @args
    if ($LASTEXITCODE -ne 0){throw "risen failed: $args"}
}
function Entities([string]$Layer){@(Invoke-Risen entities $Layer --geometry --props --json | ConvertFrom-Json)}
function Snapshot {
    @(Get-ChildItem (Join-Path $game 'data'),(Join-Path $game 'bin') -Recurse -File |
        Where-Object {$_.Extension -in '.lrent','.sec','.wrl','.wrldatasc','.tple','.tab','.dll','.ini'} |
        Sort-Object FullName | ForEach-Object {'{0} {1}' -f $_.FullName,(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash})
}
function Close-Array($A,$B,[double]$Tolerance=0.00005) {
    if ($A.Count -ne $B.Count){throw 'Array length differs.'}
    for ($k=0;$k -lt $A.Count;$k++) {
        if ([Math]::Abs([double]$A[$k]-[double]$B[$k]) -gt $Tolerance){throw "Coordinate differs: $k"}
    }
}
function Contact([string]$Layer,[int]$A,[int]$B) {
    Invoke-Risen contact $Layer $A --against $B --json | ConvertFrom-Json
}
function Assert-Original($Entities,$Original) {
    foreach ($i in 1..34) {
        if (($Entities[$i] | ConvertTo-Json -Depth 20 -Compress) -cne ($Original[$i] | ConvertTo-Json -Depth 20 -Compress)){throw "Old entity changed: $i"}
    }
}
$before=Snapshot
$before | Set-Content (Join-Path $Stage 'game-before.sha256.txt')
$originalHash=(Get-FileHash -LiteralPath $installed).Hash
$original=Entities RisenMods_NewLayer_Test
if ($original.Count -ne 35 -or $original[2].guid -ne '{5FCF3282-5E4B-4CAC-84F7-893A8AA57E46}'){throw 'Expected confirmed 35-entity setup.'}
$fixture=Join-Path $Stage 'fixture.lrent'
$layout=@(Import-Csv -LiteralPath (Join-Path $Stage 'layout.csv') | Sort-Object side)
if ($layout.Count -ne 2 -or $layout[0].side -ne 'A' -or $layout[1].side -ne 'B'){throw 'Wrong scene layout.'}
$a=$layout[0];$b=$layout[1]
$fixtureEntities=Entities $fixture
if ($fixtureEntities.Count -ne 45){throw 'Expected ten appended native static objects.'}
Assert-Original $fixtureEntities $original
foreach ($i in 35..44) {
    # CLI represents a root child as parent=null; the format writer uses the actual root index 0.
    if ($null -ne $fixtureEntities[$i].parent -or 'eCRigidBody_PS' -in $fixtureEntities[$i].sets.class){throw 'New objects must be static root children.'}
    if ('eCMesh_PS' -notin $fixtureEntities[$i].sets.class){throw 'New object lacks native render geometry.'}
    $self=Contact $fixture $i $i
    if (-not $self.intersects){throw 'Native collider self-query failed.'}
}
$control=Join-Path $Stage 'control.lrent'
$final=Join-Path $Stage 'final.lrent'
$raw=Invoke-Risen ground $fixture $a.board --collision --samples 3 --surface-layer $fixture --check-contact --json --dry-run
$raw | Set-Content (Join-Path $Stage 'sampled-report.json')
$sampled=$raw | ConvertFrom-Json
Invoke-Risen ground $fixture $a.board --collision --samples 3 --surface-layer $fixture --check-contact --out $control | Set-Content (Join-Path $Stage 'control-placement.txt')
$raw=Invoke-Risen ground $control $b.board --collision --samples 3 --surface-layer $control --check-contact --correct-height --json --dry-run
$raw | Set-Content (Join-Path $Stage 'corrected-report.json')
$corrected=$raw | ConvertFrom-Json
Invoke-Risen ground $control $b.board --collision --samples 3 --surface-layer $control --check-contact --correct-height --out $final | Set-Content (Join-Path $Stage 'corrected-placement.txt')
$after=Entities $final
if ($after.Count -ne 45 -or @($after.guid | Select-Object -Unique).Count -ne 45){throw 'Wrong entity count or repeated GUID.'}
Assert-Original $after $original
foreach ($i in 35..44) {
    if ($i -notin [int]$a.board,[int]$b.board -and
        ($after[$i] | ConvertTo-Json -Depth 20 -Compress) -cne ($fixtureEntities[$i] | ConvertTo-Json -Depth 20 -Compress)){throw "Non-board fixture changed: $i"}
    $set=$after[$i].sets | Where-Object class -eq 'eCCollisionShape_PS'
    if (($set.props | Where-Object name -eq 'IsClimbable').value -ne '1'){throw 'Missing climb permission.'}
}
Close-Array $sampled.planned_world_matrix $after[[int]$a.board].geometry.world_matrix
Close-Array $corrected.planned_world_matrix $after[[int]$b.board].geometry.world_matrix
Close-Array $after[[int]$a.board].geometry.world_matrix[0..11] $after[[int]$b.board].geometry.world_matrix[0..11]
if (($after[[int]$a.board].sets | ConvertTo-Json -Depth 20 -Compress) -cne ($after[[int]$b.board].sets | ConvertTo-Json -Depth 20 -Compress)){throw 'Compared board properties differ.'}
$pierA=Contact $final $a.board $a.pier
$pierB=Contact $final $b.board $b.pier
$baseA=Contact $final $a.board $a.base
$baseB=Contact $final $b.board $b.base
$delta=$after[[int]$b.board].position[1]-$after[[int]$a.board].position[1]
if (-not $pierA.intersects -or $pierB.intersects -or $pierB.distance_cm -gt 0.01){throw 'Expected sampled overlap and corrected clear near-contact.'}
if ($baseA.distance_cm -gt 0.01 -or $baseB.distance_cm -lt 35 -or $delta -lt 35 -or $delta -gt 55){throw 'Scene is not visibly distinguishable.'}
if ([Math]::Abs($delta-[double]::Parse($b.pier_height,[Globalization.CultureInfo]::InvariantCulture)) -gt 0.03){throw 'Height correction differs from native pier height.'}
foreach ($row in $layout) {
    foreach ($post in [int]$row.post1,[int]$row.post2) {
        $join=Contact $final $post $row.base
        if ($join.distance_cm -gt 0.01){throw 'Base is disconnected from a post.'}
    }
}
$pierA | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Stage 'contact-control-pier.json')
$pierB | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Stage 'contact-corrected-pier.json')
$baseB | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $Stage 'contact-corrected-base.json')
if (($before -join "`n") -cne ((Snapshot) -join "`n")){throw 'Staging changed game data/INI/DLL.'}
@{stage=$Stage;layout=$layout;entities=$after;difference_cm=$delta;corrected_pier_distance_cm=$pierB.distance_cm;
    game_hashes_unchanged=$true;cli_build=(Get-Item $exe).LastWriteTime.ToString('o');manual_test='pending'} |
    ConvertTo-Json -Depth 22 | Set-Content (Join-Path $Stage 'staging-test.json')
Write-Output "STAGING PASSED: A board [$($a.board)] overlaps narrow pier; B [$($b.board)] clears it, raised $delta cm."
if ($Install) {
    if (Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq (Join-Path $game 'bin/Risen.exe')}){throw 'Close Risen before installation.'}
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $originalHash){throw 'Game source changed since staging.'}
    $backup=$null
    try {
        # No-op property write routes the exact staged scene through project::apply backups/guards.
        $result=@(Invoke-Risen climbable $final $b.board --enabled true --out $installed)
        $result | Write-Output
        $line=$result | Where-Object {$_ -like 'BACKUP: *'} | Select-Object -Last 1
        if (-not $line){throw 'Missing verified backup.'}
        $backup=$line.Substring(8)
        if ((Get-FileHash -LiteralPath (Join-Path $backup '0.before')).Hash -ne $originalHash){throw 'Backup differs from original layer.'}
        $finalHash=(Get-FileHash -LiteralPath $final).Hash
        if ((Get-FileHash -LiteralPath $installed).Hash -ne $finalHash -or
            (Get-FileHash -LiteralPath (Join-Path $backup '0.after')).Hash -ne $finalHash){throw 'Installed/staged/backup-after bytes differ.'}
        $actual=Entities RisenMods_NewLayer_Test
        if (($actual | ConvertTo-Json -Depth 20 -Compress) -cne ($after | ConvertTo-Json -Depth 20 -Compress)){throw 'Installed VFS readback differs.'}
        $changes=@(Compare-Object $before (Snapshot))
        if ($changes.Count -ne 2 -or @($changes | Where-Object {-not $_.InputObject.StartsWith($installed+' ')}).Count){throw 'Unrelated game files changed.'}
        @{backup=$backup;stage=$Stage;layer=$installed;staged=$final;layout=$layout;difference_cm=$delta;
            corrected_pier_distance_cm=$pierB.distance_cm;manual_test='pending';entities=$actual} |
            ConvertTo-Json -Depth 22 | Set-Content (Join-Path $Stage 'installed-test.json')
    } catch {if ($backup){Invoke-Risen world-restore $backup};throw}
    Write-Output "INSTALLED: native A/B height correction scene, only test layer changed. BACKUP: $backup"
}
