param([ValidateRange(1,200)][int]$Trials=20,[ValidateSet('none','composition','selection')][string]$Fault='none')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'run.ps1') -CrossProcess -BuildOnly
$executable=Join-Path $PSScriptRoot 'build/clang/editor_cross_host.exe'
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$outputRoot=Join-Path $PSScriptRoot "build/stress/$runId"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$results=@()
for($trial=1;$trial -le $Trials;$trial++){
  $trialDir=Join-Path $outputRoot ('trial-{0:D3}' -f $trial)
  New-Item -ItemType Directory -Path $trialDir -Force | Out-Null
  Push-Location $trialDir
  try {
    $arguments=@('--cross-process','--trace-state')
    if($Fault -ne 'none'){$arguments+="--inject-test-$Fault"}
    # Windows PowerShell treats native stderr as ErrorRecord. Capture it first,
    # then use the process exit code so failures still get a summary and path.
    $savedPreference=$ErrorActionPreference
    try {
      $ErrorActionPreference='Continue'
      & $executable @arguments *> host.log
      $code=$LASTEXITCODE
    } finally {$ErrorActionPreference=$savedPreference}
  } finally {Pop-Location}
  $results+=[pscustomobject]@{trial=$trial;exit_code=$code;directory=$trialDir}
  $results|Export-Csv -LiteralPath (Join-Path $outputRoot 'summary.csv') -NoTypeInformation -Encoding UTF8
  Write-Output "trial=$trial exit=$code artifacts=$trialDir"
  if($code -ne 0){
    $failure=[Exception]::new("UIA failure; evidence retained at $trialDir")
    $failure.Data['ArtifactDirectory']=$trialDir
    $failure.Data['ProcessExitCode']=$code
    throw $failure
  }
}
Write-Output "PASS $Trials runs. Artifacts: $outputRoot"
