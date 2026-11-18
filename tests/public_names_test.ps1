$ErrorActionPreference = 'Stop'
$checker = Join-Path $PSScriptRoot 'public_names.ps1'
$fixture = Join-Path $PSScriptRoot 'public_names/bad.hpp.txt'
$output = & powershell -NoProfile -File $checker -Headers $fixture 2>&1 | Out-String
if ($LASTEXITCODE -eq 0) { throw "The checker accepted snake_case public declarations: $output" }
foreach ($name in @('deliberately_bad', 'component_id', 'default_limit', 'BAD_CONSTANT', 'radio_group', 'publicTrailing_', 'still_snake_', 'collision_')) {
    if ($output -notmatch "'$name'") { throw "Missing diagnostic for ${name}: $output" }
}
$fixture = Join-Path $PSScriptRoot 'public_names/allowed.hpp.txt'
$output = & powershell -NoProfile -File $checker -Headers $fixture 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Ignored C++ tokens must not fail the check: $output" }
$output = & powershell -NoProfile -File $checker 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "Public headers violate the naming rule: $output" }
Write-Output $output
