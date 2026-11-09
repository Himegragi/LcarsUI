$ErrorActionPreference = 'Stop'
$root = Join-Path $env:TEMP ('lcars-perf-script-' + [guid]::NewGuid())
try {
  foreach ($appearance in 'web','material') { foreach ($size in '1080','4k') { foreach ($case in 'startup','list-hold-down','typing','idle','delay') {
    $dir = Join-Path $root "perf/$appearance/$size/$case"; New-Item -ItemType Directory -Force $dir | Out-Null
    $actualCase = if ($case -eq 'delay') { 'list-hold-down' } else { $case }
    $resolution = if ($size -eq '4k') { '3840x2160' } else { '1920x1080' }
    "scenario`tappearance`tresolution`tphase`tstatus`n$actualCase`t$appearance`t$resolution`twarmup`tPASS`n$actualCase`t$appearance`t$resolution`tsteady`tPASS" | Set-Content (Join-Path $dir 'perf.tsv')
  } } }
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/../verify-idle.ps1" -Only perf -DryRun -EvidenceDir $root
  if ($LASTEXITCODE -ne 0) { throw 'valid matrix rejected' }
  Remove-Item (Join-Path $root 'perf/web/4k/typing/perf.tsv')
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/../verify-idle.ps1" -Only perf -DryRun -EvidenceDir $root
  if ($LASTEXITCODE -eq 0) { throw 'missing report accepted' }
} finally { Remove-Item -LiteralPath $root -Recurse -Force }
