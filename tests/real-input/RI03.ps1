Open-ItemsPage
$topY = (Wait-RealInputElement 'T010').Current.BoundingRectangle.Y
Scroll-ItemsPageToBottom
$point = [IsoduoRealInput]::ThumbAtEnd($window, $true)
$client = [IsoduoRealInput]::ClientRect($window)
$ys = [System.Collections.Generic.List[double]]::new()
$ys.Add((Wait-RealInputElement 'T010').Current.BoundingRectangle.Y)
Save-RealInputScreenshot 'before-drag'
[IsoduoRealInput]::Guard($window)
[IsoduoRealInput]::Move($point[0], $point[1])
[IsoduoRealInput]::Mouse(2, 0)
try {
    for ($y = $point[1] - 40; $y -ge $client.Top; $y -= 40) {
        [IsoduoRealInput]::Guard($window)
        [IsoduoRealInput]::Move($point[0], $y)
        Start-Sleep -Milliseconds 120
        $ys.Add((Wait-RealInputElement 'T010').Current.BoundingRectangle.Y)
    }
} finally {
    [IsoduoRealInput]::Mouse(4, 0)
}
$steps = for ($i = 1; $i -lt $ys.Count; ++$i) { $ys[$i] - $ys[$i - 1] }
$motion = @($steps | Where-Object { $_ -gt 0 })
$steady = @($motion | Select-Object -SkipLast 1)
$tolerance = [Math]::Max(2, $steps[0] * 0.1)
$uneven = @($steady | Where-Object { [Math]::Abs($_ - $steps[0]) -gt $tolerance })
if ($steady.Count -lt 2 -or $uneven.Count -gt 0 -or $motion[-1] -gt ($steps[0] + $tolerance) -or
    @($steps | Select-Object -First $motion.Count | Where-Object { $_ -le 0 }).Count -gt 0 -or
    @($steps | Where-Object { $_ -lt 0 }).Count -gt 0 -or
    $steps[-1] -ne 0 -or $ys[-1] -ne $topY) {
    throw "Expected steady motion and a stop at the original top ($topY). T010 Y: $($ys -join ', ')"
}
"T010 Y (40 px drag steps): $($ys -join ', '); original top: $topY"
