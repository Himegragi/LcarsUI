$ErrorActionPreference='Stop'
foreach($mode in @('composition','selection')){
  $artifact=$null
  try {& (Join-Path $PSScriptRoot 'stress-uia.ps1') -Trials 1 -Fault $mode}
  catch {
    $failure=$_.Exception
    if(-not $failure.Data['ArtifactDirectory'] -and $failure.InnerException){$failure=$failure.InnerException}
    $artifact=$failure.Data['ArtifactDirectory']
    if(-not $artifact -or $failure.Data['ProcessExitCode'] -ne 1){throw}
  }
  if(-not $artifact){throw "$mode injection did not fail as expected"}
  $hostText=Get-Content -LiteralPath (Join-Path $artifact 'host.log') -Raw
  $clientText=Get-Content -LiteralPath (Join-Path $artifact 'uia-cross-process.log') -Raw
  $expected=if($mode -eq 'composition'){'FAIL write Value across process boundary'}else{'FAIL UIA selection matches selected character'}
  if(-not $clientText.Contains($expected)){throw "$mode failed for an unrelated reason"}
  if(-not $hostText.Contains('cleanup_host_detached=1')){throw "$mode left a callback attached to a destroyed model"}
  $rows=Import-Csv -LiteralPath (Join-Path $artifact 'state-trace.tsv') -Delimiter "`t"
  if(-not $rows){throw "$mode missing state trace"}
  foreach($row in $rows){
    if([int]$row.anchor -gt [int]$row.length -or [int]$row.caret -gt [int]$row.length){throw "$mode invalid state during teardown"}
  }
  Write-Output "PASS ${mode}: intended failure captured, callbacks detached, trace invariants preserved"
}
