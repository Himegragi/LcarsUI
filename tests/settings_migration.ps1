$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('isoduo-migration-' + [guid]::NewGuid().ToString('N'))
$script = Join-Path $PSScriptRoot '../tools/migrate-settings.ps1'
function Assert($condition, $message) { if (-not $condition) { throw $message } }
try {
    $old = Join-Path $fixture 'LcarsUI/SettingsDemo'
    $new = Join-Path $fixture 'Isoduo/SettingsDemo'
    New-Item -ItemType Directory -Force (Join-Path $old 'backup') | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $old 'settings.json'), [byte[]](0,1,2,255))
    Set-Content -LiteralPath (Join-Path $old 'backup/settings.json') -Value 'previous backup'
    $hiddenBackup = Join-Path $old 'backup/hidden.json'
    Set-Content -LiteralPath $hiddenBackup -Value 'hidden backup'
    (Get-Item -LiteralPath $hiddenBackup).Attributes = [IO.FileAttributes]::Hidden
    & $script -AppDataRoot $fixture
    Assert (([IO.File]::ReadAllBytes((Join-Path $new 'settings.json')) -join ',') -eq '0,1,2,255') 'Copy must preserve bytes'
    Assert (Test-Path -LiteralPath (Join-Path $old 'backup/settings.json')) 'Old backup must remain'
    Assert (Test-Path -LiteralPath (Join-Path $new 'backup/settings.json')) 'Nested backup must be copied'
    Assert ((Get-Content -LiteralPath (Join-Path $new 'backup/hidden.json')) -eq 'hidden backup') 'Hidden backup must be copied'
    Set-Content -LiteralPath (Join-Path $new 'settings.json') -Value 'current'
    $refused = $false
    try { & $script -AppDataRoot $fixture } catch { $refused = $true }
    Assert $refused 'Existing destination must be refused'
    Assert ((Get-Content -LiteralPath (Join-Path $new 'settings.json')) -eq 'current') 'Current data must remain'
    Assert (([IO.File]::ReadAllBytes((Join-Path $old 'settings.json')) -join ',') -eq '0,1,2,255') 'Old data must remain'
    $refused = $false
    try { & $script -AppDataRoot (Join-Path $fixture 'missing') } catch { $refused = $true }
    Assert $refused 'Missing source must be refused'
    foreach ($backupFile in 'backup/settings.json', 'settings.json.bad') {
        $backupRoot = Join-Path $fixture ([guid]::NewGuid().ToString('N'))
        $oldBackup = Join-Path $backupRoot "LcarsUI/SettingsDemo/$backupFile"
        $newBackup = Join-Path $backupRoot "Isoduo/SettingsDemo/$backupFile"
        New-Item -ItemType Directory -Force (Split-Path $oldBackup) | Out-Null
        [IO.File]::WriteAllBytes($oldBackup, [byte[]](0,1,2,255))
        & $script -AppDataRoot $backupRoot
        Assert (([IO.File]::ReadAllBytes($newBackup) -join ',') -eq '0,1,2,255') "Copy backup-only $backupFile byte for byte"
        Assert (([IO.File]::ReadAllBytes($oldBackup) -join ',') -eq '0,1,2,255') "Retain source $backupFile"
        Assert (-not (Test-Path -LiteralPath (Join-Path $backupRoot 'Isoduo/SettingsDemo/settings.json'))) 'Do not promote a backup to active settings'
    }
    $fileRoot = Join-Path $fixture 'destination-file'
    New-Item -ItemType Directory -Force (Join-Path $fileRoot 'LcarsUI/SettingsDemo'), (Join-Path $fileRoot 'Isoduo') | Out-Null
    [IO.File]::WriteAllText((Join-Path $fileRoot 'LcarsUI/SettingsDemo/settings.json'), 'source')
    $destinationFile = Join-Path $fileRoot 'Isoduo/SettingsDemo'
    [IO.File]::WriteAllText($destinationFile, 'existing file')
    $refused = $false
    try { & $script -AppDataRoot $fileRoot } catch { $refused = $true }
    Assert $refused 'Existing destination file must be refused'
    Assert ([IO.File]::ReadAllText($destinationFile) -eq 'existing file') 'Destination file must remain'
    $failures = @()
    foreach ($case in 'source', 'old-parent', 'new-parent', 'root') {
        $caseRoot = Join-Path $fixture $case
        $caseOld = Join-Path $caseRoot 'LcarsUI/SettingsDemo'
        $caseNew = Join-Path $caseRoot 'Isoduo/SettingsDemo'
        New-Item -ItemType Directory -Force $caseOld, (Split-Path $caseNew) | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $caseOld 'settings.json'), [byte[]](0,1,2,255))
        $hidden = switch ($case) {
            'source' { $caseOld }
            'old-parent' { Split-Path $caseOld }
            'new-parent' { Split-Path $caseNew }
            'root' { $caseRoot }
        }
        $item = Get-Item -LiteralPath $hidden -Force
        $item.Attributes = $item.Attributes -bor [IO.FileAttributes]::Hidden
        try {
            & $script -AppDataRoot $caseRoot
            Assert (([IO.File]::ReadAllBytes((Join-Path $caseNew 'settings.json')) -join ',') -eq '0,1,2,255') "Hidden $case must preserve bytes"
            Assert (([IO.File]::ReadAllBytes((Join-Path $caseOld 'settings.json')) -join ',') -eq '0,1,2,255') "Hidden $case must retain source"
            Assert (@(Get-ChildItem -LiteralPath (Split-Path $caseNew) -Filter 'migration-*' -Force).Count -eq 0) "Hidden $case must leave no staging directory"
            Write-Output "PASS Hidden $case"
        } catch {
            $failures += "Hidden ${case}: $($_.Exception.Message)"
            Write-Output "FAIL Hidden ${case}: $($_.Exception.Message)"
        }
    }
    Assert ($failures.Count -eq 0) ($failures -join "`n")
    # Exercise rejection before copying at each link boundary, including hidden links.
    foreach ($case in 'source', 'old-parent', 'new-parent', 'root', 'nested') {
        $caseRoot = Join-Path $fixture ('link-' + $case)
        $target = Join-Path $fixture ('target-' + $case)
        $caseOld = Join-Path $caseRoot 'LcarsUI/SettingsDemo'
        New-Item -ItemType Directory -Force $caseOld, $target | Out-Null
        [IO.File]::WriteAllText((Join-Path $caseOld 'settings.json'), 'source')
        $link = switch ($case) {
            'source' { Join-Path $target 'SettingsDemo' }
            'old-parent' { Join-Path $target 'LcarsUI' }
            'new-parent' { Join-Path $caseRoot 'Isoduo' }
            'root' { Join-Path $target 'profile' }
            'nested' { Join-Path $caseOld 'linked' }
        }
        $linkTarget = switch ($case) {
            'source' { $caseOld }
            'old-parent' { Split-Path $caseOld }
            'new-parent' { $target }
            'root' { $caseRoot }
            'nested' { $target }
        }
        New-Item -ItemType Junction -Path $link -Target $linkTarget | Out-Null
        try {
            (Get-Item -LiteralPath $link -Force).Attributes = [IO.FileAttributes]::Hidden
            $migrationRoot = switch ($case) {
                'source' { $target }  # Supply the required legacy parent below.
                'old-parent' { $target }
                'root' { $link }
                default { $caseRoot }
            }
            if ($case -eq 'source') {
                New-Item -ItemType Directory -Path (Join-Path $target 'LcarsUI') | Out-Null
                # Move only the fixture link, never its target.
                [IO.Directory]::Move($link, (Join-Path $target 'LcarsUI/SettingsDemo'))
                $link = Join-Path $target 'LcarsUI/SettingsDemo'
            }
            $message = ''
            try { & $script -AppDataRoot $migrationRoot } catch { $message = $_.Exception.Message }
            Assert ($message -match 'Linked paths|linked path') "Link $case must be explicitly refused: $message"
            Assert ([IO.File]::ReadAllText((Join-Path $caseOld 'settings.json')) -eq 'source') "Link $case source must remain"
            Assert (-not (Test-Path -LiteralPath (Join-Path $migrationRoot 'Isoduo/SettingsDemo'))) "Link $case must not publish destination"
            Write-Output "PASS Link $case"
        } finally { [IO.Directory]::Delete($link) }
    }
    # A failed copy must remove even a partially populated staging directory.
    $caseRoot = Join-Path $fixture 'copy-failure'
    $caseOld = Join-Path $caseRoot 'LcarsUI/SettingsDemo'
    New-Item -ItemType Directory -Force $caseOld | Out-Null
    [IO.File]::WriteAllText((Join-Path $caseOld 'settings.json'), 'source')
    function Copy-Item {
        param($LiteralPath, $Destination, [switch]$Recurse, [switch]$Force, $ErrorAction)
        Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Recurse -Force
        throw 'Injected copy failure'
    }
    try {
        $message = ''
        try { & $script -AppDataRoot $caseRoot } catch { $message = $_.Exception.Message }
        Assert ($message -eq 'Injected copy failure') 'Copy failure must propagate'
    } finally { Remove-Item Function:\Copy-Item }
    Assert (@(Get-ChildItem -LiteralPath (Join-Path $caseRoot 'Isoduo') -Force).Count -eq 0) 'Failed copy must clean staging and not publish destination'
    Assert ([IO.File]::ReadAllText((Join-Path $caseOld 'settings.json')) -eq 'source') 'Failed copy must retain source'
    Write-Output 'Settings migration fixtures passed'
} finally {
    if ([IO.Path]::GetFullPath((Split-Path $fixture)).TrimEnd('\') -ne [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')) { throw 'Unexpected fixture path; cleanup refused' }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
