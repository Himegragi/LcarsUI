# Cases: PF01-PF05 (spec 4 demo performance)
[CmdletBinding()]
param([string]$OutDir = "$PSScriptRoot/../../build/Release/evidence/demo-perf", [switch]$List)
$cases=@('startup','startup-font','list-hold-down','typing','idle','delay')
if ($List) { 'Demo performance: web/material, 1080p/4K, startup, hold-down, typing, idle, 20 ms self-check'; exit 0 }
$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot '../../build/Release/lcars_host_verify.exe'
$results=[System.Collections.Generic.List[object]]::new()
foreach($appearance in @('web','material')) { foreach($size in @('1080','4k')) { foreach($scenario in $cases) {
 if($size -eq '4k' -and $scenario -in @('startup','startup-font')) { continue }
 $folder=Join-Path $OutDir "$appearance/$size/$scenario"; New-Item -ItemType Directory -Force $folder | Out-Null
 $case=if($scenario -eq 'delay'){'list-hold-down'}elseif($scenario -eq 'startup-font'){'startup'}else{$scenario}
 $arguments=@('--demo-perf',$case,'--perf-appearance',$appearance,'--evidence',$folder)
 if($size -eq '4k'){$arguments+='--perf-large'}
 if($scenario -eq 'delay'){$arguments+=@('--paint-delay-ms','20')}
 if($scenario -eq 'startup-font'){$arguments+='--demo-perf-font'}
 & $exe @arguments *> (Join-Path $folder 'console.log'); $code=$LASTEXITCODE
 $path=Join-Path $folder 'demo-perf.tsv'; $status='FAIL'
 if((Test-Path $path) -and $code -eq 0) {
   $rows=@(Import-Csv -Delimiter "`t" $path)
   if($rows.Count -eq 2 -and @($rows | Where-Object { $_.status -ne 'PASS' -or $_.run_kind -ne 'measurement' }).Count -eq 0) {$status='PASS'}
 }
 $results.Add([pscustomobject]@{appearance=$appearance;size=$size;scenario=$scenario;status=$status;evidence=$path})
 $results | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir 'summary.tsv')
} } }
if(@($results | Where-Object status -ne 'PASS').Count){exit 1}
