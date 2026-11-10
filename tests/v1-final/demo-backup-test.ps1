$ErrorActionPreference='Stop'
. "$PSScriptRoot/settings-sandbox.ps1"
$root=Join-Path ([IO.Path]::GetTempPath()) ('lcars-backup-test-'+[guid]::NewGuid().ToString('N'))
try {
 New-Item -ItemType Directory -Force "$root/settings/sub" | Out-Null
 [IO.File]::WriteAllBytes("$root/settings/settings.json",[byte[]](0,1,255,10))
 [IO.File]::WriteAllBytes("$root/settings/sub/trial.json",[byte[]](8,9,0,255))
 try { Use-SettingsSandbox "$root/settings" { [IO.File]::WriteAllText("$root/settings/new",'new'); throw 'injected failure' } } catch { if ($_.Exception.Message -ne 'injected failure') { throw } }
 if (([Convert]::ToBase64String([IO.File]::ReadAllBytes("$root/settings/settings.json"))) -ne 'AAH/Cg==' -or ([Convert]::ToBase64String([IO.File]::ReadAllBytes("$root/settings/sub/trial.json"))) -ne 'CAkA/w==' -or (Test-Path "$root/settings/new")) { throw 'restore mismatch' }
 Use-SettingsSandbox "$root/absent" { [IO.File]::WriteAllText("$root/absent/new",'new') }
 if (Test-Path "$root/absent") { throw 'absent directory not restored' }
} finally { if (Test-Path $root) { Remove-Item -Recurse -Force $root } }
