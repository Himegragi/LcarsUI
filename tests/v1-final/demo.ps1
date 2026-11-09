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
$exe=Join-Path $PSScriptRoot '../../build/Release/lcars_demo_keyboard.exe'
& $exe --real --evidence $OutDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$PSScriptRoot/appdata-smoke.ps1" -OutDir $OutDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$log=Join-Path $OutDir 'CR05-events.log'
"$(Get-Date -Format o) CR05 starting" | Set-Content -Encoding UTF8 $log
& "$PSScriptRoot/real-close.ps1" -OutDir (Join-Path $OutDir 'CR05') *>&1 | Tee-Object -FilePath $log -Append
$code=$LASTEXITCODE
"$(Get-Date -Format o) CR05 exit $code" | Add-Content -Encoding UTF8 $log
exit $code
