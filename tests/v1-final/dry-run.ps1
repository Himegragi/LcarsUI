$ErrorActionPreference = 'Stop'
$output = & "$PSScriptRoot/../../verify-idle.ps1" -Only v1-final -DryRun 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "v1-final dry run failed: $output" }
foreach ($term in @('estimated', 'clipboard', 'ime', 'dpi', 'host_native', 'perf', 'presentation', 'uia-stress', 'render', 'VC01', 'SendInput', 'RenderDoc', 'TDR', 'OutputDebugStringW', 'hands-on')) {
    if ($output -notmatch [regex]::Escape($term)) { throw "Missing $term from final plan" }
}
'v1-final dry run passed'
