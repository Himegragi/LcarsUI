param([switch]$BuildOnly,[switch]$Benchmark,[switch]$Interactive)
# THROWAWAY skin-switch prototype. Builds the opt-in target inside the text-pipeline build tree to reuse cached
# HarfBuzz/FreeType; produces only skin_switch.exe (no editor binaries are rebuilt or overwritten).
$ErrorActionPreference='Stop'
if((@($BuildOnly,$Benchmark,$Interactive)|Where-Object{$_}).Count -ne 1){throw 'Use exactly one of -BuildOnly, -Benchmark, -Interactive'}
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsRoot){throw 'C++ Build Tools required'}
$lines=& cmd.exe /d /c "call `"$(Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat')`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if($LASTEXITCODE){throw 'Developer environment failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$source=Join-Path $PSScriptRoot '../text-pipeline'
$buildDir=Join-Path $source 'build/clang'
& $cmake -S $source -B $buildDir -G Ninja '-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' -DCMAKE_BUILD_TYPE=Release -DLCARS_SKIN_SWITCH_PROTOTYPE=ON
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build $buildDir --target skin_switch --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
if($BuildOnly){return}
$exe=Join-Path $buildDir 'skin_switch.exe'
$out=Join-Path $PSScriptRoot 'evidence'
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try{
  if($Benchmark){& $exe --benchmark}else{& $exe --interactive}
  if($LASTEXITCODE){throw "skin_switch failed (exit $LASTEXITCODE)"}
}finally{Pop-Location}
