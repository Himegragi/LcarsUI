param(
    # All runs Debug then Release, the same pair core.yml runs.
    [ValidateSet('Debug','Release','All')][string]$Configuration = 'Debug',
    [switch]$CoreOnly,
    [switch]$SkipTests,
    [string]$Tests,
    [string]$Install,
    # Release-only AddressSanitizer build in build/Release[-core]-asan; package tests are left out.
    [switch]$AddressSanitizer
)
$ErrorActionPreference = 'Stop'
if ($SkipTests -and $Tests) { throw '-SkipTests and -Tests cannot be combined' }
if ($AddressSanitizer -and $Configuration -ne 'Release') { throw '-AddressSanitizer needs -Configuration Release (the sanitizer runtime uses the release CRT)' }
if ($AddressSanitizer -and $Install) { throw '-AddressSanitizer and -Install cannot be combined' }
if ($Configuration -eq 'All') {
    foreach ($each in @('Debug', 'Release')) {
        $forward = @{} + $PSBoundParameters
        $forward.Configuration = $each
        & $PSCommandPath @forward
    }
    return
}
# VsDevCmd appends to PATH and friends; restore them so repeated runs in one session stay under cmd.exe's line limit.
$savedEnvironment = @{}
Get-ChildItem env: | ForEach-Object { $savedEnvironment[$_.Name] = $_.Value }
try {
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
    # A newer CI toolset rejects code an older local one accepts, so builds here can pass and CI still fail.
    $ciToolsetFile = Join-Path $PSScriptRoot 'cmake/ci-msvc-toolset.txt'
    $ciToolset = (Get-Content -LiteralPath $ciToolsetFile -TotalCount 1).Trim()
    if ($env:GITHUB_ACTIONS -eq 'true') {
        if ($toolsetVersion -ne $ciToolset) { Write-Host "::warning file=cmake/ci-msvc-toolset.txt::The runner uses MSVC $toolsetVersion; update cmake/ci-msvc-toolset.txt (now $ciToolset) so local builds compare against it." }
    } elseif ([version]$toolsetVersion -lt [version]$ciToolset) {
        Write-Warning "Local MSVC $toolsetVersion is older than CI's $ciToolset (cmake/ci-msvc-toolset.txt). A build that passes here can still fail on CI; install the Visual Studio toolset CI uses."
    }

    # Git Bash can prepend GNU tar, which misinterprets Windows drive-letter paths.
    $env:PATH = (Join-Path $env:SystemRoot 'System32') + ';' + $env:PATH

    $cmake = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe).Source }
    $compiler = (Get-Command clang-cl.exe).Source
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    $suffix = if ($CoreOnly) { '-core' } else { '' }
    if ($AddressSanitizer) { $suffix += '-asan' }
    $buildDir = Join-Path $PSScriptRoot "build/$Configuration$suffix"
    $desktop = if ($CoreOnly) { 'OFF' } else { 'ON' }
    $configureArgs = @('-S', $PSScriptRoot, '-B', $buildDir, '-G', 'Ninja', "-DCMAKE_C_COMPILER=$compiler", "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_BUILD_TYPE=$Configuration", "-DLCARS_BUILD_DESKTOP=$desktop")
    if ($AddressSanitizer) {
        $configureArgs += @('-DLCARS_SANITIZE_ADDRESS=ON', '-DLCARS_INSTALL=OFF')
        # The instrumented executables load Visual Studio's clang_rt.asan_dynamic-x86_64.dll (see CMakeLists.txt).
        $env:PATH = (Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64') + ';' + $env:PATH
    }
    Invoke-Native $cmake $configureArgs 'Configure failed'
    Invoke-Native $cmake @('--build', $buildDir, '--parallel', '4') 'Build failed'
    [pscustomobject]@{ InstallationPath = $vsRoot; InstallationVersion = $vs.InstallationVersion; Generator = $vs.Generator; ToolsetVersion = $toolsetVersion } |
        ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $buildDir 'lcars-visual-studio.json')
    if (-not $SkipTests) {
        $testArgs = @('--test-dir', $buildDir, '--output-on-failure')
        if ($Tests) { $testArgs += @('-R', $Tests) }
        Invoke-Native $ctest $testArgs 'Tests failed'
    }

    if ($Install) { Invoke-Native $cmake @('--install', $buildDir, '--prefix', $Install) 'Install failed' }
} finally {
    Get-ChildItem env: | Where-Object { -not $savedEnvironment.ContainsKey($_.Name) } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    foreach ($name in $savedEnvironment.Keys) { Set-Item -LiteralPath "env:$name" -Value $savedEnvironment[$name] }
}
