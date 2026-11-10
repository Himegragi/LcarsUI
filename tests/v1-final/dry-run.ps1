$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot/../.."
$output = & "$root/verify-idle.ps1" -Only v1-final -DryRun 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "v1-final dry run failed: $output" }
foreach ($term in @('estimated', 'clipboard', 'ime', 'dpi', 'host_native', 'perf', 'presentation', 'uia-stress', 'render', 'VC01', 'SendInput', 'RenderDoc', 'TDR', 'OutputDebugStringW', 'hands-on')) {
    if ($output -notmatch [regex]::Escape($term)) { throw "Missing $term from final plan" }
}
# Every required author-machine case has a driver that names it and a step the final run executes.
if ($output -notmatch '(?m)^Steps: (.+)$') { throw 'Final plan does not list its steps' }
$steps = $Matches[1].Trim() -split ','
$cases = @(Import-Csv -Delimiter "`t" -LiteralPath "$PSScriptRoot/cases.tsv")
if (-not $cases.Count) { throw 'cases.tsv is empty' }
$problems = [Collections.Generic.List[string]]::new()
foreach ($row in $cases) {
    if (-not $row.case -or -not $row.step -or -not $row.driver -or -not $row.evidence) { $problems.Add("incomplete row: $($row.case)"); continue }
    if ($row.step -ne 'manual' -and $row.step -notin $steps) { $problems.Add("$($row.case): step '$($row.step)' is not run by verify-v1-final.ps1") }
    $driver = Join-Path $root $row.driver
    if (-not (Test-Path -LiteralPath $driver)) { $problems.Add("$($row.case): driver $($row.driver) does not exist"); continue }
    if (-not (Select-String -LiteralPath $driver -SimpleMatch -Quiet -Pattern $row.case)) { $problems.Add("$($row.case): driver $($row.driver) never names the case") }
    if ($output -notmatch [regex]::Escape($row.case)) { $problems.Add("$($row.case): missing from the final plan") }
}
if (@($cases.case | Group-Object | Where-Object Count -gt 1).Count) { $problems.Add('duplicate case rows') }
if ($problems.Count) { $problems | ForEach-Object { Write-Output $_ }; throw "$($problems.Count) case coverage problem(s)" }
"v1-final dry run passed ($($cases.Count) cases)"
