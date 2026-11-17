$ErrorActionPreference = 'Stop'
Set-Location (Resolve-Path (Join-Path $PSScriptRoot '../../../..'))
. "$PSScriptRoot/../../../../cmake/VisualStudio.ps1"
$vs = Get-LcarsVisualStudio
Import-LcarsVisualStudioEnvironment $vs.InstallationPath | Out-Null
$cmake = Join-Path $vs.InstallationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$compiler = (Get-Command clang-cl.exe).Source
& $cmake -S . -B build/Release -G Ninja "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_CXX_COMPILER=$compiler" -DCMAKE_BUILD_TYPE=Release -DLCARS_BUILD_DESKTOP=ON
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build build/Release --parallel 4 --target lcars_uia_client lcars_option_nodes_tests lcars_snapshots_tests lcars_removals_tests lcars_tests
if ($LASTEXITCODE) { throw 'Build failed' }
& $ctest --test-dir build/Release -R '^(option_nodes|snapshots|removals|core_regressions)$' --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
$inventory = (& $ctest --test-dir build/Release --show-only=json-v1 | ConvertFrom-Json)
if ($LASTEXITCODE) { throw 'Test inventory failed' }
$inventory.tests | Where-Object { $_.name -match 'readiness|accessibility' } | ForEach-Object { Write-Output $_.name }
Write-Output "Total Tests: $($inventory.tests.Count)"
exit 0
