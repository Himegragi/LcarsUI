param([switch]$BuildOnly,[switch]$Scenarios,[switch]$Interactive,[switch]$ResumeScenarios,[switch]$ResumeInteractive)
# THROWAWAY safe-handoff logic prototype. Standalone build in prototypes/safe-handoff/build.
$ErrorActionPreference='Stop'
if((@($BuildOnly,$Scenarios,$Interactive,$ResumeScenarios,$ResumeInteractive)|Where-Object{$_}).Count -ne 1){throw 'Use exactly one of -BuildOnly, -Scenarios, -Interactive, -ResumeScenarios, -ResumeInteractive'}
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsRoot){throw 'C++ Build Tools required'}
$lines=& cmd.exe /d /c "call `"$(Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat')`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if($LASTEXITCODE){throw 'Developer environment failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$buildDir=Join-Path $PSScriptRoot 'build'
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build $buildDir --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
if($BuildOnly){return}
$exe=Join-Path $buildDir 'safe_handoff.exe'
$out=Join-Path $PSScriptRoot 'evidence'
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try{
  if($Scenarios){& $exe --scenarios}elseif($ResumeScenarios){& $exe --resume-scenarios}elseif($ResumeInteractive){& $exe --resume-interactive}else{& $exe --interactive}
  if($LASTEXITCODE){throw "safe_handoff failed (exit $LASTEXITCODE)"}
}finally{Pop-Location}
