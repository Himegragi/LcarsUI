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
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h,int attribute,out RECT r,int size);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 public static void Key(ushort k,bool up=false) { var i=new INPUT{type=1,data=new U{key=new K{vk=k,flags=up?2u:0u}}}; if(SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput key failed"); }
 public static void Click(int x,int y,bool right=false) { SetCursorPos(x,y); uint down=right?8u:2u, up=right?16u:4u; foreach(uint f in new[]{down,up}) { var i=new INPUT{type=0,data=new U{mouse=new M{flags=f}}}; if(SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput mouse failed"); } }
}
"@
# Per-monitor DPI awareness (V2) so UIA rectangles, window rectangles and SetCursorPos all use physical
# pixels; otherwise a click on a monitor whose scale differs from the primary one lands elsewhere.
if ([CloseInput]::SetThreadDpiAwarenessContext([IntPtr](-4)) -eq [IntPtr]::Zero) { throw 'Per-monitor DPI awareness unavailable' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$rows = [System.Collections.Generic.List[object]]::new()
function Find-Id($root, $id) { $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty,$id))) }
function Wait-Element($root, $id) { for($i=0;$i -lt 50;$i++) { $item=Find-Id $root $id; if($item){return $item}; Start-Sleep -Milliseconds 100 }; throw "Element not found: $id" }
function Click-Element($element, [bool]$right=$false) { for($i=0;$i -lt 50;$i++){ $r=$element.Current.BoundingRectangle; if(!$r.IsEmpty -and $r.Width -gt 0 -and $r.Height -gt 0){ [CloseInput]::Click([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2),$right); return }; Start-Sleep -Milliseconds 100 }; throw 'Element has no bounds' }
foreach($appearance in @('web','material')) { foreach($method in @('x','alt-f4','taskbar')) {
 $dir=Join-Path $OutDir ("$appearance-$method-" + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force $dir | Out-Null
 $process=$null; $status='FAIL'; $detail=''
 try {
  $exe=Join-Path $PSScriptRoot "../../build/Release/lcars_settings_demo_$appearance.exe"
  $process=Start-Process -FilePath $exe -ArgumentList @('--settings-dir', ('"' + $dir + '"')) -PassThru
  for($i=0;$i -lt 100 -and $process.MainWindowHandle -eq 0;$i++){ Start-Sleep -Milliseconds 100; $process.Refresh() }
  if($process.MainWindowHandle -eq [IntPtr]::Zero){ throw 'Demo window did not appear' }
  [CloseInput]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
  $root=[System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
  [CloseInput]::Key(9); [CloseInput]::Key(9,$true); [CloseInput]::Key(65); [CloseInput]::Key(65,$true); [CloseInput]::Key(9); [CloseInput]::Key(9,$true)
  Start-Sleep -Milliseconds 300
  switch($method) {
   'x' {
     # The native caption buttons (minimize, maximize, close) as DWM reports them, relative to the window rectangle.
     $window=New-Object CloseInput+RECT; if(![CloseInput]::GetWindowRect($process.MainWindowHandle,[ref]$window)){throw 'Window rectangle unavailable'}
     $caption=New-Object CloseInput+RECT; $hr=[CloseInput]::DwmGetWindowAttribute($process.MainWindowHandle,5,[ref]$caption,16)
     if($hr -ne 0 -or $caption.Right -le $caption.Left -or $caption.Bottom -le $caption.Top){throw ('Native caption buttons unavailable (DWMWA_CAPTION_BUTTON_BOUNDS hr=0x{0:X8})' -f $hr)}
     # Close is the rightmost of the three caption buttons; click the middle of its sixth of the strip.
     [CloseInput]::Click([int]($window.Left + $caption.Right - ($caption.Right-$caption.Left)/6), [int]($window.Top + ($caption.Top+$caption.Bottom)/2))
   }
   'alt-f4' { $rect=New-Object CloseInput+RECT; if(![CloseInput]::GetWindowRect($process.MainWindowHandle,[ref]$rect)){throw 'Window rectangle unavailable'}; [CloseInput]::Click([int](($rect.Left+$rect.Right)/2),$rect.Top+10); [CloseInput]::Key(18); [CloseInput]::Key(115); [CloseInput]::Key(115,$true); [CloseInput]::Key(18,$true) }
   'taskbar' {
     $desktop=[System.Windows.Automation.AutomationElement]::RootElement
     $buttons=$desktop.FindAll([System.Windows.Automation.TreeScope]::Descendants,(New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Button)))
     $button=$null; $windowTitle=$root.Current.Name.TrimStart('*').Trim()
     # Windows 11 names a taskbar button after the application (here the executable, e.g.
     # "lcars_settings_demo_web.exe - 1 running window"); older shells use the window title.
     # With a taskbar on each monitor there is one button per taskbar: use the one nearest the window.
     $names=@($windowTitle, ($process.ProcessName + '.exe')) | Where-Object { $_ }
     $window=$root.Current.BoundingRectangle; $cx=$window.X+$window.Width/2; $cy=$window.Y+$window.Height/2; $best=[double]::MaxValue
     foreach($b in $buttons) {
       $label=$b.Current.Name.TrimStart('*').Trim()
       if($b.Current.ProcessId -eq $process.Id -or !$label) { continue }
       $match=$false; foreach($name in $names) { if($label -eq $name -or $label.StartsWith($name + ' - ') -or $label.StartsWith($name + ', ')) { $match=$true } }
       if(!$match) { continue }
       $r=$b.Current.BoundingRectangle; $distance=[math]::Pow($r.X+$r.Width/2-$cx,2)+[math]::Pow($r.Y+$r.Height/2-$cy,2)
       if($distance -lt $best) { $best=$distance; $button=$b }
     }
     if(!$button){throw "Taskbar button not found for '$($names -join "' or '")'"}; Click-Element $button $true
     # Windows 11 shows the taskbar context menu as a jump list window (ShellExperienceHost) whose
     # "Close window" entry is a list item with AutomationId 'Close'; older shells use a MenuItem.
     $close=$null
     for($i=0;$i -lt 30 -and !$close;$i++) {
       Start-Sleep -Milliseconds 100
       $foreground=[CloseInput]::GetForegroundWindow(); if($foreground -eq [IntPtr]::Zero) { continue }
       $menuRoot=[System.Windows.Automation.AutomationElement]::FromHandle($foreground)
       foreach($item in $menuRoot.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)) {
         $type=$item.Current.ControlType
         if(($type -eq [System.Windows.Automation.ControlType]::ListItem -and $item.Current.AutomationId -eq 'Close') -or
            ($type -eq [System.Windows.Automation.ControlType]::MenuItem -and $item.Current.Name -match '^(Close window|Close all windows|關閉視窗|關閉所有視窗)$')) { $close=$item; break }
       }
     }
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
