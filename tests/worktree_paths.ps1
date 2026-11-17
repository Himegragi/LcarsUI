# Explicit fresh directories only. Retain fixtures as review evidence; never prune/remove.
param([Parameter(Mandatory=$true)][string]$FixtureRoot,
      [Parameter(Mandatory=$true)][string]$OtherVolumeRoot,
      [string]$WindowsGitExecutable)
$ErrorActionPreference = 'Stop'
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function LinuxPath($path) { (& wsl.exe -e wslpath -a $path).Trim() }
function Git($arguments) {
    & wsl.exe -e git @arguments
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git failed: $arguments" }
}
function WriteExistingBytes($path, [byte[]]$bytes) {
    $stream = [IO.File]::OpenWrite($path)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.SetLength($bytes.Length) }
    finally { $stream.Dispose() }
}
foreach ($path in $FixtureRoot, $OtherVolumeRoot) {
    Assert (-not (Test-Path -LiteralPath $path)) "Use a fresh fixture path: $path"
    New-Item -ItemType Directory -Path $path | Out-Null
}
$tool = Join-Path $PSScriptRoot '../tools/repair-worktree-paths.ps1'
$repo = Join-Path $OtherVolumeRoot 'repo'
Git @('init', (LinuxPath $repo))
Git @('-C', (LinuxPath $repo), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '--allow-empty', '-m', 'fixture')
foreach ($case in 'cross-volume', 'same-volume') {
    $base = if ($case -eq 'cross-volume') { $FixtureRoot } else { $OtherVolumeRoot }
    $wt = Join-Path $base $case
    Git @('-C', (LinuxPath $repo), 'worktree', 'add', (LinuxPath $wt), '-b', $case)
    $admin = Join-Path $repo ('.git/worktrees/' + $case)
    $dotGit = Join-Path $wt '.git'
    $reverseFile = Join-Path $admin 'gitdir'
    # Reproduce the Windows absolute-path metadata produced by the host.
    [IO.File]::WriteAllText($dotGit, "gitdir: $admin`n")
    [IO.File]::WriteAllText($reverseFile, "$dotGit`n")
    $before = [IO.File]::ReadAllBytes($dotGit) -join ','
    $reverseBefore = [IO.File]::ReadAllBytes($reverseFile) -join ','
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & wsl.exe -e git -C (LinuxPath $wt) rev-parse --show-toplevel 2>&1 | ForEach-Object { "Expected RED: $_" }
    $red = $LASTEXITCODE
    $ErrorActionPreference = $previous
    Assert ($red -ne 0) 'Windows absolute metadata must reproduce WSL discovery failure'
    $bridge = Join-Path $base ($case + '-bridge')
    $backup = Join-Path $base ($case + '-backup')
    & $tool -Worktree $wt -Bridge $bridge -Backup $backup -Mode Plan
    Assert (([IO.File]::ReadAllBytes($dotGit) -join ',') -eq $before) 'Plan must not change metadata'
    Assert (([IO.File]::ReadAllBytes($reverseFile) -join ',') -eq $reverseBefore) 'Plan must not change reverse pointer'
    Assert (-not (Test-Path -LiteralPath $bridge)) 'Plan must not create bridge'
    Assert (-not (Test-Path -LiteralPath $backup)) 'Plan must not create backup'
    # T3 creates a hidden .git file; rewriting and restoring must preserve its attributes.
    foreach ($metadata in $dotGit, $reverseFile) {
        $item = Get-Item -LiteralPath $metadata -Force
        $item.Attributes = $item.Attributes -bor [IO.FileAttributes]::Hidden
    }
    & $tool -Worktree $wt -Bridge $bridge -Backup $backup -Mode Apply
    # Windows/.NET resolves both relative pointers to the expected bytes through junctions.
    $forward = [IO.File]::ReadAllText($dotGit).Trim().Substring(8)
    $resolvedAdmin = [IO.Path]::GetFullPath((Join-Path $wt $forward))
    Assert ([IO.File]::ReadAllText((Join-Path $resolvedAdmin 'HEAD')) -eq [IO.File]::ReadAllText((Join-Path $admin 'HEAD'))) 'Windows forward resolution'
    $resolvedReverse = [IO.Path]::GetFullPath((Join-Path $admin ([IO.File]::ReadAllText($reverseFile).Trim())))
    Assert ([IO.File]::ReadAllText($resolvedReverse) -eq [IO.File]::ReadAllText($dotGit)) 'Windows reverse resolution'
    Git @('-C', (LinuxPath $wt), 'rev-parse', '--show-toplevel')
    Git @('-C', (LinuxPath $wt), 'status', '--porcelain')
    $list = & wsl.exe -e git -C (LinuxPath $repo) worktree list --porcelain
    Assert ($LASTEXITCODE -eq 0) 'Worktree list must succeed'
    $records = @((($list -join "`n") -split "`n`n") | Where-Object { $_ -match ('(?m)^branch refs/heads/' + [regex]::Escape($case) + '$') })
    Assert ($records.Count -eq 1) 'Repaired registration must be present'
    Assert ($records[0] -notmatch '(?m)^prunable') 'Repaired registration must not be prunable'
    if ($WindowsGitExecutable) {
        # Compatibility probe only: native Windows Git runs through WSL without index writes.
        $top = & wsl.exe -e $WindowsGitExecutable --no-optional-locks -C $wt rev-parse --show-toplevel
        Assert ($LASTEXITCODE -eq 0) 'Windows Git discovery must succeed'
        Assert ([IO.Path]::GetFullPath($top.Trim()) -eq [IO.Path]::GetFullPath($wt)) 'Windows Git must retain original worktree path'
        $winList = & wsl.exe -e $WindowsGitExecutable --no-optional-locks -C $repo worktree list --porcelain
        Assert ($LASTEXITCODE -eq 0) 'Windows Git worktree list must succeed'
        $winRecords = @((($winList -join "`n") -split "`n`n") | Where-Object { $_ -match ('(?m)^branch refs/heads/' + [regex]::Escape($case) + '$') })
        Assert ($winRecords.Count -eq 1 -and $winRecords[0] -notmatch '(?m)^prunable') 'Windows Git must retain valid registration'
    }
    foreach ($metadata in $dotGit, $reverseFile) {
        Assert ((Get-Item -LiteralPath $metadata -Force).Attributes -band [IO.FileAttributes]::Hidden) 'Apply must preserve Hidden attributes'
    }
    [IO.File]::WriteAllText((Join-Path $wt 'fixture.txt'), 'fixture')
    Git @('-C', (LinuxPath $wt), 'add', 'fixture.txt')
    Git @('-C', (LinuxPath $wt), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'write through repaired discovery')
    $repaired = [IO.File]::ReadAllBytes($dotGit)
    WriteExistingBytes $dotGit ($repaired + [Text.Encoding]::UTF8.GetBytes('# concurrent edit'))
    $refused = $false
    try { & $tool -Worktree $wt -Bridge $bridge -Backup $backup -Mode Restore } catch { $refused = $_.Exception.Message -match 'Metadata changed' }
    Assert $refused 'Restore must refuse concurrent edits'
    WriteExistingBytes $dotGit $repaired
    & $tool -Worktree $wt -Bridge $bridge -Backup $backup -Mode Restore
    Assert (([IO.File]::ReadAllBytes($dotGit) -join ',') -eq $before) 'Restore forward bytes'
    Assert (([IO.File]::ReadAllBytes($reverseFile) -join ',') -eq $reverseBefore) 'Restore reverse bytes'
    foreach ($metadata in $dotGit, $reverseFile) {
        Assert ((Get-Item -LiteralPath $metadata -Force).Attributes -band [IO.FileAttributes]::Hidden) 'Restore must preserve Hidden attributes'
    }
    Write-Output "PASS $case (restore byte-for-byte verified)"
}
