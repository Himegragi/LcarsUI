# Run Plan first; stop all Git/T3 operations before Apply or Restore.
# No Git commands, global configuration or T3 records are changed by this tool.
param(
    [Parameter(Mandatory=$true)][string]$Worktree,
    [Parameter(Mandatory=$true)][string]$Bridge,
    [Parameter(Mandatory=$true)][string]$Backup,
    [ValidateSet('Plan','Apply','Restore')][string]$Mode = 'Plan'
)
$ErrorActionPreference = 'Stop'
$Worktree = [IO.Path]::GetFullPath($Worktree)
$Bridge = [IO.Path]::GetFullPath($Bridge)
$Backup = [IO.Path]::GetFullPath($Backup)
$dotGit = Join-Path $Worktree '.git'
function RelativePath($fromDirectory, $to) {
    $from = [uri]($fromDirectory.TrimEnd('\','/') + '/')
    $relative = $from.MakeRelativeUri([uri]$to)
    if ($relative.IsAbsoluteUri) { throw 'Bridge must be on the worktree volume' }
    [uri]::UnescapeDataString($relative.ToString())
}
function WriteUtf8($path, $value) { [IO.File]::WriteAllText($path, $value, (New-Object Text.UTF8Encoding($false))) }
if ($Mode -eq 'Restore') {
    $state = Get-Content -LiteralPath (Join-Path $Backup 'manifest.json') -Raw | ConvertFrom-Json
    if ($state.Worktree -ne $Worktree -or $state.Bridge -ne $Bridge) { throw 'Backup belongs to another repair' }
    if ([IO.File]::ReadAllText($dotGit) -ne $state.Forward -or [IO.File]::ReadAllText($state.ReverseFile) -ne $state.Reverse) { throw 'Metadata changed since repair; restore refused' }
    # Leave bridge junctions in place: restoration never recursively deletes anything.
    [IO.File]::WriteAllBytes($dotGit, [IO.File]::ReadAllBytes((Join-Path $Backup 'dotgit.bin')))
    [IO.File]::WriteAllBytes($state.ReverseFile, [IO.File]::ReadAllBytes((Join-Path $Backup 'gitdir.bin')))
    Write-Output 'Restored original metadata bytes; bridge junctions and backup retained.'
    return
}
$content = [IO.File]::ReadAllText($dotGit).Trim()
if ($content -notmatch '^gitdir: (.+)$') { throw 'Expected a linked worktree .git file' }
$admin = $Matches[1]
if (-not [IO.Path]::IsPathRooted($admin)) { $admin = Join-Path $Worktree $admin }
$admin = [IO.Path]::GetFullPath($admin)
$common = [IO.Path]::GetFullPath((Join-Path $admin ([IO.File]::ReadAllText((Join-Path $admin 'commondir')).Trim())))
if ((Split-Path $admin) -ne (Join-Path $common 'worktrees')) { throw 'Unexpected worktree registration layout' }
$reverseFile = Join-Path $admin 'gitdir'
$oldReverse = [IO.File]::ReadAllText($reverseFile).Trim()
if (-not [IO.Path]::IsPathRooted($oldReverse)) { $oldReverse = Join-Path $admin $oldReverse }
if ([IO.Path]::GetFullPath($oldReverse) -ne $dotGit) { throw 'Reverse pointer does not match worktree' }
if ([IO.Path]::GetPathRoot($Bridge) -ne [IO.Path]::GetPathRoot($Worktree)) { throw 'Bridge must be on the worktree volume' }
$backBridge = Join-Path $admin 'interop-worktree'
$forward = 'gitdir: ' + (RelativePath $Worktree (Join-Path $Bridge ('worktrees/' + (Split-Path $admin -Leaf)))) + "`n"
$reverse = "interop-worktree/.git`n"
Write-Output "Forward: $dotGit -> $forward"
Write-Output "Junction: $Bridge -> $common"
Write-Output "Reverse: $reverseFile -> $reverse"
Write-Output "Junction: $backBridge -> $Worktree"
Write-Output "Backup: $Backup"
if ($Mode -eq 'Plan') { return }
foreach ($path in $Bridge, $backBridge, $Backup) {
    if (Test-Path -LiteralPath $path) { throw "Path already exists; choose fresh paths: $path" }
}
New-Item -ItemType Directory -Path $Backup | Out-Null
[IO.File]::WriteAllBytes((Join-Path $Backup 'dotgit.bin'), [IO.File]::ReadAllBytes($dotGit))
[IO.File]::WriteAllBytes((Join-Path $Backup 'gitdir.bin'), [IO.File]::ReadAllBytes($reverseFile))
@{ Worktree=$Worktree; Bridge=$Bridge; ReverseFile=$reverseFile; Forward=$forward; Reverse=$reverse } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Backup 'manifest.json') -Encoding UTF8
try {
    New-Item -ItemType Junction -Path $Bridge -Target $common | Out-Null
    New-Item -ItemType Junction -Path $backBridge -Target $Worktree | Out-Null
    WriteUtf8 $dotGit $forward
    WriteUtf8 $reverseFile $reverse
} catch {
    [IO.File]::WriteAllBytes($dotGit, [IO.File]::ReadAllBytes((Join-Path $Backup 'dotgit.bin')))
    [IO.File]::WriteAllBytes($reverseFile, [IO.File]::ReadAllBytes((Join-Path $Backup 'gitdir.bin')))
    throw
}
Write-Output 'Applied relative pointers. Validate Windows path resolution and WSL Git before resuming tools.'
