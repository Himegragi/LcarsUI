param([switch]$Interactive,[switch]$SelfDraw,[switch]$CrossProcess,[switch]$TraceState,[switch]$BuildOnly,[switch]$Concurrency,[switch]$Lifecycle,[switch]$ProbeUiaRoot,[switch]$DiagnoseUiaFailure,[switch]$TestUiaFailureDiagnostics,[switch]$ManualNarrator)
# -ManualNarrator: manual accessibility entry. Builds editor_cross_host and runs it with
# --interactive --manual-narrator only (no --cross-process): no scripted input, fixtures or self-checks.
if($ManualNarrator -and ($ProbeUiaRoot -or $Interactive -or $SelfDraw -or $BuildOnly -or $CrossProcess -or $Concurrency -or $Lifecycle -or $TraceState -or $DiagnoseUiaFailure -or $TestUiaFailureDiagnostics)){throw '-ManualNarrator cannot be combined with test options'}
# -TestUiaFailureDiagnostics: diagnostic self-test only, implies -CrossProcess. Injects a labelled synthetic failure
# after a successful roundtrip to exercise the child path; the run is expected to fail (nonzero).
if($TestUiaFailureDiagnostics){$CrossProcess=$true}
# -DiagnoseUiaFailure: diagnostic only, implies -CrossProcess; launches read-only probe children only after the
# host's original in-process UIA roundtrip has failed. The run still fails in that case.
if($DiagnoseUiaFailure){$CrossProcess=$true}
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
# -ProbeUiaRoot: diagnostic only. Builds the external client and runs one cold-process GetRootElement.
if($ProbeUiaRoot){
  & $cmake --build $buildDir --target uia_external_client --parallel 4
  if($LASTEXITCODE){throw 'Build failed'}
  & (Join-Path $buildDir 'uia_external_client.exe') --probe-root
  if($LASTEXITCODE){throw "UIA root probe failed (exit $LASTEXITCODE)"}
  return
}
$target=if($CrossProcess -or $Concurrency -or $Lifecycle -or $ManualNarrator){'editor_cross_host'}elseif($SelfDraw){'editor_probe'}else{'text_probe'}
& $cmake --build $buildDir --target $target --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
if($BuildOnly){return}
Push-Location $buildDir
try {
  $probeExe=".\$target.exe"
  $probeArgs=@()
  if($Interactive -or $ManualNarrator){$probeArgs+='--interactive'}
  if($ManualNarrator){$probeArgs+='--manual-narrator'}
  if($CrossProcess -or $Concurrency -or $Lifecycle){$probeArgs+='--cross-process'}
  if($TraceState){$probeArgs+='--trace-state'}
  if($Concurrency){$probeArgs+='--concurrent'}
  if($Lifecycle){$probeArgs+='--lifecycle'}
  if($DiagnoseUiaFailure){$probeArgs+='--diagnose-uia-failure'}
  if($TestUiaFailureDiagnostics){$probeArgs+='--test-uia-failure-diagnostics'}
  & $probeExe @probeArgs
  if($LASTEXITCODE){throw 'Text probe failed'}
}finally{Pop-Location}
