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
    Write-Output 'Settings migration fixtures passed'
} finally {
    if ([IO.Path]::GetFullPath((Split-Path $fixture)).TrimEnd('\') -ne [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')) { throw 'Unexpected fixture path; cleanup refused' }
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
