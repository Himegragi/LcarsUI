[CmdletBinding()]
param(
    [string]$OutDir,
    [string[]]$Only = @('clipboard', 'ime', 'dpi'),
    [ValidateRange(1, 1000)][int]$DpiRuns = 1,
    [switch]$DryRun,
    [string]$EvidenceDir
)
$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot 'build/Release/evidence/idle' }
if (-not $EvidenceDir) { $EvidenceDir = Join-Path $PSScriptRoot 'examples/edit_form/evidence/final-b5535b6' }
$selected = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
if (-not $selected.Count -or @($selected | Where-Object { $_ -notin @('clipboard', 'ime', 'dpi') }).Count) {
    throw '-Only must contain clipboard, ime, and/or dpi'
}
if (@($selected | Select-Object -Unique).Count -ne $selected.Count) { throw '-Only must not contain duplicates' }
Write-Host 'Precondition: stop using the computer; set the displays to the required scales before running.'
$exe = Join-Path $PSScriptRoot 'build/Release/lcars_host_verify.exe'
if (-not $DryRun -and -not (Test-Path -LiteralPath $exe)) { throw "Verifier not found: $exe" }
$failed = $false
foreach ($name in @('clipboard', 'ime', 'dpi')) {
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
            $runArgs = switch ($name) {
                'clipboard' { @('--clipboard-only', '--repeat', '100') }
                'ime' { @('--ime') }
                'dpi' { @('--dpi') }
            }
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
