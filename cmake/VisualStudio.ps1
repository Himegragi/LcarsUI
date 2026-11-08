function Get-LcarsVisualStudioGenerator([string]$Version) {
    $major = ([version]$Version).Major
    switch ($major) {
        17 { return 'Visual Studio 17 2022' }
        18 { return 'Visual Studio 18 2026' }
        default { throw "Unsupported Visual Studio version $Version; no CMake generator is configured" }
    }
}

function Get-LcarsVisualStudio {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    $json = & $vswhere -latest -version '[17.0,)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8
    if ($LASTEXITCODE -ne 0) { throw 'vswhere failed to find Visual Studio' }
    $instances = @($json | ConvertFrom-Json)
    if (-not $instances -or -not $instances[0].installationPath) { throw 'Visual Studio 2022 or later with C++ Build Tools not found' }
    $instance = $instances[0]
    [pscustomobject]@{ InstallationPath = $instance.installationPath; InstallationVersion = $instance.installationVersion; Generator = (Get-LcarsVisualStudioGenerator $instance.installationVersion) }
}

function Import-LcarsVisualStudioEnvironment([string]$InstallationPath, [string]$ToolsetVersion = '') {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    $env:PATH = (Split-Path $vswhere) + ';' + $env:PATH
    $devCmd = Join-Path $InstallationPath 'Common7\Tools\VsDevCmd.bat'
    $toolsetArg = if ($ToolsetVersion) { " -vcvars_ver=$ToolsetVersion" } else { '' }
    $envLines = & cmd.exe /d /c "call `"$devCmd`" -no_logo -arch=x64 -host_arch=x64$toolsetArg >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed' }
    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
    if (-not $env:VCToolsVersion) { throw 'VsDevCmd did not select an MSVC toolset version' }
    $selected = $env:VCToolsVersion.TrimEnd('\', '/')
    if ($ToolsetVersion -and $selected -ne $ToolsetVersion) { throw "Requested MSVC toolset $ToolsetVersion, but VsDevCmd selected $selected" }
    $expectedTools = [IO.Path]::GetFullPath((Join-Path $InstallationPath 'VC/Tools/MSVC')).TrimEnd('\') + '\'
    if (-not $env:VCToolsInstallDir -or -not [IO.Path]::GetFullPath($env:VCToolsInstallDir).StartsWith($expectedTools, [StringComparison]::OrdinalIgnoreCase)) {
        throw "VsDevCmd did not select MSVC tools from $InstallationPath"
    }
    return $selected
}
