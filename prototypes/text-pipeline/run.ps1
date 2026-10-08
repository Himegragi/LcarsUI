param([switch]$Interactive,[switch]$SelfDraw,[switch]$CrossProcess,[switch]$TraceState,[switch]$BuildOnly)
$ErrorActionPreference='Stop'
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsRoot){throw 'C++ Build Tools required'}
$devCmd=Join-Path $vsRoot 'Common7/Tools/VsDevCmd.bat'
$lines=& cmd.exe /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if($LASTEXITCODE){throw 'Developer environment failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$cmake=Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$buildDir=Join-Path $PSScriptRoot 'build/clang'
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja '-DCMAKE_C_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' '-DCMAKE_CXX_COMPILER=C:/Program Files/LLVM/bin/clang-cl.exe' -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){throw 'Configure failed'}
$target=if($CrossProcess){'editor_cross_host'}elseif($SelfDraw){'editor_probe'}else{'text_probe'}
& $cmake --build $buildDir --target $target --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
if($BuildOnly){return}
Push-Location $buildDir
try {
  $probeExe=".\$target.exe"
  $probeArgs=@()
  if($Interactive){$probeArgs+='--interactive'}
  if($CrossProcess){$probeArgs+='--cross-process'}
  if($TraceState){$probeArgs+='--trace-state'}
  & $probeExe @probeArgs
  if($LASTEXITCODE){throw 'Text probe failed'}
}finally{Pop-Location}
