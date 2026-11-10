param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$CoreOnly,
    [switch]$SkipTests,
    [string]$Tests,
    [string]$Install
)
$ErrorActionPreference = 'Stop'
if ($SkipTests -and $Tests) { throw '-SkipTests and -Tests cannot be combined' }
# Keep stderr visible as text, while using the native exit code to detect failures.
function Invoke-Native([string]$Program, [string[]]$Arguments, [string]$Failure) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                $message = $_.Exception.Message
                if ($message -and $message -ne 'System.Management.Automation.RemoteException') { Write-Host $message }
            } else { Write-Host $_ }
        }
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previous }
    if ($exitCode -ne 0) { throw "$Failure (exit $exitCode)" }
}

. "$PSScriptRoot/cmake/VisualStudio.ps1"
$vs = Get-LcarsVisualStudio
$vsRoot = $vs.InstallationPath
$toolsetVersion = Import-LcarsVisualStudioEnvironment $vsRoot
Write-Host "Visual Studio: $vsRoot ($($vs.InstallationVersion)); MSVC toolset: $toolsetVersion"

# Git Bash can prepend GNU tar, which misinterprets Windows drive-letter paths.
$env:PATH = (Join-Path $env:SystemRoot 'System32') + ';' + $env:PATH

$cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe).Source }
$compiler = (Get-Command clang-cl.exe).Source
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$suffix = if ($CoreOnly) { '-core' } else { '' }
$buildDir = Join-Path $PSScriptRoot "build/$Configuration$suffix"
$desktop = if ($CoreOnly) { 'OFF' } else { 'ON' }
Invoke-Native $cmake @('-S', $PSScriptRoot, '-B', $buildDir, '-G', 'Ninja', "-DCMAKE_C_COMPILER=$compiler", "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_BUILD_TYPE=$Configuration", "-DLCARS_BUILD_DESKTOP=$desktop") 'Configure failed'
Invoke-Native $cmake @('--build', $buildDir, '--parallel', '4') 'Build failed'
[pscustomobject]@{ InstallationPath = $vsRoot; InstallationVersion = $vs.InstallationVersion; Generator = $vs.Generator; ToolsetVersion = $toolsetVersion } |
    ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $buildDir 'lcars-visual-studio.json')
if (-not $SkipTests) {
    $testArgs = @('--test-dir', $buildDir, '--output-on-failure')
    if ($Tests) { $testArgs += @('-R', $Tests) }
    Invoke-Native $ctest $testArgs 'Tests failed'
}

if ($Install) { Invoke-Native $cmake @('--install', $buildDir, '--prefix', $Install) 'Install failed' }
