function Get-IsoduoSourceRevision([switch]$IncludeDirty) {
    $root = Split-Path -Parent $PSScriptRoot
    $gitEntry = Get-Item -Force -LiteralPath (Join-Path $root '.git')
    $gitPath = if ($gitEntry.PSIsContainer) { $gitEntry.FullName } else {
        $path = (Get-Content -LiteralPath $gitEntry.FullName -TotalCount 1) -replace '^gitdir: ', ''
        if ([IO.Path]::IsPathRooted($path)) { $path } else { Join-Path $root $path }
    }
    $program = 'git'
    $gitArgs = @("--git-dir=$gitPath", "--work-tree=$root")
    if (Get-Command wsl.exe -ErrorAction SilentlyContinue) {
        # Windows CI can have wsl.exe on PATH without a Linux distribution.
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $linuxWork = & wsl.exe -e wslpath -u $root 2>$null
        } finally { $ErrorActionPreference = $previous }
        if ($LASTEXITCODE -eq 0 -and $linuxWork) {
            $linuxGit = & wsl.exe -e wslpath -u $gitPath 2>$null
            if ($LASTEXITCODE -ne 0 -or -not $linuxGit) { throw 'Cannot translate Git directory to WSL' }
            $program = 'wsl.exe'
            $gitArgs = @('-e', 'git', "--git-dir=$linuxGit", "--work-tree=$linuxWork", '-c', 'core.autocrlf=true')
        }
    }
    $commit = & $program @gitArgs rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or -not $commit) { throw 'Cannot record source revision' }
    $dirty = $null
    if ($IncludeDirty) {
        $changes = & $program @gitArgs status --porcelain
        if ($LASTEXITCODE -ne 0) { throw 'Cannot record source working tree status' }
        $dirty = [bool]$changes
    }
    [pscustomobject]@{ Commit = $commit.Trim(); Dirty = $dirty }
}
