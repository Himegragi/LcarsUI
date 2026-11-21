Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing
if (-not ('IsoduoRealInput' -as [type])) {
    Add-Type -Path "$PSScriptRoot/NativeInput.cs" -ReferencedAssemblies System.Drawing
}

function Get-RealInputElement([string]$Id) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($window)
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Wait-RealInputElement([string]$Id) {
    for ($i = 0; $i -lt 50; ++$i) {
        $element = Get-RealInputElement $Id
        if ($element -and -not $element.Current.BoundingRectangle.IsEmpty) { return $element }
        Start-Sleep -Milliseconds 100
    }
    throw "Visible UIA element not found: $Id"
}

function Click-RealInputElement([string]$Id) {
    $rect = (Wait-RealInputElement $Id).Current.BoundingRectangle
    [IsoduoRealInput]::Click($window, ($rect.X + $rect.Width / 2), ($rect.Y + $rect.Height / 2), $false)
}

function Save-RealInputScreenshot([string]$Name) {
    [IsoduoRealInput]::Screenshot($window, (Join-Path $appearanceDir "$case-$Name.png"))
}

function Open-ItemsPage {
    Click-RealInputElement 'items'
    $null = Wait-RealInputElement 'T010'
}

function Scroll-ItemsPageToBottom {
    [IsoduoRealInput]::Guard($window)
    $client = [IsoduoRealInput]::ClientRect($window)
    [IsoduoRealInput]::Move(($client.Right - 5), ($client.Top + $client.Height / 2))
    [IsoduoRealInput]::Mouse(0x0800, (-120 * 40))
    Start-Sleep -Milliseconds 250
    $null = Wait-RealInputElement 'items.add'
}

function Open-AddDialog {
    Click-RealInputElement 'items.add'
    $null = Wait-RealInputElement 'items.dialog'
}

function Get-NoteValue {
    (Wait-RealInputElement 'items.dialog.note').GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern).Current.Value
}

function Wait-NoteValue([string]$Expected) {
    for ($i = 0; $i -lt 20; ++$i) {
        $value = (Get-NoteValue).Replace("`r`n", "`n")
        if ($value -ceq $Expected) { return $value }
        Start-Sleep -Milliseconds 100
    }
    throw "Expected note [$Expected], got [$value]"
}

function Get-NoteLineRectangles {
    (Wait-RealInputElement 'items.dialog.note').GetCurrentPattern(
        [System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetBoundingRectangles()
}

function Test-BopomofoTip {
    $tip = '0404:{B115690A-EA02-48D5-A231-E3578D2FDF80}{B2F9C502-1742-11D4-9790-0080C882687E}'
    $languages = Get-ChildItem -LiteralPath 'HKCU:\Control Panel\International\User Profile' -ErrorAction SilentlyContinue
    foreach ($language in $languages) {
        if ($language.GetValueNames() -contains $tip) { return $true }
    }
    return $false
}
