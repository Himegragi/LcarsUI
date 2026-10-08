param([switch]$Interactive)
$ErrorActionPreference = 'Stop'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools not found' }
$devCmd = Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat'
$lines = & cmd.exe /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE) { throw 'Developer environment failed' }
foreach($line in $lines) {
    if($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
$cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$buildDir = Join-Path $PSScriptRoot 'build/clang'
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' -DCMAKE_BUILD_TYPE=Debug
if($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build $buildDir
if($LASTEXITCODE) { throw 'Build failed' }
Push-Location $buildDir
try {
    if($Interactive) { & '.\state_probe.exe' --interactive }
    else { & '.\state_probe.exe' }
    if($LASTEXITCODE) { throw 'State walkthrough failed' }
} finally { Pop-Location }
