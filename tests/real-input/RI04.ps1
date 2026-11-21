# The demo cannot place the dropdown over the page scrollbar. The modal panel keeps
# a 20 DIP margin (src/layout.cpp), which contains the page scrollbar. The overlap
# rule is covered by anOpenDialogBlocksThePageScrollbarUnderItsComponents in
# tests/pointer_routing.cpp.
$bounds = [IsoduoRealInput]::WindowRect($window)
if ($bounds.Width -ne 660) { throw "Expected 660 px window width, got $($bounds.Width)" }
Open-ItemsPage
Scroll-ItemsPageToBottom
$point = [IsoduoRealInput]::ThumbAtEnd($window, $true)
$before = (Wait-RealInputElement 'items.count').Current.BoundingRectangle.Y
Open-AddDialog
$dropdown = Wait-RealInputElement 'items.dialog.note.color'
$pattern = $dropdown.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
$initial = $pattern.Current.ExpandCollapseState
Click-RealInputElement 'items.dialog.note.color'
$expanded = $pattern.Current.ExpandCollapseState
if ($initial -ne 'Collapsed' -or $expanded -ne 'Expanded') {
    throw "Expected Collapsed -> Expanded. Dropdown state: $initial -> $expanded"
}
Save-RealInputScreenshot 'dropdown'
[IsoduoRealInput]::Tap($window, 0x1B)
if ($pattern.Current.ExpandCollapseState -ne 'Collapsed') { throw 'Dropdown did not close before the drag' }
$null = Wait-RealInputElement 'items.dialog'
$during = (Wait-RealInputElement 'items.count').Current.BoundingRectangle.Y
Save-RealInputScreenshot 'before-drag'
[IsoduoRealInput]::Guard($window)
[IsoduoRealInput]::Move($point[0], $point[1])
[IsoduoRealInput]::Mouse(2, 0)
try {
    foreach ($distance in @(40, 80, 120)) {
        [IsoduoRealInput]::Guard($window)
        [IsoduoRealInput]::Move($point[0], ($point[1] - $distance))
        Start-Sleep -Milliseconds 120
    }
} finally {
    [IsoduoRealInput]::Mouse(4, 0)
}
$null = Wait-RealInputElement 'items.dialog'
$after = (Wait-RealInputElement 'items.count').Current.BoundingRectangle.Y
if ($before -ne $during -or $during -ne $after) {
    throw "Modal page moved. items.count Y: $before -> $during -> $after"
}
"$initial -> $expanded; items.count Y: $before -> $during -> $after"
