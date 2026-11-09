[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('web','material')][string]$Appearance,
    [Parameter(Mandatory)][string]$OutDir,
    [Parameter(Mandatory)][string]$AxeExe,
    [Parameter(Mandatory)][string]$HostExe
)
 . (Join-Path $PSScriptRoot 'axe-common.ps1')
$states = @('v01','region','v02','v03','dialog')
$prepare = { param($window, $state, $index)
    $actions = switch ($state) { 'region' { @(34,29) } 'v02' { @(6) } 'v03' { @(2) } 'dialog' { @(2,7) } default { @() } }
    foreach ($action in $actions) {
        $result = [LcarsAxeWindow]::SendMessage($window, 0x8050, [IntPtr]$action, [IntPtr]::Zero)
        if ($result.ToInt64() -ne 0) { throw "State setup failed: $action => $result" }
        if ($action -eq 34) { Start-Sleep -Milliseconds 500 }
    }
}
$verify = { param($window, $index) [LcarsAxeWindow]::SendMessage($window, 0x8050, [IntPtr]32, [IntPtr]$index).ToInt64() -eq 1 }
if (Invoke-LcarsAxeScan $Appearance $OutDir $AxeExe $HostExe $states 'AXE01' $prepare $verify) { exit 0 } else { exit 1 }
