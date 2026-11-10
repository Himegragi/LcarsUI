param(
    [Parameter(Mandatory)][string]$Destination,
    [string]$Archive,
    [string]$Url = 'https://github.com/microsoft/axe-windows/releases/download/v2.4.2/AxeWindowsCLI-2.4.2.zip',
    [string]$Sha256 = 'aeca43f41c89b3ffb1db84011539e609ecd7cb3badd6e78fada2ada327d10a64'
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Destination) { throw "Axe directory already exists: $Destination" }
New-Item -ItemType Directory -Path $Destination | Out-Null
if (-not $Archive) {
    $Archive = Join-Path $Destination 'axe.zip'
    Invoke-WebRequest -Uri $Url -OutFile $Archive
}
$actual = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash
if ($actual -ne $Sha256) { throw "Axe SHA256 mismatch: $actual" }
Expand-Archive -LiteralPath $Archive -DestinationPath (Join-Path $Destination 'unpacked')
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'unpacked/AxeWindowsCLI.exe'))) { throw 'AxeWindowsCLI.exe missing from archive' }
