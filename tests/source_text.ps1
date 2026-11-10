$ErrorActionPreference = 'Stop'
# Catches text damaged by a non-UTF-8 hop (cp1252 shells turn Chinese text into question marks or U+FFFD).
$root = Resolve-Path "$PSScriptRoot/.."
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$codeExtensions = @('.cpp', '.hpp', '.h', '.ps1', '.cmake')
$textExtensions = $codeExtensions + @('.md', '.txt', '.tsv', '.json', '.yml')
$roots = @('src', 'include', 'tests', 'examples', 'cmake', '.github') | ForEach-Object { Join-Path $root $_ }
$files = @(Get-ChildItem -LiteralPath $roots -Recurse -File | Where-Object {
    $_.Extension -in $textExtensions -and $_.FullName -notmatch '[\\/](third_party|build)[\\/]'
}) + @(Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Extension -in $textExtensions })
$problems = [Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    $relative = $file.FullName.Substring($root.Path.Length + 1)
    try { $text = $utf8.GetString([IO.File]::ReadAllBytes($file.FullName)) }
    catch { $problems.Add("${relative}: not valid UTF-8"); continue }
    $lines = $text -split "`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line.Contains([char]0xFFFD)) { $problems.Add("${relative}:$($i + 1): U+FFFD replacement character") }
        if ($file.Extension -in $codeExtensions -and $line -match '"[^"]*\?{3,}') { $problems.Add("${relative}:$($i + 1): three or more question marks inside a string literal; non-ASCII text was probably lost") }
    }
}
if ($problems.Count) { $problems | ForEach-Object { Write-Output $_ }; throw "$($problems.Count) damaged text location(s)" }
"source text check passed ($($files.Count) files)"
