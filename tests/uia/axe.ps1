[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('web','material')][string]$Appearance,
    [Parameter(Mandatory)][string]$OutDir,
    [Parameter(Mandatory)][string]$AxeExe,
    [Parameter(Mandatory)][string]$HostExe
)
$ErrorActionPreference = 'Stop'
if (-not ('LcarsAxeWindow' -as [type])) { Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class LcarsAxeWindow {
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
}
"@
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force $OutDir | Out-Null
'No Axe rules were excluded.' | Set-Content -Encoding UTF8 (Join-Path $OutDir 'exclusions.txt')
$rows = [System.Collections.Generic.List[object]]::new()
foreach ($state in @('v01','region','v02','v03','dialog')) {
    $folder = Join-Path $OutDir $state
    New-Item -ItemType Directory -Force $folder | Out-Null
    $proc = $null; $status = 'BLOCKED'; $actual = ''
    $artifact = Join-Path $folder "$state.a11ytest"
    if (Test-Path -LiteralPath $artifact) { Remove-Item -LiteralPath $artifact }
    try {
        $proc = Start-Process -FilePath $HostExe -ArgumentList $Appearance -PassThru -RedirectStandardOutput (Join-Path $folder 'host-stdout.log') -RedirectStandardError (Join-Path $folder 'host-stderr.log')
        for ($i = 0; $i -lt 100 -and $proc.MainWindowHandle -eq [IntPtr]::Zero -and -not $proc.HasExited; $i++) { Start-Sleep -Milliseconds 100; $proc.Refresh() }
        if ($proc.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Host window unavailable' }
        # Wait until the UIA host has completed initial rendering and tree publication.
        Start-Sleep -Seconds 2
        $actions = switch ($state) { 'region' { @(34,29) } 'v02' { @(6) } 'v03' { @(2) } 'dialog' { @(2,7) } default { @() } }
        foreach ($action in $actions) {
            $result = [LcarsAxeWindow]::SendMessage($proc.MainWindowHandle, 0x8050, [IntPtr]$action, [IntPtr]::Zero)
            if ($result.ToInt64() -ne 0) { throw "State setup failed: $action => $result" }
            if ($action -eq 34) { Start-Sleep -Milliseconds 500 }
        }
        $stateIndex = [array]::IndexOf(@('v01','region','v02','v03','dialog'), $state)
        $verified = [LcarsAxeWindow]::SendMessage($proc.MainWindowHandle, 0x8050, [IntPtr]32, [IntPtr]$stateIndex)
        if ($verified.ToInt64() -ne 1) { throw "Showcase state not observed: $state" }
        $previous = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $output = @(& $AxeExe --processid "$($proc.Id)" --outputdirectory $folder --scanid $state --alwayssavetestfile --verbosity verbose 2>&1 | ForEach-Object { "$_" })
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previous }
        $output | Set-Content -Encoding UTF8 (Join-Path $folder 'axe.log')
        $summary = @($output | Where-Object { $_ -match '^\d+ errors? (were|was) found' })
        $artifact = Join-Path $folder "$state.a11ytest"
        $stillInState = [LcarsAxeWindow]::SendMessage($proc.MainWindowHandle, 0x8050, [IntPtr]32, [IntPtr]$stateIndex)
        if ($stillInState.ToInt64() -ne 1) { throw "Showcase state changed during scan: $state" }
        if ($code -eq 0 -and $summary.Count -eq 1 -and $summary[0] -match '^0 errors? (were|was) found' -and (Test-Path $artifact)) { $status='PASS' }
        elseif ($summary.Count -eq 1) { $status='FAIL' }
        $actual = "exit=$code; $($summary -join '; ')"
    } catch { $actual = $_.Exception.Message }
    finally {
        if ($proc -and -not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null; if (-not $proc.WaitForExit(5000)) { $proc.Kill(); $proc.WaitForExit() } }
    }
    $rows.Add([pscustomobject]@{case="AXE01-$state"; expected='0 errors; no excluded rules'; actual=$actual; status=$status})
}
$rows | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir 'axe.tsv')
if (@($rows | Where-Object { $_.status -ne 'PASS' }).Count) { exit 1 }
exit 0
