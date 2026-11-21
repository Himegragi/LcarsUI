Open-ItemsPage
Scroll-ItemsPageToBottom
Open-AddDialog
Click-RealInputElement 'items.dialog.note'
[IsoduoRealInput]::Type($window, 'first line')
[IsoduoRealInput]::Tap($window, 0x0D)
[IsoduoRealInput]::Type($window, 'second line')
[IsoduoRealInput]::Tap($window, 0x0D)
[IsoduoRealInput]::Type($window, 'third line')
$null = Wait-NoteValue "first line`nsecond line`nthird line"
$rects = @(Get-NoteLineRectangles)
if ($rects.Count -ne 3) { throw "Expected three note lines, got $($rects.Count)" }
$line = $rects[1]
[IsoduoRealInput]::Click($window, ($line.X + $line.Width / 2), ($line.Y + $line.Height / 2), $true)
$null = Wait-RealInputElement 'items.dialog.note/menu'
Save-RealInputScreenshot 'context-menu'
[IsoduoRealInput]::Tap($window, 0x1B)
[IsoduoRealInput]::Type($window, '#')
Start-Sleep -Milliseconds 250
$lines = (Get-NoteValue) -split '\r?\n'
if ($lines.Count -ne 3 -or $lines[0] -cne 'first line' -or $lines[2] -cne 'third line' -or
    $lines[1] -cnotmatch '^.+#.+$' -or $lines[1].Replace('#', '') -cne 'second line' -or
    ($lines[1] -split '#').Count -ne 2) {
    throw "Marker did not land inside line 2. Note: $($lines -join ' / ')"
}
"Note: $($lines -join ' / ')"
