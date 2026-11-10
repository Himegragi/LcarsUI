param(
    [Parameter(Mandatory)][string]$Url,
    [Parameter(Mandatory)][string]$Sha256,
    [Parameter(Mandatory)][string]$Destination,
    [string]$SevenZip = '7z'
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Destination) { throw "Mesa directory already exists: $Destination" }
New-Item -ItemType Directory -Path $Destination | Out-Null
$archive = Join-Path $Destination 'mesa.7z'
$maxAttempts = 4
for ($attempt = 1; ; $attempt++) {
    try { Invoke-WebRequest -Uri $Url -OutFile $archive; break }
    catch {
        if ($attempt -ge $maxAttempts) { throw }
        Write-Warning "Mesa download attempt $attempt failed: $($_.Exception.Message); retrying"
        Start-Sleep -Seconds (15 * $attempt)
    }
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ne $Sha256) { throw "Mesa SHA256 mismatch: $actual" }
$unpacked = Join-Path $Destination 'unpacked'
New-Item -ItemType Directory -Path $unpacked | Out-Null
& $SevenZip x $archive "-o$unpacked" -y
if ($LASTEXITCODE -ne 0) { throw "Mesa extraction failed (exit $LASTEXITCODE)" }
