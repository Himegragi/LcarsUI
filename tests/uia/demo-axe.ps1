[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('web','material')][string]$Appearance,
    [Parameter(Mandatory)][string]$OutDir,
    [Parameter(Mandatory)][string]$AxeExe,
    [Parameter(Mandatory)][string]$HostExe
)
 . (Join-Path $PSScriptRoot 'axe-common.ps1')
$states = @('general','items','appearance','about','items-dialog','delete-dialog','close-dialog','font-open')
$prepare = { param($window, $state, $index)
    $result = [LcarsAxeWindow]::SendMessage($window, 0x8053, [IntPtr]$index, [IntPtr]::Zero)
    if ($result.ToInt64() -ne 0) { throw "Demo setup failed: $state => $result" }
}
$verify = { param($window, $index) [LcarsAxeWindow]::SendMessage($window, 0x8054, [IntPtr]$index, [IntPtr]::Zero).ToInt64() -eq 1 }
if (Invoke-LcarsAxeScan $Appearance $OutDir $AxeExe $HostExe $states 'AX01' $prepare $verify) { exit 0 } else { exit 1 }
