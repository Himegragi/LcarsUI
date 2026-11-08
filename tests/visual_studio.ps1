$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../cmake/VisualStudio.ps1"
foreach ($case in @(
    @{ Version = '17.14.12'; Generator = 'Visual Studio 17 2022' },
    @{ Version = '18.1.2'; Generator = 'Visual Studio 18 2026' }
)) {
    $actual = Get-LcarsVisualStudioGenerator $case.Version
    if ($actual -ne $case.Generator) { throw "Wrong generator for $($case.Version): $actual" }
}
try { Get-LcarsVisualStudioGenerator '19.0.0'; throw 'Unsupported version was accepted' }
catch { if ($_.Exception.Message -eq 'Unsupported version was accepted') { throw } }
Write-Host 'Visual Studio generator selection passed'
