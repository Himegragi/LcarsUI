param([string]$AppDataRoot = $env:APPDATA)
$ErrorActionPreference = 'Stop'
if (-not $AppDataRoot) { throw 'AppDataRoot is required' }
$root = [IO.Path]::GetFullPath($AppDataRoot)
$source = Join-Path $root 'LcarsUI/SettingsDemo'
$destination = Join-Path $root 'Isoduo/SettingsDemo'
if (Test-Path -LiteralPath $destination) { throw "Destination already exists; nothing changed: $destination" }
if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "No previous settings directory found: $source" }
# Refuse links so the copy never follows a junction outside the selected profile.
foreach ($entry in @((Get-Item -LiteralPath $root -Force), (Get-Item -LiteralPath (Split-Path $source) -Force), (Get-Item -LiteralPath $source -Force)) + @(Get-ChildItem -LiteralPath $source -Recurse -Force)) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked paths cannot be migrated: $($entry.FullName)" }
}
$parent = Split-Path $destination
New-Item -ItemType Directory -Force -Path $parent | Out-Null
if ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Destination parent is a linked path' }
$stage = Join-Path $parent ('migration-' + [guid]::NewGuid().ToString('N'))
try {
    Copy-Item -LiteralPath $source -Destination $stage -Recurse -Force -ErrorAction Stop
    # Directory.Move refuses an existing destination, including one created during the copy.
    [IO.Directory]::Move($stage, $destination)
} finally {
    if ([IO.Path]::GetFullPath((Split-Path $stage)) -ne [IO.Path]::GetFullPath($parent)) { throw 'Unexpected staging path; cleanup refused' }
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
Write-Output "Copied settings to $destination. Backup retained at $source."
