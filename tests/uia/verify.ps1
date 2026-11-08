[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('uia', 'axe', 'uia-ime', 'uia-dpi', 'uia-stress')][string]$Mode,
    [Parameter(Mandatory)][ValidateSet('web', 'material')][string]$Skin,
    [Parameter(Mandatory)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
New-Item -ItemType Directory -Force $OutDir | Out-Null
$rows = [System.Collections.Generic.List[object]]::new()
function Record($case, $expected, $actual, $status) {
    $rows.Add([pscustomobject]@{case=$case; expected=$expected; actual=$actual; status=$status})
}
$revision = (& git -C $root rev-parse HEAD 2>$null)
@("commit=$revision", 'build=Release', "appearance=$Skin", "environment=$(if ($env:GITHUB_ACTIONS) { 'GitHub' } else { 'author machine' })") | Set-Content -Encoding UTF8 (Join-Path $OutDir 'metadata.txt')
if ($Mode -in @('uia', 'uia-stress')) {
    $client = Join-Path $root 'build/Release/lcars_uia_client.exe'
    $hostExe = Join-Path $root 'build/Release/lcars_uia_host.exe'
    if (-not (Test-Path $client) -or -not (Test-Path $hostExe)) { Record 'UIA' 'Client and host available' 'Missing executable' 'BLOCKED' }
    else {
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $output = @(& $client $hostExe $Skin 2>&1 | ForEach-Object { "$_" })
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previous }
        $output | Set-Content -Encoding UTF8 (Join-Path $OutDir 'client.log')
        foreach ($line in $output) {
            if ($line -match '^(UIA\d+[^:]*): (PASS|FAIL)') { Record $matches[1] 'PASS' $line $matches[2] }
        }
        if ($code -ne 0 -or -not $rows.Count) { Record 'UIA driver' 'exit 0 and case results' "exit $code" 'FAIL' }
        # UIA18 requires real host renderer failure injection and rebuild; the
        # existing UIA15 status fixture does not exercise that lifecycle.
        Record 'UIA18' 'Renderer failure, rebuild and unhandled shutdown' 'Real renderer failure driver not yet available' 'BLOCKED'
    }
} else {
    Record $Mode 'Complete platform verification' 'Driver not yet available' 'BLOCKED'
}
$rows | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "$Mode.tsv")
if (@($rows | Where-Object { $_.status -notin @('PASS', 'N/A') }).Count) { exit 1 }
exit 0
