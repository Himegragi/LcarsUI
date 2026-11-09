[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutDir)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/settings-sandbox.ps1"
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$directory=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'LcarsUI/SettingsDemo'
$rows=[Collections.Generic.List[object]]::new()
function Find-Id($root,$id) { $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,$id))) }
function Wait-Id($root,$id) { for($i=0;$i -lt 100;$i++){ $v=Find-Id $root $id; if($v){return $v}; Start-Sleep -Milliseconds 100 }; throw "Element not found: $id" }
function Start-Demo($appearance) {
 $p=Start-Process -FilePath (Join-Path $PSScriptRoot "../../build/Release/lcars_settings_demo_$appearance.exe") -PassThru
 for($i=0;$i -lt 100 -and !$p.MainWindowHandle;$i++){Start-Sleep -Milliseconds 100;$p.Refresh()}
 if(!$p.MainWindowHandle){ if(!$p.HasExited){Stop-Process -Id $p.Id -Force}; throw 'Demo window unavailable' }
 return $p
}
Use-SettingsSandbox $directory {
 foreach($appearance in @('web','material')) {
  $p=$null; $status='FAIL'; $detail=''
  try {
   $p=Start-Demo $appearance; $root=[System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
   $field=Wait-Id $root 'general.name'; $field.SetFocus(); $value=$field.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern); $value.SetValue("APPDATA $appearance")
   (Wait-Id $root 'app.nav').SetFocus()
   $save=Wait-Id $root 'app.save'; $save.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
   for($i=0;$i -lt 50 -and !(Test-Path (Join-Path $directory 'settings.json'));$i++){Start-Sleep -Milliseconds 100}
   if(!(Test-Path (Join-Path $directory 'settings.json'))){throw 'settings.json was not saved'}
   $root.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close(); if(!$p.WaitForExit(10000)){throw 'Demo did not close'}; if($p.ExitCode -ne 0){throw 'Demo failed'}
   $p=Start-Demo $appearance; $root=[System.Windows.Automation.AutomationElement]::FromHandle($p.MainWindowHandle)
   $field=Wait-Id $root 'general.name'; $actual=$field.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).Current.Value
   if($actual -ne "APPDATA $appearance"){throw "Reloaded value mismatch: $actual"}
   $root.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close(); if(!$p.WaitForExit(10000)){throw 'Relaunched demo did not close'}
   $status='PASS'; $detail='Default path saved and reloaded name'
  } catch { $detail=$_.Exception.Message } finally { if($p -and !$p.HasExited){Stop-Process -Id $p.Id -Force} }
  $rows.Add([pscustomobject]@{case='APPDATA';appearance=$appearance;status=$status;detail=$detail})
  $rows | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir 'APPDATA.tsv')
 }
}
$rows | Format-Table | Out-String | Set-Content -Encoding UTF8 (Join-Path $OutDir 'APPDATA-events.log')
if(@($rows | Where-Object status -ne 'PASS').Count){exit 1}
