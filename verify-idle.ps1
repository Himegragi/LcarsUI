[CmdletBinding()]
param(
    [string]$OutDir,
    [string[]]$Only = @('clipboard', 'ime', 'dpi'),
    [ValidateRange(1, 1000)][int]$DpiRuns = 1,
    [switch]$DryRun,
    [ValidateRange(0, 60000)][int]$PerfDurationMs = 0,
    [string]$EvidenceDir
)
$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'build/Release/evidence/idle' }
if (-not $EvidenceDir) { $EvidenceDir = Join-Path $PSScriptRoot 'examples/edit_form/evidence/final-b5535b6' }
$selected = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
if (-not $selected.Count -or @($selected | Where-Object { $_ -notin @('clipboard', 'ime', 'dpi', 'perf', 'presentation') }).Count) {
    throw '-Only must contain clipboard, ime, dpi, perf, and/or presentation'
}
if (@($selected | Select-Object -Unique).Count -ne $selected.Count) { throw '-Only must not contain duplicates' }
Write-Host 'Precondition: stop using the computer; set the displays to the required scales before running.'
$exe = Join-Path $PSScriptRoot 'build/Release/lcars_host_verify.exe'
if (-not $DryRun -and -not (Test-Path -LiteralPath $exe)) { throw "Verifier not found: $exe" }
$failed = $false
if ('perf' -in $selected) {
    if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'perf') | Out-Null
        $gitEntry = Join-Path $PSScriptRoot '.git'
        $gitPath = if ((Get-Item -Force -LiteralPath $gitEntry).PSIsContainer) { $gitEntry } else { ((Get-Content -LiteralPath $gitEntry -TotalCount 1) -replace '^gitdir: ', '') }
        $linuxGit = (& wsl.exe -e wslpath -u $gitPath).Trim()
        $linuxWork = (& wsl.exe -e wslpath -u $PSScriptRoot).Trim()
        $revision = (& wsl.exe -e git "--git-dir=$linuxGit" "--work-tree=$linuxWork" rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $revision) { throw 'Cannot record source revision' }
        @("commit=$revision", "configuration=Release", "duration_override_ms=$PerfDurationMs", '4k=window if available; otherwise offscreen FBO without compositor cost') | Set-Content -Encoding UTF8 (Join-Path $OutDir 'perf/metadata.txt')
    }
    foreach ($skin in @('web', 'material')) {
        foreach ($size in @('1080', '4k')) {
            $scenarios = if ($size -eq '4k') { @('list-hold-down', 'typing', 'delay') } else { @('startup', 'list-hold-down', 'typing', 'idle', 'delay') }
            foreach ($scenario in $scenarios) {
                $folder = Join-Path $OutDir "perf/$skin/$size/$scenario"
                $tsv = Join-Path $folder 'perf.tsv'
                $runFailure = $false
                if ($DryRun) { $tsv = Join-Path $EvidenceDir "perf/$skin/$size/$scenario/perf.tsv" }
                else {
                    New-Item -ItemType Directory -Force -Path $folder | Out-Null
                    if (Test-Path -LiteralPath $tsv) { Remove-Item -LiteralPath $tsv }
                    $case = if ($scenario -eq 'delay') { 'list-hold-down' } else { $scenario }
                    $runArgs = @('--perf', $case, '--perf-skin', $skin, '--evidence', $folder)
                    if ($size -eq '4k') { $runArgs += '--perf-large' }
                    if ($scenario -eq 'delay') { $runArgs += @('--paint-delay-ms', '20') }
                    if ($PerfDurationMs) { $runArgs += @('--perf-duration-ms', "$PerfDurationMs") }
                    $previous = $ErrorActionPreference
                    try {
                        $ErrorActionPreference = 'Continue'
                        & $exe @runArgs 2>&1 | ForEach-Object { "$_" } | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'console.log')
                        if ($LASTEXITCODE -ne 0) { $runFailure = $true }
                    } finally { $ErrorActionPreference = $previous }
                }
                if (-not (Test-Path -LiteralPath $tsv)) { $runFailure = $true }
                else {
                    $rows = @(Import-Csv -Delimiter "`t" -LiteralPath $tsv)
                    $expectedCase = if ($scenario -eq 'delay') { 'list-hold-down' } else { $scenario }
                    $expectedSize = if ($size -eq '4k') { '3840x2160' } else { '1920x1080' }
                    if ($rows.Count -ne 2 -or @($rows | Where-Object { $_.status -ne 'PASS' -or $_.scenario -ne $expectedCase -or $_.appearance -ne $skin -or $_.resolution -ne $expectedSize }).Count -or @($rows | Where-Object { $_.phase -eq 'warmup' }).Count -ne 1 -or @($rows | Where-Object { $_.phase -eq 'steady' }).Count -ne 1) { $runFailure = $true }
                }
                if ($runFailure) { $failed = $true }
                $state = if ($runFailure) { 'FAIL' } else { 'PASS' }
                Write-Host "perf/$skin/$size/${scenario}: $state"
            }
        }
    }
}
foreach ($name in @('clipboard', 'ime', 'dpi', 'presentation')) {
    if ($name -notin $selected) { continue }
    $count = if ($name -eq 'dpi') { $DpiRuns } else { 1 }
    for ($i = 1; $i -le $count; $i++) {
        $pass = 0; $fail = 0; $other = 0; $runFailure = $false
        $folder = Join-Path $OutDir $name
        if ($name -eq 'dpi' -and $count -gt 1) { $folder = Join-Path $folder "run-$i" }
        if ($DryRun) {
            $sourceName = if ($name -eq 'dpi') { 'dpi-window' } else { $name }
            $tsv = Join-Path (Join-Path $EvidenceDir $sourceName) "$name.tsv"
        } else {
            New-Item -ItemType Directory -Force -Path $folder | Out-Null
            $tsv = Join-Path $folder "$name.tsv"
            # Avoid counting an old report if the verifier aborts before creating one.
            if (Test-Path -LiteralPath $tsv) { Remove-Item -LiteralPath $tsv }
            # @() keeps a single flag as an array so += appends instead of concatenating strings.
            $runArgs = @(switch ($name) {
                'clipboard' { @('--clipboard-only', '--repeat', '100') }
                'ime' { @('--ime') }
                'dpi' { @('--dpi') }
                'presentation' { @('--presentation-idle') }
            })
            $runArgs += @('--evidence', $folder)
            $previous = $ErrorActionPreference
            try {
                $ErrorActionPreference = 'Continue'
                & $exe @runArgs 2>&1 | ForEach-Object { "$_" } | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'console.log')
                if ($LASTEXITCODE -ne 0) { $runFailure = $true }
                if ($name -eq 'presentation') {
                    & $exe '--trial-screenshots' '--evidence' (Join-Path $folder 'trial') 2>&1 | ForEach-Object { "$_" } | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $folder 'trial-console.log')
                    if ($LASTEXITCODE -ne 0) { $runFailure = $true }
                }
            } finally { $ErrorActionPreference = $previous }
        }
        if (-not (Test-Path -LiteralPath $tsv)) {
            Write-Warning "Missing TSV: $tsv"; $runFailure = $true
        } else {
            $rows = @(Import-Csv -Delimiter "`t" -LiteralPath $tsv)
            if (-not $rows.Count -or -not ($rows[0].PSObject.Properties.Name -contains 'status')) {
                $runFailure = $true
            } else {
                $pass = @($rows | Where-Object { $_.status -eq 'PASS' }).Count
                $fail = @($rows | Where-Object { $_.status -eq 'FAIL' }).Count
                $other = @($rows | Where-Object { $_.status -notin @('PASS', 'FAIL', 'N/A') }).Count
                if (-not $pass) { $runFailure = $true }
            }
        }
        if ($fail -or $other -or $runFailure) { $failed = $true }
        $state = if ($fail -or $other -or $runFailure) { 'FAIL' } else { 'PASS' }
        $label = if ($name -eq 'dpi' -and $count -gt 1) { "dpi/run-$i" } else { $name }
        Write-Host ("{0}: {1} ({2} PASS / {3} FAIL / {4} other)" -f $label, $state, $pass, $fail, $other)
    }
}
if ($failed) { exit 1 }
