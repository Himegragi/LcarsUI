param([switch]$Asan)
# THROWAWAY report-handle prototype. Builds into build/ (or build-asan/) and runs the scenarios.
$ErrorActionPreference='Stop'
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsRoot){throw 'C++ Build Tools required'}
$env:PATH="C:/Program Files (x86)/Microsoft Visual Studio/Installer;$env:PATH"
$lines=& cmd.exe /d /c "call `"$(Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat')`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if($LASTEXITCODE){throw 'Developer environment failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$buildDir=Join-Path $PSScriptRoot ($(if($Asan){'build-asan'}else{'build'}))
$type=$(if($Asan){'RelWithDebInfo'}else{'Release'})
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' "-DCMAKE_BUILD_TYPE=$type" "-DRH_ASAN=$(if($Asan){'ON'}else{'OFF'})"
if($LASTEXITCODE){throw 'Configure failed'}
& $cmake --build $buildDir
if($LASTEXITCODE){throw 'Build failed'}
if($Asan){$env:PATH="C:/Program Files/LLVM/lib/clang/$((Get-ChildItem 'C:/Program Files/LLVM/lib/clang')[0].Name)/lib/windows;$env:PATH"}
& (Join-Path $buildDir 'report_handles.exe')
if($LASTEXITCODE){throw "report_handles failed (exit $LASTEXITCODE)"}
