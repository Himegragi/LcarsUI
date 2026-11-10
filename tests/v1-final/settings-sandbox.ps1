function Use-SettingsSandbox([string]$Directory, [scriptblock]$Action) {
    $backup = Join-Path ([IO.Path]::GetTempPath()) ('lcars-settings-backup-' + [guid]::NewGuid().ToString('N'))
    $existed = Test-Path -LiteralPath $Directory
    if ($existed) { Copy-Item -LiteralPath $Directory -Destination $backup -Recurse -Force }
    try {
        if ($existed) { Remove-Item -LiteralPath $Directory -Recurse -Force }
        New-Item -ItemType Directory -Path $Directory -Force | Out-Null
        & $Action
    } finally {
        if (Test-Path -LiteralPath $Directory) { Remove-Item -LiteralPath $Directory -Recurse -Force }
        if ($existed) { Move-Item -LiteralPath $backup -Destination $Directory }
    }
}
