[CmdletBinding()]
param([switch]$List, [string]$OutDir = "$PSScriptRoot/../../build/Release/evidence/real-close")
$ErrorActionPreference = 'Stop'
if ($List) { 'SendInput: title-bar X, Alt+F4, taskbar context menu Close; web and material; isolated settings directory'; exit 0 }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class CloseInput {
 [StructLayout(LayoutKind.Sequential)] public struct INPUT { public int type; public U data; }
 [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public M mouse; [FieldOffset(0)] public K key; }
 [StructLayout(LayoutKind.Sequential)] public struct M { public int dx,dy; public uint data,flags,time; public UIntPtr extra; }
 [StructLayout(LayoutKind.Sequential)] public struct K { public ushort vk,scan; public uint flags,time; public UIntPtr extra; }
 [DllImport("user32.dll",SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] input, int size);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 public static void Key(ushort k,bool up=false) { var i=new INPUT{type=1,data=new U{key=new K{vk=k,flags=up?2u:0u}}}; if(SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput key failed"); }
 public static void Click(int x,int y,bool right=false) { SetCursorPos(x,y); uint down=right?8u:2u, up=right?16u:4u; foreach(uint f in new[]{down,up}) { var i=new INPUT{type=0,data=new U{mouse=new M{flags=f}}}; if(SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput mouse failed"); } }
}
"@
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$rows = [System.Collections.Generic.List[object]]::new()
function Find-Id($root, $id) { $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,$id))) }
function Wait-Element($root, $id) { for($i=0;$i -lt 50;$i++) { $item=Find-Id $root $id; if($item){return $item}; Start-Sleep -Milliseconds 100 }; throw "Element not found: $id" }
function Click-Element($element, [bool]$right=$false) { $r=$element.Current.BoundingRectangle; if($r.IsEmpty){throw 'Element has no bounds'}; [CloseInput]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right) }
foreach($appearance in @('web','material')) { foreach($method in @('x','alt-f4','taskbar')) {
 $dir=Join-Path $OutDir ("$appearance-$method-" + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force $dir | Out-Null
 $process=$null; $status='FAIL'; $detail=''
 try {
  $exe=Join-Path $PSScriptRoot "../../build/Release/lcars_settings_demo_$appearance.exe"
  $process=Start-Process -FilePath $exe -ArgumentList @('--settings-dir', ('"' + $dir + '"')) -PassThru
  for($i=0;$i -lt 100 -and $process.MainWindowHandle -eq 0;$i++){ Start-Sleep -Milliseconds 100; $process.Refresh() }
  if(!$process.MainWindowHandle){ throw 'Demo window did not appear' }
  [CloseInput]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
  $root=[System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
  [CloseInput]::Key(9); [CloseInput]::Key(9,$true); [CloseInput]::Key(65); [CloseInput]::Key(65,$true); [CloseInput]::Key(9); [CloseInput]::Key(9,$true)
  Start-Sleep -Milliseconds 300
  switch($method) {
   'x' { $button=$root.FindFirst([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,'Close'))); if(!$button){throw 'Native title-bar close button not found'}; Click-Element $button }
   'alt-f4' { [CloseInput]::Key(18); [CloseInput]::Key(115); [CloseInput]::Key(115,$true); [CloseInput]::Key(18,$true) }
   'taskbar' {
     $desktop=[System.Windows.Automation.AutomationElement]::RootElement
     $buttons=$desktop.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Button)))
     $button=$null; foreach($b in $buttons) { if($b.Current.Name -eq $root.Current.Name -and $b.Current.ProcessId -ne $process.Id){$button=$b;break} }
     if(!$button){throw 'Taskbar button not found; ungroup or pin the demo and retry'}; Click-Element $button $true; Start-Sleep -Milliseconds 500
     $menu=$desktop.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::MenuItem)))
     $close=$null; foreach($item in $menu) { if($item.Current.Name -match '^(Close window|Close all windows|關閉視窗|關閉所有視窗)$'){$close=$item;break} }
     if(!$close){throw 'Taskbar Close menu item not found'}; Click-Element $close
   }
  }
  $dialog=Wait-Element $root 'app.close_dialog'; if($dialog.Current.IsOffscreen){throw 'Close dialog is offscreen'}
  $cancel=Wait-Element $root 'app.close_dialog.cancel'; Click-Element $cancel; Start-Sleep -Milliseconds 300
  if($process.HasExited){throw 'Cancel unexpectedly closed process'}
  $status='PASS'; $detail='Unsaved-change prompt appeared; cancel kept process open.'
 } catch { $detail=$_.Exception.Message } finally { if($process -and !$process.HasExited){Stop-Process -Id $process.Id -Force} }
 $rows.Add([pscustomobject]@{appearance=$appearance;method=$method;status=$status;detail=$detail})
 $rows | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir 'real-close.tsv')
} }
if(@($rows | Where-Object status -ne 'PASS').Count){exit 1}
