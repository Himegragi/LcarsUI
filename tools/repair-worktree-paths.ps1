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
$manifest = Join-Path $Backup 'manifest.json'
$forwardBackup = Join-Path $Backup 'dotgit.bin'
$reverseBackup = Join-Path $Backup 'gitdir.bin'

function RelativePath($fromDirectory, $to) {
    $from = [uri]($fromDirectory.TrimEnd('\','/') + '/')
    $relative = $from.MakeRelativeUri([uri]$to)
    if ($relative.IsAbsoluteUri) { throw 'Bridge must be on the worktree volume' }
    [uri]::UnescapeDataString($relative.ToString())
}
function DecodePointer([byte[]]$bytes) {
    $reader = [IO.StreamReader]::new([IO.MemoryStream]::new($bytes))
    try { $reader.ReadToEnd().Trim() } finally { $reader.Dispose() }
}
function ResolvePointer($directory, $pointer) {
    if (-not [IO.Path]::IsPathRooted($pointer)) { $pointer = Join-Path $directory $pointer }
    [IO.Path]::GetFullPath($pointer)
}
function CheckBridge($path, $target) {
    $entry = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    if (-not $entry) { return $false }
    $targets = @($entry.Target)
    if ($entry.LinkType -ne 'Junction' -or $targets.Count -ne 1 -or -not $targets[0] -or
        [IO.Path]::GetFullPath([string]$targets[0]) -ne $target) {
        throw "Expected junction to $target at $path"
    }
    return $true
}
function OpenMetadata($path) {
    # Open in place to preserve attributes, and exclude other writers and deletion.
    [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
}
function CheckMetadata($stream, [byte[]]$original, [byte[]]$repaired) {
    $current = [IO.MemoryStream]::new()
    try {
        $stream.CopyTo($current)
        $bytes = [Convert]::ToBase64String($current.ToArray())
    } finally { $current.Dispose() }
    if ($bytes -ceq [Convert]::ToBase64String($original)) { return 'Original' }
    if ($bytes -ceq [Convert]::ToBase64String($repaired)) { return 'Repaired' }
    throw "Metadata changed; refusing to overwrite $($stream.Name)"
}
function WriteMetadata($stream, [byte[]]$bytes) {
    $stream.Position = 0
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.SetLength($bytes.Length)
    $stream.Flush($true)
}

$hasBackup = Test-Path -LiteralPath $manifest
if ($hasBackup) {
    $state = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($state.Worktree -ne $Worktree -or $state.Bridge -ne $Bridge) { throw 'Backup belongs to another repair' }
    $originalForward = [IO.File]::ReadAllBytes($forwardBackup)
    $originalReverse = [IO.File]::ReadAllBytes($reverseBackup)
} else {
    if ($Mode -eq 'Restore') { throw "No complete backup at $Backup" }
    if (Test-Path -LiteralPath $Backup) { throw "Incomplete backup; choose a fresh backup path: $Backup" }
    $originalForward = [IO.File]::ReadAllBytes($dotGit)
}
$content = DecodePointer $originalForward
if ($content -notmatch '^gitdir: (.+)$') { throw 'Expected a linked worktree .git file' }
$admin = ResolvePointer $Worktree $Matches[1]
$common = ResolvePointer $admin ([IO.File]::ReadAllText((Join-Path $admin 'commondir')).Trim())
if ((Split-Path $admin) -ne (Join-Path $common 'worktrees')) { throw 'Unexpected worktree registration layout' }
$reverseFile = Join-Path $admin 'gitdir'
if (-not $hasBackup) { $originalReverse = [IO.File]::ReadAllBytes($reverseFile) }
if ((ResolvePointer $admin (DecodePointer $originalReverse)) -ne $dotGit) { throw 'Reverse pointer does not match worktree' }
if ([IO.Path]::GetPathRoot($Bridge) -ne [IO.Path]::GetPathRoot($Worktree)) { throw 'Bridge must be on the worktree volume' }
$backBridge = Join-Path $admin 'interop-worktree'
$forward = 'gitdir: ' + (RelativePath $Worktree (Join-Path $Bridge ('worktrees/' + (Split-Path $admin -Leaf)))) + "`n"
$reverse = "interop-worktree/.git`n"
if ($hasBackup -and ($state.ReverseFile -ne $reverseFile -or $state.Forward -cne $forward -or $state.Reverse -cne $reverse)) {
    throw 'Backup does not match expected repair metadata'
}
Write-Output "Forward: $dotGit -> $forward"
Write-Output "Junction: $Bridge -> $common"
Write-Output "Reverse: $reverseFile -> $reverse"
Write-Output "Junction: $backBridge -> $Worktree"
Write-Output "Backup: $Backup"
if ($Mode -eq 'Plan') { return }

$repairedForward = [Text.Encoding]::UTF8.GetBytes($forward)
$repairedReverse = [Text.Encoding]::UTF8.GetBytes($reverse)
$forwardStream = OpenMetadata $dotGit
try {
    $reverseStream = OpenMetadata $reverseFile
    try {
        $forwardState = CheckMetadata $forwardStream $originalForward $repairedForward
        $reverseState = CheckMetadata $reverseStream $originalReverse $repairedReverse
        if ($Mode -eq 'Restore') {
            if ($forwardState -eq 'Repaired') { WriteMetadata $forwardStream $originalForward }
            if ($reverseState -eq 'Repaired') { WriteMetadata $reverseStream $originalReverse }
            Write-Output 'Restored original metadata bytes; bridge junctions and backup retained.'
            return
        }

        $hasBridge = CheckBridge $Bridge $common
        $hasBackBridge = CheckBridge $backBridge $Worktree
        if (-not $hasBackup) {
            New-Item -ItemType Directory -Path $Backup | Out-Null
            [IO.File]::WriteAllBytes($forwardBackup, $originalForward)
            [IO.File]::WriteAllBytes($reverseBackup, $originalReverse)
            @{ Worktree=$Worktree; Bridge=$Bridge; ReverseFile=$reverseFile; Forward=$forward; Reverse=$reverse } |
                ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
        }
        if (-not $hasBridge) { New-Item -ItemType Junction -Path $Bridge -Target $common | Out-Null }
        if (-not $hasBackBridge) { New-Item -ItemType Junction -Path $backBridge -Target $Worktree | Out-Null }
        if ($forwardState -eq 'Original') { WriteMetadata $forwardStream $repairedForward }
        if ($reverseState -eq 'Original') { WriteMetadata $reverseStream $repairedReverse }
    } finally { $reverseStream.Dispose() }
} finally { $forwardStream.Dispose() }
Write-Output 'Applied relative pointers. Validate Windows path resolution and WSL Git before resuming tools.'
