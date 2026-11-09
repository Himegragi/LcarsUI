[CmdletBinding()]
param([string]$OutDir, [switch]$DryRun)
$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot ('build/Release/evidence/v1-final-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$plan = @(
    'clipboard, IME, DPI (user configures Microsoft Bopomofo and two displays at 100% and 150%)',
    'Release and Debug host_native CTest',
    'perf (PM01-PM05 showcase; demo PF01-PF06 pending: no demo performance driver)',
    'presentation (PR01-PR04 automated parts)',
    'uia, uia-ime, uia-dpi, uia-stress and demo-uia',
    'real close controls via SendInput: pending (driver not implemented)',
    'render / GC01 real GPU showcase comparison; VC01 demo matrix pending',
    'RenderDoc DG05/RD01: user capture and inspect groups; N/A if not installed',
    'TDR RF06/TD01: user runs dxcap -forcetdr, retries, saves diagnostics and screenshots',
    'OutputDebugStringW DG03: user observes Debug output in a debugger',
    'hands-on trial: user completes both appearances'
)
Write-Output 'Estimated duration: 2-4 hours plus user setup, TDR, RenderDoc and hands-on trial; Debug UIA and desktop contention may take longer.'
$plan | ForEach-Object { Write-Output " - $_" }
if ($DryRun) { exit 0 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
function Record([string]$Name, [string]$Status, [string]$Detail) {
    $results.Add([pscustomobject]@{ step=$Name; status=$Status; detail=$Detail })
    $results | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 -LiteralPath (Join-Path $OutDir 'steps.tsv')
    Write-Host "${Name}: $Status - $Detail"
}
function Run-Step([string]$Name, [scriptblock]$Action) {
    try {
        $global:LASTEXITCODE = 0
        & $Action *>&1 | Out-File -Encoding UTF8 -LiteralPath (Join-Path $OutDir "$Name.log")
        if ($LASTEXITCODE -ne 0) { throw "exit $LASTEXITCODE" }
        Record $Name 'PASS' "$Name.log"
    } catch { Record $Name 'FAIL' "$($_.Exception.Message); $Name.log" }
}
function Pause-User([string]$Prompt) { Write-Host $Prompt; Read-Host 'Press Enter when ready (record results separately)' | Out-Null }
Pause-User 'Configure Microsoft Bopomofo and two monitors at 100% and 150%; keep this desktop idle during tests.'
. "$PSScriptRoot/cmake/VisualStudio.ps1"
$vs = Get-LcarsVisualStudio
$ctest = Join-Path $vs.InstallationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { $ctest = (Get-Command ctest.exe).Source }
foreach ($name in @('clipboard','ime','dpi')) {
    Run-Step $name { & powershell -NoProfile -File "$PSScriptRoot/verify-idle.ps1" -Only $name -OutDir (Join-Path $OutDir $name) }
}
foreach ($configuration in @('Release','Debug')) {
    Run-Step "host_native-$configuration" { & powershell -NoProfile -File "$PSScriptRoot/build.ps1" -Configuration $configuration -SkipTests; if ($LASTEXITCODE -ne 0) { throw 'build failed' }; & $ctest --test-dir "$PSScriptRoot/build/$configuration" -R '^host_native$' --output-on-failure --timeout 900 }
}
foreach ($name in @('perf','presentation','uia','uia-ime','uia-dpi','uia-stress','demo-uia')) {
    Run-Step $name { & powershell -NoProfile -File "$PSScriptRoot/verify-idle.ps1" -Only $name -OutDir (Join-Path $OutDir $name) }
}
Run-Step 'render-real-gpu' {
    $renderDir = Join-Path $OutDir 'render-real-gpu'
    & "$PSScriptRoot/build/Release/lcars_host_verify.exe" --render-compare --evidence $renderDir
    if ($LASTEXITCODE -ne 0) { throw "comparison exited $LASTEXITCODE" }
    $rows = @(Import-Csv -Delimiter "`t" -LiteralPath (Join-Path $renderDir 'render-compare.tsv'))
    if ($rows.Count -ne 40 -or @($rows | Where-Object { $_.status -ne 'PASS' -or $_.renderer -match 'llvmpipe|softpipe|GDI Generic' }).Count) {
        throw 'Expected 40 passing showcase rows on a real GPU.'
    }
}
foreach ($name in @('demo-keyboard','demo-perf','real-close','VC01')) { Record $name 'PENDING' 'Driver or orchestration is not yet implemented.' }
$renderdoc = Get-Command renderdoccmd.exe -ErrorAction SilentlyContinue
$renderdocPath = if ($renderdoc) { $renderdoc.Source } else { @("${env:ProgramFiles}/RenderDoc/renderdoccmd.exe", "${env:ProgramFiles(x86)}/RenderDoc/renderdoccmd.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1 }
if ($renderdocPath) { Record 'RenderDoc' 'PENDING' "Found $renderdocPath; capture Debug showcase and demo frames and inspect component ID groups including app.nav and general.name." }
else { Record 'RenderDoc' 'N/A' 'RenderDoc not found on PATH or in standard install locations; install it, capture Debug showcase and demo frames, and inspect component ID groups.' }
Pause-User 'TDR: for the showcase and demo, save a before screenshot, run dxcap -forcetdr with required administrator rights, choose Retry, and save diagnostics and an after screenshot. Do not report PASS without evidence.'
Record 'TDR' 'PENDING' 'Record RF06/TD01 outcome and evidence paths in the completion report.'
Pause-User 'Inspect Debug OutputDebugStringW in a debugger and record the result. Then complete the hands-on trial for both appearances.'
Record 'OutputDebugStringW' 'PENDING' 'Record debugger output and evidence path.'
Record 'hands-on' 'PENDING' 'Complete both appearances using the trial template.'
Write-Host "Evidence folder: $OutDir"
if (@($results | Where-Object { $_.status -ne 'PASS' }).Count) { exit 1 }

