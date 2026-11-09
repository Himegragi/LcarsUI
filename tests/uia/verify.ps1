[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('uia', 'axe', 'uia-ime', 'uia-dpi', 'uia-stress', 'demo-uia')][string]$Mode,
    [Parameter(Mandatory)][ValidateSet('web', 'material')][string]$Skin,
    [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force $OutDir | Out-Null
$rows = [System.Collections.Generic.List[object]]::new()
function Record($case, $expected, $actual, $status) {
    $rows.Add([pscustomobject]@{case=$case; expected=$expected; actual=$actual; status=$status})
}
$revision = (& git -C $root rev-parse HEAD 2>$null)
$dirty = @(& git -C $root status --porcelain 2>$null).Count -gt 0
@("commit=$revision", "dirty=$dirty", 'build=Release', "appearance=$Skin", "environment=$(if ($env:GITHUB_ACTIONS) { 'GitHub' } else { 'author machine' })") | Set-Content -Encoding UTF8 (Join-Path $OutDir 'metadata.txt')
$client = Join-Path $root 'build/Release/lcars_uia_client.exe'
$hostExe = Join-Path $root 'build/Release/lcars_uia_host.exe'
try {
    if ($Mode -in @('axe', 'demo-uia')) {
        $axeExe = $env:LCARS_AXE_EXE
        if (-not $axeExe) {
            $toolDir = Join-Path $OutDir ('tool-' + [guid]::NewGuid().ToString('N'))
            & (Join-Path $root '.github/scripts/install-axe.ps1') -Destination $toolDir
            $axeExe = Join-Path $toolDir 'unpacked/AxeWindowsCLI.exe'
        }
        if (-not (Test-Path -LiteralPath $axeExe) -or -not (Test-Path -LiteralPath $hostExe)) { throw 'Axe executable or UIA host missing' }
        $archive = Join-Path (Split-Path -Parent (Split-Path -Parent $axeExe)) 'axe.zip'
        if (-not (Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'AECA43F41C89B3FFB1DB84011539E609ECD7CB3BADD6E78FADA2ADA327D10A64' -or (Get-FileHash -LiteralPath $axeExe -Algorithm SHA256).Hash -ne '46DC0AB9282B89ACCF8E03F7C445877693FA257F7B716321F79AD5AF9D3F997C') { throw 'Axe 2.4.2 archive or executable SHA256 mismatch' }
        if ($Mode -eq 'demo-uia') {
            $demoHost = Join-Path $root 'build/Release/lcars_uia_demo_host.exe'
            $demoClient = Join-Path $root 'build/Release/lcars_uia_demo_client.exe'
            $demoOutput = @(& $demoClient $demoHost $Skin 2>&1 | ForEach-Object { "$_" })
            $demoExit = $LASTEXITCODE
            $demoOutput | Set-Content -Encoding UTF8 (Join-Path $OutDir 'client.log')
            if ($demoExit -ne 0) { throw 'Demo UIA cases failed' }
            foreach ($line in $demoOutput) {
                if ($line -match '^(UA\d+[^:]*): (PASS|FAIL)$') { Record $matches[1] 'PASS' $line $matches[2] }
            }
            foreach ($id in @('UA01','UA02','UA03','UA04 item dialog','UA04 close dialog','UA05','UA06','UA07')) {
                if (-not @($rows | Where-Object { $_.case -eq $id -and $_.status -eq 'PASS' }).Count) { throw "Missing demo case $id" }
            }
            & (Join-Path $PSScriptRoot 'demo-axe.ps1') -Skin $Skin -OutDir $OutDir -AxeExe $axeExe -HostExe $demoHost
            if ($LASTEXITCODE -ne 0) { exit 1 }
            foreach ($scan in @(Import-Csv -Delimiter "`t" (Join-Path $OutDir 'axe.tsv'))) { $rows.Add($scan) }
            $rows | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir 'demo-uia.tsv')
            exit 0
        }
        & (Join-Path $PSScriptRoot 'axe.ps1') -Skin $Skin -OutDir $OutDir -AxeExe $axeExe -HostExe $hostExe
        if ($LASTEXITCODE -ne 0) { exit 1 }
        exit 0
    }
    if ($Mode -eq 'uia-ime') {
        $verifier = Join-Path $root 'build/Release/lcars_host_verify.exe'
        if (-not (Test-Path -LiteralPath $verifier) -or -not (Test-Path -LiteralPath $client)) { throw 'IME verifier or UIA client missing' }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $output = @(& $verifier --uia-ime --perf-skin $Skin --evidence $OutDir 2>&1 | ForEach-Object { "$_" })
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previous }
        $output | Set-Content -Encoding UTF8 (Join-Path $OutDir 'client.log')
        $raw = Join-Path $OutDir 'uia-ime.tsv'
        if (-not (Test-Path -LiteralPath $raw)) { throw "IME verifier produced no TSV (exit $code)" }
        $observed = @(Import-Csv -Delimiter "`t" -LiteralPath $raw)
        foreach ($item in $observed) {
            $status = if ($item.case -match 'BLOCKED') { 'BLOCKED' } else { $item.status }
            Record $item.case 'SetValue rejected; committed Value and Bopomofo preedit unchanged' $item.evidence $status
        }
        if ($code -ne 0 -and -not @($rows | Where-Object { $_.status -ne 'PASS' }).Count) { Record 'UIA21' 'exit 0' "exit $code" 'FAIL' }
    } else {
        if (-not (Test-Path -LiteralPath $client) -or -not (Test-Path -LiteralPath $hostExe)) { throw 'UIA client or host missing' }
        $argsForClient = @($hostExe, $Skin)
        if ($Mode -eq 'uia-dpi') { $argsForClient += '--uia-dpi' }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $output = @(& $client @argsForClient 2>&1 | ForEach-Object { "$_" })
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previous }
        $output | Set-Content -Encoding UTF8 (Join-Path $OutDir 'client.log')
        if ($Mode -eq 'uia-dpi') {
            $state = if ($output -match 'BLOCKED') { 'BLOCKED' } elseif ($code -eq 0 -and $output -match 'UIA17-DPI physical transitions: PASS') { 'PASS' } else { 'FAIL' }
            Record 'UIA17-DPI' '144 to 120 DPI and back; geometry and layout event' ($output -join '; ') $state
        } else {
            foreach ($line in $output) {
                if ($line -match '^(UIA\d+[^:]*): (PASS|FAIL)') { Record $matches[1] 'PASS' $line $matches[2] }
                if ($line -match 'UIA17 size=(\d)') { Record 'UIA17 font size' '1' $matches[1] $(if ($matches[1] -eq '1') { 'PASS' } else { 'FAIL' }) }
            }
            foreach ($id in @('UIA01','UIA02','UIA03','UIA04','UIA05','UIA06','UIA07','UIA08','UIA09','UIA10','UIA11','UIA12','UIA13','UIA14','UIA15','UIA17','UIA18','UIA19')) {
                if (-not @($rows | Where-Object { $_.case -like "$id*" }).Count) { Record $id 'Case executed and passed' 'No case result' 'BLOCKED' }
            }
            if ($code -ne 0) { Record 'UIA driver' 'exit 0' "exit $code" 'FAIL' }
        }
    }
} catch { Record $Mode 'Verification available' $_.Exception.Message 'BLOCKED' }
if (-not $rows.Count) { Record $Mode 'At least one case result' 'No results' 'BLOCKED' }
$rows | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "$Mode.tsv")
if (@($rows | Where-Object { $_.status -notin @('PASS', 'N/A') }).Count) { exit 1 }
exit 0
