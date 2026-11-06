param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$CoreOnly
)
$ErrorActionPreference = 'Stop'
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ Build Tools not found' }
$devCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
# VsDevCmd calls vswhere.exe by name; the installer folder is not on PATH by default.
$env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
# Import the native developer environment only into this PowerShell process.
$envLines = & cmd.exe /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed' }
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

$cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe).Source }
$compiler = (Get-Command clang-cl.exe).Source
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$suffix = if ($CoreOnly) { '-core' } else { '' }
$buildDir = Join-Path $PSScriptRoot "build/$Configuration$suffix"
$desktop = if ($CoreOnly) { 'OFF' } else { 'ON' }
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_BUILD_TYPE=$Configuration" "-DLCARS_BUILD_DESKTOP=$desktop"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
& $cmake --build $buildDir --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& $ctest --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
