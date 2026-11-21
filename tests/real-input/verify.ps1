[CmdletBinding()]
param(
    [string]$OutDir = "$PSScriptRoot/../../build/Release/evidence/idle/real-input",
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$cases = [ordered]@{
    RI01 = 'right-click the second line of the note and insert a marker'
    RI02 = 'commit wrapped Microsoft Bopomofo composition in the note'
    RI03 = 'drag the items page thumb in steady steps to the end'
    RI04 = 'open the dialog dropdown and verify the modal blocks page thumb dragging'
}
if ($DryRun) {
    foreach ($case in $cases.Keys) { Write-Host "real-input/${case}: $($cases[$case]) (web and material)" }
    exit 0
}

. "$PSScriptRoot/helpers.ps1"
$rows = [System.Collections.Generic.List[object]]::new()
$previousDpi = [IsoduoRealInput]::SetThreadDpiAwarenessContext([IntPtr](-4))
if ($previousDpi -eq [IntPtr]::Zero) { throw 'Per-monitor DPI awareness V2 unavailable' }
try {
    $bopomofo = Test-BopomofoTip
    foreach ($appearance in @('web', 'material')) {
        $exe = Join-Path $PSScriptRoot "../../build/Release/isoduo_settings_demo_$appearance.exe"
        $appearanceDir = (New-Item -ItemType Directory -Force -Path (Join-Path $OutDir $appearance)).FullName
        foreach ($case in $cases.Keys) {
            $process = $null
            $window = [IntPtr]::Zero
            $settings = Join-Path ([IO.Path]::GetTempPath()) ("isoduo-real-input-" + [guid]::NewGuid().ToString('N'))
            $status = 'FAIL'
            $evidence = ''
            try {
                $null = New-Item -ItemType Directory -Path $settings
                $process = Start-Process -FilePath $exe -ArgumentList @('--settings-dir', ('"' + $settings + '"')) -PassThru
                for ($i = 0; $i -lt 100 -and -not $process.HasExited; ++$i) {
                    $process.Refresh()
                    $window = $process.MainWindowHandle
                    if ($window -ne [IntPtr]::Zero) { break }
                    Start-Sleep -Milliseconds 100
                }
                if ($window -eq [IntPtr]::Zero) { throw 'Demo window did not appear' }
                $width = if ($case -in @('RI02', 'RI04')) { 660 } else { 0 }
                [IsoduoRealInput]::FitWindow($window, $width)
                [IsoduoRealInput]::Activate($window)
                if ($case -eq 'RI02' -and -not $bopomofo) {
                    $status = 'BLOCKED'
                    $evidence = 'Enable the Microsoft Bopomofo input method in the current Windows user profile and rerun.'
                } else {
                    $evidence = & (Join-Path $PSScriptRoot "$case.ps1")
                    $status = 'PASS'
                }
                Save-RealInputScreenshot 'result'
                $evidence += " | $appearance/$case-result.png"
            } catch {
                $status = 'FAIL'
                $evidence = $_.Exception.Message
                if ($window -ne [IntPtr]::Zero -and [IsoduoRealInput]::GetForegroundWindow() -eq $window) {
                    try {
                        Save-RealInputScreenshot 'failure'
                        $evidence += " | $appearance/$case-failure.png"
                    } catch { Write-Warning "Screenshot failed: $($_.Exception.Message)" }
                }
            } finally {
                if ($process) {
                    if (-not $process.HasExited) {
                        $process.Kill()
                        $process.WaitForExit()
                    }
                    $process.Dispose()
                }
                if (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings -Recurse -Force }
            }
            $evidence = $evidence -replace '[\t\r\n]+', ' '
            $rows.Add([pscustomobject][ordered]@{ case = $case; appearance = $appearance; status = $status; evidence = $evidence })
            Write-Host "real-input/$appearance/${case}: $status ($evidence)"
        }
    }
} finally {
    [IsoduoRealInput]::SetThreadDpiAwarenessContext($previousDpi) | Out-Null
    $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Delimiter "`t" -LiteralPath (Join-Path $OutDir 'real-input.tsv')
}
if (@($rows | Where-Object { $_.status -eq 'FAIL' }).Count) { exit 1 }
if (@($rows | Where-Object { $_.status -eq 'BLOCKED' }).Count) { exit 3 }
exit 0
