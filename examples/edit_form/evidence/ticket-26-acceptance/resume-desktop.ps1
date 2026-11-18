$ErrorActionPreference = 'Stop'
$root = 'C:/Users/user/.t3/worktrees/LcarsUI/t3-b9a83cd8-7567de4ded5a750d'
$fix = 'C:/Users/user/.t3/worktrees/LcarsUI/t3-6586e5ee-6be5a2eeeed9b7de'
$out = "$root/.scratch/acceptance-26"
. "$root/cmake/VisualStudio.ps1"
$vs = Get-LcarsVisualStudio
$toolset = Import-LcarsVisualStudioEnvironment $vs.InstallationPath
$ctest = Join-Path $vs.InstallationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
"VS=$($vs.InstallationVersion); MSVC=$toolset; CI=$env:CI" | Set-Content "$out/environment.txt"
$common = 'sem01|core_regressions|layout|handles|snapshots|option_nodes|removals|uia_unobserved_guard|uia_readiness_web|uia_readiness_material'
$debugExclude = '^(' + $common + '|uia_web|uia_material|uia_ime_simulation_web|uia_global_web|demo_settings_smoke|public_names|perf_instrumentation|perf_script|visual_studio_selection|render_cpu|render_boundary|third_party_notices|components|error_lines|edit_form|multiline|text_flow|justify|animation|context_menu|vertical|richtext|word_select|word_select_icu|list_page|package_install|about_package_no_json|consumer_example|package_contract|diagnostics|diagnostic_queue|about_desktop|about_executable_diagnostics|about_executable_nodiag|uia_demo_web|uia_demo_material|uia_ime_simulation_material|uia_global_material|demo_keyboard|settings_close_host|renderer_failure|presentation_recovery|text|vertical_text|perf_report_only_setup|host_native)$'
& $ctest --test-dir "$fix/build/Debug" -E $debugExclude -j 1 --timeout 1200 --output-on-failure -O "$out/debug-after-interruption.log"
"Debug remaining exit=$LASTEXITCODE" | Add-Content "$out/verdicts.txt"
Copy-Item "$fix/build/Debug/Testing/Temporary/LastTest.log" "$out/debug-after-interruption-LastTest.log"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$releaseExclude = '^(' + $common + '|context_menu)$'
& $ctest --test-dir "$fix/build/Release" -E $releaseExclude -j 1 --timeout 1200 --output-on-failure -O "$out/release-remaining.log"
"Release remaining exit=$LASTEXITCODE" | Add-Content "$out/verdicts.txt"
Copy-Item "$fix/build/Release/Testing/Temporary/LastTest.log" "$out/release-remaining-LastTest.log"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$firstLog = Get-Content "$out/debug-uia-original.log" -Raw
$timeouts = @([regex]::Matches($firstLog, 'Test\s+#\d+:\s+(uia_(?:web|material))\s+\.+\*\*\*Timeout') | ForEach-Object {$_.Groups[1].Value} | Select-Object -Unique)
if ($timeouts.Count) {
    $retry = '^(' + ($timeouts -join '|') + ')$'
    & $ctest --test-dir "$fix/build/Debug" -R $retry -j 1 --timeout 1800 --output-on-failure -V -O "$out/debug-uia-completion.log"
    "Debug fixture completion exit=$LASTEXITCODE; selection=$retry" | Add-Content "$out/verdicts.txt"
    exit $LASTEXITCODE
}
exit 0
