[CmdletBinding()]
param([switch]$DryRun,[string]$OutDir)
$ErrorActionPreference='Stop'
if ($DryRun) {
 'KW02: SendInput keyboard walkthrough, Microsoft Bopomofo, web and material'
 'CR09-real: close during real Microsoft Bopomofo composition'
 'APPDATA: backup, save and relaunch real demo executables, restore settings folder'
 'CR05: tests/v1-final/real-close.ps1 (six real close entry points)'
 exit 0
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# Run every part even after a failure, so one idle session reports all of them.
$failed=@()
$exe=Join-Path $PSScriptRoot '../../build/Release/lcars_demo_keyboard.exe'
$ErrorActionPreference='Continue'
& $exe --real --evidence $OutDir
if ($LASTEXITCODE -ne 0) { $failed+='KW02/CR09-real' }
try { & "$PSScriptRoot/appdata-smoke.ps1" -OutDir $OutDir; if ($LASTEXITCODE -ne 0) { $failed+='APPDATA' } } catch { Write-Warning $_; $failed+='APPDATA' }
$log=Join-Path $OutDir 'CR05-events.log'
"$(Get-Date -Format o) CR05 starting" | Set-Content -Encoding UTF8 $log
& "$PSScriptRoot/real-close.ps1" -OutDir (Join-Path $OutDir 'CR05') *>&1 | Tee-Object -FilePath $log -Append
$code=$LASTEXITCODE
"$(Get-Date -Format o) CR05 exit $code" | Add-Content -Encoding UTF8 $log
if ($code -ne 0) { $failed+='CR05' }
if ($failed.Count) { Write-Host "demo failed: $($failed -join ', ')"; exit 1 }
Write-Host 'demo: PASS'
exit 0
