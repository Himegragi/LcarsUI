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
Invoke-WebRequest -Uri $Url -OutFile $archive
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ne $Sha256) { throw "Mesa SHA256 mismatch: $actual" }
$unpacked = Join-Path $Destination 'unpacked'
New-Item -ItemType Directory -Path $unpacked | Out-Null
& $SevenZip x $archive "-o$unpacked" -y
if ($LASTEXITCODE -ne 0) { throw "Mesa extraction failed (exit $LASTEXITCODE)" }
