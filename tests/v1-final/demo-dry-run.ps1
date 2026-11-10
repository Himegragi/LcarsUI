$ErrorActionPreference = 'Stop'
$driver = "$PSScriptRoot/../../verify-idle.ps1"
$output = & $driver -Only ' DeMo ' -DryRun 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw "demo dry run failed: $output" }
foreach ($term in @('KW02', 'CR09-real', 'APPDATA', 'CR05')) { if ($output -notmatch $term) { throw "Missing $term" } }
foreach ($bad in @('demo,unknown','demo,demo',',,,')) {
 $rejected=$false
 try { & $driver -Only $bad -DryRun 2>&1 | Out-Null } catch { $rejected=$true }
 if (!$rejected) { throw "Invalid selection accepted: $bad" }
}
'demo selection dry run passed'
