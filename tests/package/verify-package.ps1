param(
    [Parameter(Mandatory=$true)][string]$Prefix,
    [ValidateSet('Debug','Release')][string[]]$Configurations = @('Debug','Release'),
    [ValidateSet('Ninja','VS2022')][string[]]$Generators = @('Ninja','VS2022'),
    [string]$Evidence = 'build/evidence/package'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$prefixPath = (Resolve-Path -LiteralPath $Prefix).Path
New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
$evidencePath = (Resolve-Path -LiteralPath $Evidence).Path
$work = Join-Path ([IO.Path]::GetTempPath()) ('lcars-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found' }
$env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
$devCmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$envLines = & cmd.exe /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed' }
foreach ($line in $envLines) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake.exe).Source }
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$clang = (Get-Command clang-cl.exe).Source
$cl = (Get-Command cl.exe).Source
$rows = [Collections.Generic.List[object]]::new()
$counter = 0
function Invoke-Logged([string]$program, [string[]]$arguments, [string]$log) {
    $old = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; $output = & $program @arguments 2>&1 | Out-String; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $old }
    Add-Content -LiteralPath $log -Value ("`n> " + $program + ' ' + ($arguments -join ' ') + "`n" + $output)
    return @{ Code = $code; Output = $output }
}
function Run-Case($name, $generator, $config, $source, $mode = 'build', $extra = @(), $expected = '') {
    $script:counter++
    $id = '{0:D3}' -f $script:counter
    $dir = Join-Path $work $id
    $log = Join-Path $evidencePath "$id.log"
    $genName = if ($generator -eq 'Ninja') { 'Ninja' } else { 'Visual Studio 17 2022' }
    $compiler = if ($generator -eq 'Ninja') { $clang } else { $cl }
    $compilerLabel = if ($generator -eq 'Ninja') { 'clang-cl' } else { 'cl.exe' }
    $args = @('-S', $source, '-B', $dir, '-G', $genName, '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF')
    if ($name -ne 'reject/no-prefix') { $args += "-DCMAKE_PREFIX_PATH=$prefixPath" }
    if ($generator -eq 'Ninja') { $args += "-DCMAKE_CXX_COMPILER=$compiler"; if ($config) { $args += "-DCMAKE_BUILD_TYPE=$config" } }
    $args += $extra
    $result = Invoke-Logged $cmake $args $log
    $ok = $true; $message = ''
    if ($mode -eq 'reject') {
        $ok = $result.Code -ne 0 -and $result.Output -match $expected
        $message = if ($ok) { 'rejected with expected diagnostic' } else { 'missing rejection or diagnostic; see log' }
    } elseif ($result.Code -ne 0) { $ok = $false; $message = 'configure failed; see log' }
    elseif ($mode -ne 'configure') {
        $result = Invoke-Logged $cmake @('--build', $dir, '--config', $config, '--parallel', '4', '--verbose') $log
        if ($result.Code -ne 0) { $ok = $false; $message = 'build failed; see log' }
        elseif ($mode -eq 'consume') {
            $buildOutput = $result.Output
            $result = Invoke-Logged $ctest @('--test-dir', $dir, '-C', $config, '--output-on-failure') $log
            if ($result.Code -ne 0 -or $result.Output -notmatch 'profile_alias' -or ($script:hasDesktop -and $result.Output -notmatch 'notices_smoke') -or (-not $script:hasDesktop -and $result.Output -match 'notices_smoke')) { $ok = $false; $message = 'consumer tests or Desktop selection failed; see log' }
            if ($ok -and $generator -eq 'Ninja') {
                $commands = Invoke-Logged 'ninja.exe' @('-C', $dir, '-t', 'commands') $log
                $lib = if ($config -eq 'Debug') { 'lcars_cored\.lib' } else { 'lcars_core\.lib' }
                if ($commands.Code -ne 0 -or $commands.Output -notmatch $lib) { $ok = $false; $message = 'wrong Core library selected; see log' }
            }
            if ($ok -and $generator -eq 'VS2022') {
                $lib = if ($config -eq 'Debug') { 'lcars_cored\.lib' } else { 'lcars_core\.lib' }
                if ($buildOutput -notmatch $lib) { $ok = $false; $message = 'wrong Core library selected; see log' }
            }
        }
    }
    $cache = Join-Path $dir 'CMakeCache.txt'
    $version = if (Test-Path $cache) { (Select-String -Path (Join-Path $dir 'CMakeFiles/*/CMakeCXXCompiler.cmake') -Pattern '^set\(CMAKE_CXX_COMPILER_VERSION ' -ErrorAction SilentlyContinue | Select-Object -First 1).Line } else { '' }
    $status = if ($ok) { 'PASS' } else { 'FAIL' }
    if (-not $message) { $message = 'completed' }
    $script:rows.Add([pscustomobject]@{ Scenario=$name; Generator=$generator; Configuration=$config; Compiler="$compilerLabel $version"; Result=$status; Message="$message ($id.log)" })
    Write-Host "$status $name $generator $config $message"
}
function Source($name, $body, $code = 'int main() { return 0; }') {
    $path = Join-Path $work "src-$name"
    New-Item -ItemType Directory -Force $path | Out-Null
    Set-Content -Encoding ASCII (Join-Path $path 'main.cpp') $code
    Set-Content -Encoding ASCII (Join-Path $path 'CMakeLists.txt') ("cmake_minimum_required(VERSION 3.25)`nproject(PackageProbe LANGUAGES CXX)`n" + $body)
    return $path
}
try {
    $targetDir = Join-Path $prefixPath 'lib/cmake/LcarsUI'
    $components = @(Get-ChildItem $targetDir -Filter 'LcarsUI*Targets.cmake' | ForEach-Object { $_.Name -replace '^LcarsUI(.*)Targets\.cmake$', '$1' })
    if ($components -notcontains 'Core') { throw 'Prefix does not contain the Core package' }
    $hasDesktop = $components -contains 'Desktop'
    $consumer = Join-Path $root 'examples/consumer'
    foreach ($gen in $Generators) {
        $configs = @($Configurations)
        if ($gen -eq 'VS2022' -and $Configurations -contains 'Release') { $configs += 'RelWithDebInfo' }
        foreach ($cfg in $configs) { Run-Case "consume/$gen/$cfg" $gen $cfg $consumer 'consume' }
    }
    $primary = if ($Generators -contains 'Ninja') { 'Ninja' } else { $Generators[0] }
    $cfg = if ($Configurations -contains 'Release') { 'Release' } else { $Configurations[0] }
    foreach ($component in $components) {
        $body = "find_package(LcarsUI REQUIRED COMPONENTS $component)`n"
        foreach ($other in $components) {
            $present = $other -eq 'Core' -or $other -eq $component
            $condition = if ($present) { 'NOT TARGET' } else { 'TARGET' }
            $body += "if($condition LcarsUI::$other)`n message(FATAL_ERROR `"unexpected target state: $other`")`nendif()`n"
        }
        $body += "add_executable(probe main.cpp)`ntarget_link_libraries(probe PRIVATE LcarsUI::$component)`n"
        $header = if ($component -eq 'Desktop') { 'desktop.hpp' } elseif ($component -eq 'Diagnostics') { 'diagnostics.hpp' } elseif ($component -like 'Appearance*') { 'appearance.hpp' } else { 'core.hpp' }
        $call = switch ($component) {
            'Core' { 'lcars::Ui ui;' }
            'AppearanceWeb' { '(void)lcars::web_appearance();' }
            'AppearanceMaterial' { '(void)lcars::material_appearance();' }
            'Desktop' { '(void)lcars::third_party_notices();' }
            default { '' }
        }
        if ($component -eq 'Desktop') { $header = 'third_party_notices.hpp' }
        $src = Source "component-$component" $body "#include <lcars/$header>`nint main() { $call return 0; }"
        Run-Case "component/$component" $primary $cfg $src
    }
    $baseBody = 'find_package(LcarsUI REQUIRED COMPONENTS Core)' + "`nadd_executable(probe main.cpp)`ntarget_link_libraries(probe PRIVATE LcarsUI::Core)`n"
    foreach ($header in Get-ChildItem (Join-Path $prefixPath 'include/lcars') -Filter '*.hpp') {
        $src = Source "header-$($header.BaseName)" $baseBody "#include <lcars/$($header.Name)>`nint main() { return 0; }"
        Run-Case "header/$($header.Name)" $primary $cfg $src
    }
    foreach ($std in 17,23) {
        $minimum = if ($std -eq 17) { '202002L' } else { '202003L' }
        $src = Source "std-$std" $baseBody "#if _MSVC_LANG < $minimum`n#error incorrect language standard`n#endif`nint main() { return 0; }"
        Run-Case "std/$std" $primary $cfg $src 'build' @("-DCMAKE_CXX_STANDARD=$std")
    }
    $req = Source 'required' 'find_package(LcarsUI REQUIRED COMPONENTS Core)'
    $unknown = Source 'unknown' 'find_package(LcarsUI REQUIRED COMPONENTS Widgets)'
    Run-Case 'reject/unknown-component' $primary $cfg $unknown 'reject' @() '(?s)Widgets.*available'
    $optional = Source 'optional' "find_package(LcarsUI REQUIRED OPTIONAL_COMPONENTS Widgets)`nif(LcarsUI_Widgets_FOUND)`n message(FATAL_ERROR `"unexpected component`" )`nendif()"
    Run-Case 'optional/unknown-component' $primary $cfg $optional 'configure'
    if (-not $hasDesktop) {
        $missing = Source 'missing' 'find_package(LcarsUI REQUIRED COMPONENTS Desktop)'
        Run-Case 'reject/missing-desktop' $primary $cfg $missing 'reject' @() 'LCARS_BUILD_DESKTOP=OFF'
    } else { $rows.Add([pscustomobject]@{Scenario='reject/missing-desktop';Generator=$primary;Configuration=$cfg;Compiler='';Result='N/A';Message='Desktop installed'}) }
    Run-Case 'reject/runtime' $primary $cfg $req 'reject' @('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded') '(?s)runtime mismatch.*consumer MultiThreaded.*LcarsUI\s+MultiThreaded.*DLL'
    if ($Generators -contains 'Ninja') { Run-Case 'reject/empty-build-type' 'Ninja' '' $req 'reject' @('-DCMAKE_BUILD_TYPE=') 'specify -DCMAKE_BUILD_TYPE' }
    foreach ($v in '0.1','1.0','0.2') {
        $src = Source "version-$v" "find_package(LcarsUI $v REQUIRED)"
        if ($v -eq '0.2') { Run-Case "version/$v" $primary $cfg $src 'configure' } else { Run-Case "reject/version/$v" $primary $cfg $src 'reject' @() 'version' }
    }
    Run-Case 'reject/no-prefix' $primary $cfg $req 'reject' @('-DCMAKE_PREFIX_PATH=', '-DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF') 'LcarsUI'
    # Exercise relocation on an owned copy: the caller's prefix is never removed.
    $first = Join-Path $work 'original-prefix'; $moved = Join-Path $work 'moved-prefix'
    Copy-Item -Recurse -LiteralPath $prefixPath -Destination $first
    Move-Item -LiteralPath $first -Destination $moved
    $saved = $prefixPath; $prefixPath = $moved
    Run-Case 'relocate' $primary $cfg $consumer 'consume'
    $prefixPath = $saved
} finally {
    $rows | Export-Csv -LiteralPath (Join-Path $evidencePath 'results.tsv') -Delimiter "`t" -NoTypeInformation -Encoding UTF8
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
if (@($rows | Where-Object Result -eq 'FAIL').Count) { exit 1 }

