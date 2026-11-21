Open-ItemsPage
Scroll-ItemsPageToBottom
Open-AddDialog
Click-RealInputElement 'items.dialog.note'
$thread = [IsoduoRealInput]::GetWindowThreadProcessId($window, [IntPtr]::Zero)
$original = [IsoduoRealInput]::GetKeyboardLayout($thread)
$bopomofoLayout = [IntPtr]0x04040404
if (-not [IsoduoRealInput]::PostMessageW($window, 0x0050, [IntPtr]::Zero, $bopomofoLayout)) {
    throw 'Cannot request Bopomofo keyboard layout for the demo window'
}
try {
    for ($i = 0; $i -lt 20; ++$i) {
        $actual = [IsoduoRealInput]::GetKeyboardLayout($thread)
        if ($actual -eq $bopomofoLayout) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($actual -ne $bopomofoLayout) { throw ('Expected Bopomofo layout 0x04040404, got 0x{0:X8}' -f $actual.ToInt64()) }
    for ($i = 0; $i -lt 12; ++$i) {
        foreach ($key in @(0x53, 0x55, 0x33, 0x43, 0x4C, 0x33)) { [IsoduoRealInput]::Tap($window, $key) }
    }
    Start-Sleep -Milliseconds 200
    Save-RealInputScreenshot 'composition'
    [IsoduoRealInput]::Tap($window, 0x28)
    Start-Sleep -Milliseconds 250
    Save-RealInputScreenshot 'candidates'
    [IsoduoRealInput]::Tap($window, 0x31)
    [IsoduoRealInput]::Tap($window, 0x0D)
    $expected = ([string][char]0x4F60 + [char]0x597D) * 12
    $value = Wait-NoteValue $expected
    $lineCount = @(Get-NoteLineRectangles).Count
    if ($lineCount -lt 2) { throw "Expected at least two visual lines, got $lineCount" }
    "HKL 0x04040404, $($value.Length) committed characters, $lineCount line rectangles: $value"
} finally {
    if ($original -ne $bopomofoLayout) {
        [IsoduoRealInput]::PostMessageW($window, 0x0050, [IntPtr]::Zero, $original) | Out-Null
    }
}
