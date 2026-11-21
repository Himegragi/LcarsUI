[CmdletBinding()]
param(
    [string]$OutDir,
    [string[]]$Only = @('clipboard', 'ime', 'dpi'),
    [ValidateRange(1, 1000)][int]$DpiRuns = 1,
    [switch]$DryRun,
    [ValidateRange(0, 60000)][int]$PerfDurationMs = 0,
    [string]$EvidenceDir,
    [switch]$PresentationSignedOff
)
$ErrorActionPreference = 'Stop'
if ($Only.Count -eq 1 -and $Only[0] -eq 'v1-final') {
    & "$PSScriptRoot/verify-v1-final.ps1" -OutDir $OutDir -DryRun:$DryRun
    exit $LASTEXITCODE
}
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'build/Release/evidence/idle' }
if (-not $EvidenceDir) { $EvidenceDir = Join-Path $PSScriptRoot 'examples/edit_form/evidence/final-b5535b6' }
$selected = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
if (-not $selected.Count -or @($selected | Where-Object { $_ -notin @('clipboard', 'ime', 'dpi', 'perf', 'presentation', 'real-input', 'uia', 'axe', 'uia-ime', 'uia-dpi', 'uia-stress', 'demo-uia', 'demo') }).Count) {
    throw '-Only contains an unknown verification selection'
}
if (@($selected | Select-Object -Unique).Count -ne $selected.Count) { throw '-Only must not contain duplicates' }
Write-Host 'Precondition: stop using the computer; set the displays to the required scales before running.'
if ('demo' -in $selected) {
    & (Join-Path $PSScriptRoot 'tests/v1-final/demo.ps1') -OutDir (Join-Path $OutDir 'demo') -DryRun:$DryRun
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$exe = Join-Path $PSScriptRoot 'build/Release/isoduo_host_verify.exe'
if (-not $DryRun -and @($selected | Where-Object { $_ -in @('clipboard', 'ime', 'dpi', 'perf', 'presentation') }).Count -and -not (Test-Path -LiteralPath $exe)) { throw "Verifier not found: $exe" }
$failed = $false
$blocked = $false
$signOffPending = $false
if (-not $DryRun -and @($selected | Where-Object { $_ -in @('perf', 'presentation', 'real-input') }).Count) {
    . "$PSScriptRoot/cmake/SourceRevision.ps1"
    $revision = (Get-IsoduoSourceRevision).Commit
}
if (-not $DryRun -and @($selected | Where-Object { $_ -in @('presentation', 'real-input') }).Count) {
    $revisionFile = Join-Path $PSScriptRoot 'build/Release/source-revision.txt'
    foreach ($name in @('presentation', 'real-input')) {
        if ($name -notin $selected) { continue }
        $folder = Join-Path $OutDir $name
        New-Item -ItemType Directory -Force -Path $folder | Out-Null
        $evidenceRevision = Join-Path $folder 'source-revision.txt'
        if (Test-Path -LiteralPath $revisionFile) {
            Copy-Item -LiteralPath $revisionFile -Destination $evidenceRevision
        } elseif (Test-Path -LiteralPath $evidenceRevision) {
            Remove-Item -LiteralPath $evidenceRevision
        }
    }
    if (Test-Path -LiteralPath $revisionFile) {
        $builtCommit = (Get-Content -LiteralPath $revisionFile | Where-Object { $_ -like 'commit=*' }) -replace '^commit=', ''
        if ($builtCommit -ne $revision) { Write-Warning "Release build revision $builtCommit differs from current HEAD $revision." }
    } else { Write-Warning "Release build revision is missing. Rebuild with ./build.ps1 -Configuration Release." }
}
if ('real-input' -in $selected) {
    $folder = Join-Path $OutDir 'real-input'
    $tsv = Join-Path $folder 'real-input.tsv'
    if (-not $DryRun -and (Test-Path -LiteralPath $tsv)) { Remove-Item -LiteralPath $tsv }
    & (Join-Path $PSScriptRoot 'tests/real-input/verify.ps1') -OutDir $folder -DryRun:$DryRun
    if (-not $DryRun) {
        $runnerFailed = $LASTEXITCODE -notin @(0, 3)
        $rows = if (Test-Path -LiteralPath $tsv) { Import-Csv -Delimiter "`t" -LiteralPath $tsv }
        $pass = @($rows | Where-Object { $_.status -eq 'PASS' }).Count
        $fail = @($rows | Where-Object { $_.status -eq 'FAIL' }).Count
        $unavailable = @($rows | Where-Object { $_.status -eq 'BLOCKED' }).Count
        $complete = $pass + $fail + $unavailable -eq 8 -and @($rows).Count -eq 8
        $state = if ($fail -or $runnerFailed -or -not $complete) { $failed = $true; 'FAIL' }
            elseif ($unavailable) { $blocked = $true; 'BLOCKED' }
            else { 'PASS' }
        Write-Host "real-input: $state ($pass PASS / $fail FAIL / $unavailable BLOCKED)"
    }
}
foreach ($name in @('uia', 'axe', 'uia-ime', 'uia-dpi', 'uia-stress', 'demo-uia')) {
    if ($name -notin $selected) { continue }
    $runs = if ($name -eq 'uia-stress') { 20 } else { 1 }
    for ($i = 1; $i -le $runs; $i++) {
        foreach ($appearance in @('web', 'material')) {
            $relative = if ($name -eq 'uia-stress') { "$name/run-$i/$appearance" } else { "$name/$appearance" }
            $folder = Join-Path $OutDir $relative
            $tsv = Join-Path $(if ($DryRun) { Join-Path $EvidenceDir $relative } else { $folder }) "$name.tsv"
            $runFailure = $false
            if (-not $DryRun) {
                New-Item -ItemType Directory -Force -Path $folder | Out-Null
                if (Test-Path -LiteralPath $tsv) { Remove-Item -LiteralPath $tsv }
                & (Join-Path $PSScriptRoot 'tests/uia/verify.ps1') -Mode $name -Appearance $appearance -OutDir $folder
                if ($LASTEXITCODE -ne 0) { $runFailure = $true }
            }
            if (-not (Test-Path -LiteralPath $tsv)) { Write-Warning "Missing TSV: $tsv"; $runFailure = $true }
            else {
                $rows = @(Import-Csv -Delimiter "`t" -LiteralPath $tsv)
                if (-not $rows.Count -or @($rows | Where-Object { $_.status -eq 'PASS' }).Count -eq 0 -or @($rows | Where-Object { $_.status -notin @('PASS', 'N/A') }).Count) { $runFailure = $true }
            }
            if ($runFailure) { $failed = $true }
            $state = if ($runFailure) { 'FAIL' } else { 'PASS' }
            Write-Host "${relative}: $state"
        }
    }
}
if ('perf' -in $selected) {
    # Cases: PM01-PM05 (spec 1 showcase performance)
    if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'perf') | Out-Null
        @("commit=$revision", "configuration=Release", "duration_override_ms=$PerfDurationMs", '4k=window if available; otherwise offscreen FBO without compositor cost') | Set-Content -Encoding UTF8 (Join-Path $OutDir 'perf/metadata.txt')
    }
    foreach ($appearance in @('web', 'material')) {
        foreach ($size in @('1080', '4k')) {
            # slider-drag-text-size (#73) is report-only: it feeds PM01-PM05 nothing and decides nothing.
            # idle-after-animation (#75) gates like idle: the idle rule must hold after one slider transition has played.
            $scenarios = if ($size -eq '4k') { @('list-hold-down', 'typing', 'delay') } else { @('startup', 'list-hold-down', 'typing', 'idle', 'delay', 'slider-drag-text-size', 'idle-after-animation') }
            foreach ($scenario in $scenarios) {
                $folder = Join-Path $OutDir "perf/$appearance/$size/$scenario"
                $tsv = Join-Path $folder 'perf.tsv'
                $runFailure = $false
                if ($DryRun) { $tsv = Join-Path $EvidenceDir "perf/$appearance/$size/$scenario/perf.tsv" }
                else {
                    New-Item -ItemType Directory -Force -Path $folder | Out-Null
                    if (Test-Path -LiteralPath $tsv) { Remove-Item -LiteralPath $tsv }
                    $case = if ($scenario -eq 'delay') { 'list-hold-down' } else { $scenario }
                    $runArgs = @('--perf', $case, '--perf-appearance', $appearance, '--evidence', $folder)
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
                    $reportOnly = $scenario -eq 'slider-drag-text-size'
                    if ($rows.Count -ne 2 -or @($rows | Where-Object { ($_.status -ne 'PASS' -and -not $reportOnly) -or ($reportOnly -and $_.status -ne 'REPORT') -or $_.scenario -ne $expectedCase -or $_.appearance -ne $appearance -or $_.resolution -ne $expectedSize }).Count -or @($rows | Where-Object { $_.phase -eq 'warmup' }).Count -ne 1 -or @($rows | Where-Object { $_.phase -eq 'steady' }).Count -ne 1) { $runFailure = $true }
                }
                if ($scenario -eq 'slider-drag-text-size') {
                    # Report only: a missing or malformed report is shown, but never fails the PM01-PM05 verdict.
                    $state = if ($runFailure) { 'REPORT MISSING (not gating)' } else { 'REPORT (no pass/fail)' }
                    Write-Host "perf/$appearance/$size/${scenario}: $state"
                    continue
                }
                if ($runFailure) { $failed = $true }
                $state = if ($runFailure) { 'FAIL' } else { 'PASS' }
                Write-Host "perf/$appearance/$size/${scenario}: $state"
            }
        }
    }
}
foreach ($name in @('clipboard', 'ime', 'dpi', 'presentation')) {
    if ($name -notin $selected) { continue }
    if ($name -eq 'presentation' -and $DryRun) {
        Write-Host 'presentation: PR01 IME, PR02 DPI, PR03 per-user fonts, PR04 trial font-switch timing (report only); web and material'
        continue
    }
    $count = if ($name -eq 'dpi') { $DpiRuns } else { 1 }
    for ($i = 1; $i -le $count; $i++) {
        $pass = 0; $fail = 0; $other = 0; $runFailure = $false
        $unavailable = 0; $reported = 0
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
                if ($name -eq 'presentation') {
                    $unavailable = @($rows | Where-Object { $_.status -eq 'BLOCKED' }).Count
                    $reported = @($rows | Where-Object { $_.status -eq 'REPORT' }).Count
                    $other -= $unavailable + $reported
                }
                if (-not $pass) { $runFailure = $true }
            }
        }
        $state = if ($fail -or $other -or $runFailure) { $failed = $true; 'FAIL' }
            elseif ($unavailable) { $blocked = $true; 'BLOCKED' }
            elseif ($name -eq 'presentation' -and -not $PresentationSignedOff) { $signOffPending = $true; 'SIGN-OFF PENDING' }
            else { 'PASS' }
        $label = if ($name -eq 'dpi' -and $count -gt 1) { "dpi/run-$i" } else { $name }
        if ($name -eq 'presentation') {
            Write-Host "presentation: $state ($pass PASS / $fail FAIL / $unavailable BLOCKED / $reported REPORT / $other other)"
        } else {
            Write-Host ("{0}: {1} ({2} PASS / {3} FAIL / {4} other)" -f $label, $state, $pass, $fail, $other)
        }
    }
}
if ($failed) { exit 1 }
if ($blocked) { exit 3 }
if ($signOffPending) { exit 2 }
exit 0
