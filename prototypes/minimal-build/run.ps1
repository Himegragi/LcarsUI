param(
    [ValidateSet('msvc','clang')][string]$Compiler = 'clang',
    [string]$CMakeExecutable = '',
    [switch]$Interactive,
    [switch]$CoreOnly
)
$ErrorActionPreference = 'Stop'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ Build Tools not found' }
$devCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
# Import the native developer environment only into this PowerShell process.
$envLines = & cmd.exe /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed' }
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$compilerExe = if ($Compiler -eq 'clang') { 'C:/Program Files/LLVM/bin/clang-cl.exe' } else { 'cl.exe' }
if (-not $CMakeExecutable) {
    $CMakeExecutable = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $CMakeExecutable)) { $CMakeExecutable = (Get-Command cmake.exe).Source }
}
$cmakeVersion = ((& $CMakeExecutable --version | Select-Object -First 1) -split ' ')[2]
$suffix = if ($CoreOnly) { '-core' } else { '' }
$buildDir = Join-Path $PSScriptRoot "build/$Compiler-cmake-$cmakeVersion$suffix"
$glEnabled = if ($CoreOnly) { 'OFF' } else { 'ON' }
& $CMakeExecutable -S $PSScriptRoot -B $buildDir -G Ninja "-DCMAKE_CXX_COMPILER=$compilerExe" -DCMAKE_BUILD_TYPE=Debug "-DPROBE_BUILD_OPENGL=$glEnabled"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $CMakeExecutable --build $buildDir --verbose
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& (Join-Path $buildDir 'core_consumer.exe')
if ($LASTEXITCODE -ne 0) { throw 'Core consumer failed' }
if (-not $CoreOnly) {
    Push-Location $buildDir
    try {
        if ($Interactive) { & '.\gl_probe.exe' --interactive }
        else { & '.\gl_probe.exe' }
        if ($LASTEXITCODE -ne 0) { throw 'OpenGL probe failed' }
    } finally { Pop-Location }
}
