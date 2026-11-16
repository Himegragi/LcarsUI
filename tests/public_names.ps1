param([string[]]$Headers)
$ErrorActionPreference = 'Stop'
if (-not $Headers) {
    $Headers = @(Get-ChildItem (Join-Path $PSScriptRoot '../include/lcars') -Filter *.hpp -Recurse |
        ForEach-Object FullName)
    # This public header is generated at configure time.
    $Headers += Join-Path $PSScriptRoot '../cmake/version.hpp.in'
}

# Tokenize C++ rather than searching raw lines: literals, comments and preprocessor
# directives cannot declare public names. Keep offsets for useful diagnostics.
$pattern = @'
(?x)
    (?m: ^ [\t ]* \# (?: [^\r\n\\] | \\ \r?\n | \\ [^\r\n] )* )
  | // [^\r\n]*
  | /\* [\s\S]*? \*/
  | (?:u8|u|U|L)? R" (?<delimiter>[^\s()\\]{0,16}) \( [\s\S]*? \) \k<delimiter> "
  | (?:u8|u|U|L)? " (?: \\ [\s\S] | [^"\\] )* "
  | (?:u8|u|U|L)? ' (?: \\ [\s\S] | [^'\\] )* '
  | [a-zA-Z_] [a-zA-Z_0-9]*
  | ::
  | ->
  | [^\s]
'@
$problems = [Collections.Generic.List[string]]::new()
foreach ($header in $Headers) {
    $source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $header))
    $macros = @{}
    foreach ($definition in [regex]::Matches($source, '(?m)^\s*#\s*define\s+(\w+)')) {
        $macros[$definition.Groups[1].Value] = $true
    }
    $tokens = @([regex]::Matches($source, $pattern) | Where-Object {
        $_.Value -match '^(?:[a-zA-Z_][a-zA-Z_0-9]*|::|->|[{}:;<>.()=,*&])$'
    })
    $scopes = [Collections.Generic.List[object]]::new()
    $privateMembers = @{}
    $candidates = [Collections.Generic.List[object]]::new()
    $pendingClass = $null
    $nextClass = 0
    $standard = $false
    for ($i = 0; $i -lt $tokens.Count; ++$i) {
        $token = $tokens[$i].Value
        if ($token -eq 'class' -or $token -eq 'struct') {
            # enum class is not a class access scope.
            if ($i -eq 0 -or $tokens[$i - 1].Value -ne 'enum') {
                $pendingClass = if ($token -eq 'class') { 'private' } else { 'public' }
            }
        } elseif ($token -eq '{') {
            $classId = if ($pendingClass) { $nextClass += 1; $nextClass } else { 0 }
            $scopes.Add(@{ Access = $pendingClass; ClassId = $classId })
            $pendingClass = $null
        } elseif ($token -eq '}') {
            if ($scopes.Count) { $scopes.RemoveAt($scopes.Count - 1) }
        } elseif ($token -eq ';' -or $token -eq '>') {
            $pendingClass = $null
        } elseif ($token -in @('public', 'private', 'protected') -and
                  $i + 1 -lt $tokens.Count -and $tokens[$i + 1].Value -eq ':' -and $scopes.Count) {
            $scopes[$scopes.Count - 1].Access = $token
        }
        if ($token -eq 'std') { $standard = $true; continue }
        if ($token -eq '::') { continue }
        $isStandard = $standard -and $i -gt 0 -and $tokens[$i - 1].Value -eq '::'
        if (-not $isStandard) { $standard = $false }
        if ($token -notmatch '_' -or $token -notmatch '^[a-zA-Z_][a-zA-Z_0-9]*$' -or
            $token -cmatch '^[A-Z][A-Z_0-9]*$' -or $macros.ContainsKey($token) -or $isStandard) { continue }
        if ($token -in @('char16_t', 'char32_t', 'wchar_t', 'static_assert', 'static_cast',
                        'const_cast', 'dynamic_cast', 'reinterpret_cast', 'thread_local')) { continue }
        # A member access is an expression, never the name being declared. This
        # also leaves standard container members (value_or, push_back) alone.
        if ($i -gt 0 -and $tokens[$i - 1].Value -in @('.', '->')) { continue }
        $access = $null
        $classId = 0
        for ($scope = $scopes.Count - 1; $scope -ge 0; --$scope) {
            if ($scopes[$scope].ClassId) {
                $access = $scopes[$scope].Access
                $classId = $scopes[$scope].ClassId
                break
            }
        }
        if ($access -in @('private', 'protected') -and $token -match '^[a-z][a-zA-Z0-9]*_$') {
            $privateMembers["${classId}:$token"] = $true
            continue
        }
        $line = 1 + [regex]::Matches($source.Substring(0, $tokens[$i].Index), "`n").Count
        $candidates.Add(@{ Name = $token; Line = $line; ClassId = $classId })
    }
    foreach ($candidate in $candidates) {
        # A public inline method can refer to a private field declared later.
        if ($privateMembers.ContainsKey("$($candidate.ClassId):$($candidate.Name)")) { continue }
        $problems.Add("${header}:$($candidate.Line): snake_case public name '$($candidate.Name)'")
    }
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Output $_ }
    exit 1
}
Write-Output "Public naming check passed ($($Headers.Count) headers)"
