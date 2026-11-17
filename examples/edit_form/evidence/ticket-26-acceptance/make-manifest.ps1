$out = $PSScriptRoot
$fix = 'C:/Users/user/.t3/worktrees/LcarsUI/t3-6586e5ee-6be5a2eeeed9b7de'
$expected = @([regex]::Matches((Get-Content "$fix/build/Debug/CTestTestfile.cmake" -Raw), '(?m)^add_test\(\[=\[(\w+)\]=\]') | ForEach-Object {$_.Groups[1].Value})
$groups = @{
 Debug = @('core-fix-debug.log','debug-before-interruption.log','debug-after-interruption.log','authorized-demo-settings-smoke.log','debug-uia-completion.log')
 Release = @('core-fix-release.log','release-remaining.log')
 ReleaseCore = @('release-core.log')
}
$result = @{}
foreach ($config in $groups.Keys) {
 $passes = @{}
 foreach ($file in $groups[$config]) {
  $path = Join-Path $out $file
  if (-not (Test-Path $path)) {continue}
  foreach ($m in [regex]::Matches((Get-Content $path -Raw), 'Test\s+#\d+:\s+(\w+)\s+\.+\s+Passed\s+([\d.]+) sec')) {
   $passes[$m.Groups[1].Value] = @{log=$file;seconds=[double]$m.Groups[2].Value}
  }
 }
 if ($config -eq 'Debug') {
  foreach ($m in [regex]::Matches((Get-Content "$out/core-fix-uia-partial.log" -Raw), '(?s)\d+/73 Testing: (\w+)(.*?)(?=\r?\n\d+/73 Testing:|\z)')) {
   if ($m.Groups[2].Value -match 'Test Passed\.') {
    $t=[regex]::Match($m.Groups[2].Value,'Test time = ([\d.]+) sec')
    if (-not $passes.ContainsKey($m.Groups[1].Value)) {$passes[$m.Groups[1].Value]=@{log='core-fix-uia-partial.log'; seconds=[double]$t.Groups[1].Value}}
   }
  }
 }
 $missing = if ($config -ne 'ReleaseCore') {@($expected | Where-Object {-not $passes.ContainsKey($_)})} else {@()}
 $result[$config]=@{passCount=$passes.Count;expectedCount=$(if($config -eq 'ReleaseCore'){43}else{$expected.Count});missing=@($missing | Where-Object {$_});passes=$passes}
 "$config $($passes.Count) passes, missing: $($missing -join ', ')"
}
$result | ConvertTo-Json -Depth 6 | Set-Content "$out/pass-manifest.json" -Encoding UTF8
