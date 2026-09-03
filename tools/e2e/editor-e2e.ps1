#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameRoot = 'D:\IGI1',
    [string]$EditorPath = '',
    [string]$ScenarioPath = '',
    [string]$ScenarioName = '',
    [string]$ArtifactsRoot = '',
    [switch]$ValidateOnly,
    [switch]$AllowGameDataMutation,
    [switch]$KeepEditorOpen
)

$ErrorActionPreference = 'Stop'
$script:SupportedSteps = @(
    'launch_editor', 'wait_for_window', 'wait_for_log', 'assert_process',
    'key', 'key_hold', 'click', 'type_text', 'wait', 'screenshot',
    'assert_screenshot_region', 'assert_screenshot_color_ratio',
    'assert_screenshot_difference', 'assert_log', 'mark_log', 'assert_file',
    'assert_path', 'assert_cursor_visible', 'assert_cursor_hidden', 'close_editor'
)

function Fail([string]$Message) {
    throw [System.InvalidOperationException]::new($Message)
}
function Get-FullPath([string]$Path) { return [System.IO.Path]::GetFullPath($Path) }
function Assert-UnderRoot([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Fail "$Label - $fullPath"
    }
    return $fullPath
}
function Get-Property($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}
function Assert-Integer($Value, [string]$Name, [int]$Minimum, [int]$Maximum) {
    if ($null -eq $Value -or $Value -is [bool] -or [int]$Value -ne $Value) { Fail "$Name must be an integer." }
    if ([int]$Value -lt $Minimum -or [int]$Value -gt $Maximum) { Fail "$Name must be between $Minimum and $Maximum." }
}
function Assert-Region($Region, [string]$Name) {
    if ($null -eq $Region -or @($Region).Count -ne 4) { Fail "$Name must be [x,y,width,height]." }
    for ($i = 0; $i -lt 4; $i++) { if ([int]$Region[$i] -lt 0) { Fail "$Name contains a negative value." } }
    if ([int]$Region[2] -le 0 -or [int]$Region[3] -le 0) { Fail "$Name width and height must be positive." }
}
function Validate-Step($Step, [string]$ScenarioName, [int]$Index) {
    $id = [string](Get-Property $Step 'id')
    $type = [string](Get-Property $Step 'type')
    if ([string]::IsNullOrWhiteSpace($id)) { Fail "Scenario '$ScenarioName' step $Index is missing id." }
    if ([string]::IsNullOrWhiteSpace($type) -or $script:SupportedSteps -notcontains $type) { Fail "Scenario '$ScenarioName' step '$id' has unsupported type '$type'." }
    switch ($type) {
        'launch_editor' { Assert-Integer (Get-Property $Step 'level') "$ScenarioName/$id level" 1 14 }
        'wait_for_window' { if ($null -ne (Get-Property $Step 'timeoutSeconds')) { Assert-Integer (Get-Property $Step 'timeoutSeconds') "$ScenarioName/$id timeoutSeconds" 1 300 } }
        'wait_for_log' { if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'pattern'))) { Fail "$ScenarioName/$id requires pattern." } }
        'key' { if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'key'))) { Fail "$ScenarioName/$id requires key." } }
        'key_hold' {
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'key'))) { Fail "$ScenarioName/$id requires key." }
            if ($null -eq (Get-Property $Step 'seconds') -or [double](Get-Property $Step 'seconds') -le 0 -or [double](Get-Property $Step 'seconds') -gt 300) { Fail "$ScenarioName/$id seconds must be greater than 0 and no more than 300." }
        }
        'click' {
            Assert-Integer (Get-Property $Step 'x') "$ScenarioName/$id x" 0 10000
            Assert-Integer (Get-Property $Step 'y') "$ScenarioName/$id y" 0 10000
            $button = [string]$(if ($null -ne (Get-Property $Step 'button')) { Get-Property $Step 'button' } else { 'left' })
            if (@('left','right') -notcontains $button.ToLowerInvariant()) { Fail "$ScenarioName/$id button must be left or right." }
        }
        'type_text' { if ($null -eq (Get-Property $Step 'text')) { Fail "$ScenarioName/$id requires text." } }
        'wait' {
            if ($null -eq (Get-Property $Step 'seconds')) { Fail "$ScenarioName/$id requires seconds." }
            if ([double](Get-Property $Step 'seconds') -lt 0 -or [double](Get-Property $Step 'seconds') -gt 300) { Fail "$ScenarioName/$id seconds out of range." }
        }
        'screenshot' {
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'name'))) { Fail "$ScenarioName/$id requires name." }
            if ($null -ne (Get-Property $Step 'region')) { Assert-Region (Get-Property $Step 'region') "$ScenarioName/$id region" }
        }
        'assert_screenshot_region' {
            Assert-Region (Get-Property $Step 'region') "$ScenarioName/$id region"
            $hasLimit = $false
            foreach ($name in @('minNonBlackRatio','minUniqueRatio','minMeanLuma','maxMeanLuma')) {
                if ($null -ne (Get-Property $Step $name)) { $hasLimit = $true }
            }
            if (-not $hasLimit) { Fail "$ScenarioName/$id requires an image metric." }
        }
        'assert_screenshot_color_ratio' {
            Assert-Region (Get-Property $Step 'region') "$ScenarioName/$id region"
            $rgb = Get-Property $Step 'rgb'
            if ($null -eq $rgb -or @($rgb).Count -ne 3) { Fail "$ScenarioName/$id rgb must be [r,g,b]." }
            foreach ($channel in @($rgb)) { Assert-Integer $channel "$ScenarioName/$id rgb" 0 255 }
            if ($null -eq (Get-Property $Step 'minRatio') -and $null -eq (Get-Property $Step 'maxRatio')) { Fail "$ScenarioName/$id requires minRatio or maxRatio." }
            if ($null -ne (Get-Property $Step 'tolerance')) { Assert-Integer (Get-Property $Step 'tolerance') "$ScenarioName/$id tolerance" 0 255 }
        }
        'assert_screenshot_difference' {
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'from')) -or [string]::IsNullOrWhiteSpace([string](Get-Property $Step 'to'))) { Fail "$ScenarioName/$id requires from and to screenshot names." }
            Assert-Region (Get-Property $Step 'region') "$ScenarioName/$id region"
            $hasLimit = $false
            foreach ($name in @('minChangedRatio','maxChangedRatio','minMeanAbsDifference','maxMeanAbsDifference')) {
                if ($null -ne (Get-Property $Step $name)) { $hasLimit = $true }
            }
            if (-not $hasLimit) { Fail "$ScenarioName/$id requires an image difference metric." }
            if ($null -ne (Get-Property $Step 'changedPixelThreshold')) { Assert-Integer (Get-Property $Step 'changedPixelThreshold') "$ScenarioName/$id changedPixelThreshold" 0 255 }
        }
        'assert_log' {
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'pattern'))) { Fail "$ScenarioName/$id requires pattern." }
            if ($null -ne (Get-Property $Step 'timeoutSeconds')) { Assert-Integer (Get-Property $Step 'timeoutSeconds') "$ScenarioName/$id timeoutSeconds" 1 300 }
        }
        'assert_file' {
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'path'))) { Fail "$ScenarioName/$id requires path." }
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'pattern'))) { Fail "$ScenarioName/$id requires pattern." }
            if ($null -ne (Get-Property $Step 'timeoutSeconds')) { Assert-Integer (Get-Property $Step 'timeoutSeconds') "$ScenarioName/$id timeoutSeconds" 1 300 }
        }
        'assert_path' {
            if ([string]::IsNullOrWhiteSpace([string](Get-Property $Step 'path'))) { Fail "$ScenarioName/$id requires path." }
            $kind = [string]$(if ($null -ne (Get-Property $Step 'kind')) { Get-Property $Step 'kind' } else { 'file' })
            if (@('file','directory') -notcontains $kind.ToLowerInvariant()) { Fail "$ScenarioName/$id kind must be file or directory." }
            if ($null -ne (Get-Property $Step 'minBytes')) { Assert-Integer (Get-Property $Step 'minBytes') "$ScenarioName/$id minBytes" 0 2147483647 }
            if ($null -ne (Get-Property $Step 'timeoutSeconds')) { Assert-Integer (Get-Property $Step 'timeoutSeconds') "$ScenarioName/$id timeoutSeconds" 1 300 }
        }
        'assert_cursor_visible' { }
        'assert_cursor_hidden' { }
    }
}
function Validate-Manifest($Manifest) {
    if ($null -eq $Manifest) { Fail 'Manifest is empty.' }
    $scenarios = Get-Property $Manifest 'scenarios'
    if ($null -eq $scenarios -or @($scenarios).Count -eq 0) { Fail 'Manifest requires a non-empty scenarios array.' }
    $scenarioNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($scenario in @($scenarios)) {
        $name = [string](Get-Property $scenario 'name')
        if ([string]::IsNullOrWhiteSpace($name)) { Fail 'Every scenario requires name.' }
        if (-not $scenarioNames.Add($name)) { Fail "Duplicate scenario name '$name'." }
        Assert-Integer (Get-Property $scenario 'level') "$name level" 1 14
        if ($null -ne (Get-Property $scenario 'inputScale')) {
            $inputScale = [double](Get-Property $scenario 'inputScale')
            if ($inputScale -le 0 -or $inputScale -gt 2) { Fail "Scenario '$name' inputScale must be greater than 0 and no more than 2." }
        }
        $requiresMutation = [bool](Get-Property $scenario 'requiresMutation')
        $steps = Get-Property $scenario 'steps'
        if ($null -eq $steps -or @($steps).Count -eq 0) { Fail "Scenario '$name' requires steps." }
        $stepIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $index = 0
        foreach ($step in @($steps)) {
            $id = [string](Get-Property $step 'id')
            if ([string]::IsNullOrWhiteSpace($id)) { Fail "Scenario '$name' step $index is missing id." }
            if (-not $stepIds.Add($id)) { Fail "Scenario '$name' has duplicate step id '$id'." }
            Validate-Step $step $name $index
            $index++
        }
    }
    return @($scenarios)
}

if (-not ('EditorE2E_Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EditorE2E_Native {
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X; public int Y; }
    [StructLayout(LayoutKind.Sequential)] public struct CursorInfo { public int cbSize; public uint flags; public IntPtr hCursor; public Point ptScreenPos; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool GetCursorInfo(ref CursorInfo info);
    public const uint LeftDown = 0x0002;
    public const uint LeftUp = 0x0004;
    public const uint RightDown = 0x0008;
    public const uint RightUp = 0x0010;
    public const uint KeyUp = 0x0002;
    public const int ShowNormal = 5;
    public static void Click(int x, int y, bool right) {
        SetCursorPos(x, y);
        mouse_event(right ? RightDown : LeftDown, 0, 0, 0, UIntPtr.Zero);
        mouse_event(right ? RightUp : LeftUp, 0, 0, 0, UIntPtr.Zero);
    }
    public static void Key(byte key, bool up) { keybd_event(key, 0, up ? KeyUp : 0, UIntPtr.Zero); }
    public static bool CursorVisible() { var info = new CursorInfo(); info.cbSize = Marshal.SizeOf(typeof(CursorInfo)); return GetCursorInfo(ref info) && (info.flags & 1) != 0; }
}
'@
}
function Get-VirtualKey([string]$Name) {
    $name = $Name.ToUpperInvariant()
    $named = @{
        'ESC' = 0x1B; 'ENTER' = 0x0D; 'TAB' = 0x09; 'SPACE' = 0x20; 'BACKSPACE' = 0x08
        'DELETE' = 0x2E; 'UP' = 0x26; 'DOWN' = 0x28; 'LEFT' = 0x25; 'RIGHT' = 0x27
        'PAGEUP' = 0x21; 'PAGEDOWN' = 0x22; 'F2' = 0x71; 'F3' = 0x72; 'F4' = 0x73
        'F8' = 0x77; 'F11' = 0x7A; 'CTRL' = 0x11; 'ALT' = 0x12; 'SHIFT' = 0x10
    }
    if ($named.ContainsKey($name)) { return [byte]$named[$name] }
    if ($name.Length -eq 1) {
        $code = [int][char]$name[0]
        if (($code -ge 0x30 -and $code -le 0x39) -or ($code -ge 0x41 -and $code -le 0x5A)) { return [byte]$code }
    }
    Fail "Unsupported virtual key '$Name'."
}
function Send-Key([string]$KeyName) {
    $parts = $KeyName.ToUpperInvariant().Split('+')
    $modifiers = @()
    for ($i = 0; $i -lt $parts.Length - 1; $i++) { $modifiers += Get-VirtualKey $parts[$i] }
    $key = Get-VirtualKey $parts[$parts.Length - 1]
    foreach ($modifier in $modifiers) { [EditorE2E_Native]::Key($modifier, $false) }
    [EditorE2E_Native]::Key($key, $false)
    # Keep the chord down long enough for GLUT's callback and the editor's
    # GetAsyncKeyState-based configurable binding check to observe it.
    Start-Sleep -Milliseconds 80
    [EditorE2E_Native]::Key($key, $true)
    for ($i = $modifiers.Length - 1; $i -ge 0; $i--) { [EditorE2E_Native]::Key($modifiers[$i], $true) }
}
function Send-KeyForDuration([string]$KeyName, [double]$Seconds) {
    $parts = $KeyName.ToUpperInvariant().Split('+')
    $modifiers = @()
    for ($i = 0; $i -lt $parts.Length - 1; $i++) { $modifiers += Get-VirtualKey $parts[$i] }
    $key = Get-VirtualKey $parts[$parts.Length - 1]
    foreach ($modifier in $modifiers) { [EditorE2E_Native]::Key($modifier, $false) }
    [EditorE2E_Native]::Key($key, $false)
    Start-Sleep -Milliseconds ([int]($Seconds * 1000))
    [EditorE2E_Native]::Key($key, $true)
    for ($i = $modifiers.Length - 1; $i -ge 0; $i--) { [EditorE2E_Native]::Key($modifiers[$i], $true) }
}
function Send-Text([string]$Text) {
    foreach ($character in $Text.ToCharArray()) {
        $encoded = [EditorE2E_Native]::VkKeyScan($character)
        if ($encoded -eq -1) { Fail "Cannot inject character '$character'." }
        $virtualKey = [byte]($encoded -band 0xFF)
        $shiftState = [byte](($encoded -shr 8) -band 0xFF)
        $held = @()
        if (($shiftState -band 1) -ne 0) { $held += [byte]0x10 }
        if (($shiftState -band 2) -ne 0) { $held += [byte]0x11 }
        if (($shiftState -band 4) -ne 0) { $held += [byte]0x12 }
        foreach ($modifier in $held) { [EditorE2E_Native]::Key($modifier, $false) }
        $scan = [byte]([EditorE2E_Native]::MapVirtualKey($virtualKey, 0) -band 0xFF)
        [EditorE2E_Native]::keybd_event($virtualKey, $scan, 0, [UIntPtr]::Zero)
        [EditorE2E_Native]::keybd_event($virtualKey, $scan, [EditorE2E_Native]::KeyUp, [UIntPtr]::Zero)
        for ($i = $held.Count - 1; $i -ge 0; $i--) { [EditorE2E_Native]::Key($held[$i], $true) }
        Start-Sleep -Milliseconds 20
    }
}
function Wait-ForEditor([int]$ProcessId, [int]$TimeoutSeconds = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if ($null -ne $process) {
            try { $process.Refresh() } catch {}
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
                if ($process.SessionId -ne 1) { Fail "Editor PID $Pid is not on interactive Session 1." }
                if ($process.Responding -eq $false) { Fail "Editor PID $Pid is not responding." }
                return $process
            }
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    Fail "Editor PID $ProcessId did not expose a responsive window within $TimeoutSeconds seconds."
}
function Focus-Editor($Process) {
    [void][EditorE2E_Native]::ShowWindow($Process.MainWindowHandle, [EditorE2E_Native]::ShowNormal)
    [void][EditorE2E_Native]::BringWindowToTop($Process.MainWindowHandle)
    [void][EditorE2E_Native]::SetForegroundWindow($Process.MainWindowHandle)
    [void][EditorE2E_Native]::SetFocus($Process.MainWindowHandle)
    Start-Sleep -Milliseconds 120
}
function Capture-Screenshot([string]$Path, $Region) {
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $x = 0; $y = 0; $width = $bounds.Width; $height = $bounds.Height
    if ($null -ne $Region) { $x = [int]$Region[0]; $y = [int]$Region[1]; $width = [int]$Region[2]; $height = [int]$Region[3] }
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try { $graphics.CopyFromScreen($x, $y, 0, 0, [System.Drawing.Size]::new($width, $height)) } finally { $graphics.Dispose() }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
    return [pscustomobject]@{ path = $Path; x = $x; y = $y; width = $width; height = $height }
}
function Get-ImageMetrics([string]$Path, $Region) {
    Add-Type -AssemblyName System.Drawing
    $image = [System.Drawing.Bitmap]::new($Path)
    try {
        $x = [int]$Region[0]; $y = [int]$Region[1]; $w = [int]$Region[2]; $h = [int]$Region[3]
        if ($x + $w -gt $image.Width -or $y + $h -gt $image.Height) { Fail "Image region exceeds screenshot bounds: $Path" }
        $step = [Math]::Max(1, [int]([Math]::Max($w,$h) / 120))
        $samples = 0; $nonBlack = 0; $lumaSum = 0.0
        $colors = [System.Collections.Generic.HashSet[string]]::new()
        for ($py = $y; $py -lt $y + $h; $py += $step) {
            for ($px = $x; $px -lt $x + $w; $px += $step) {
                $pixel = $image.GetPixel($px,$py)
                $luma = (0.2126 * $pixel.R) + (0.7152 * $pixel.G) + (0.0722 * $pixel.B)
                $samples++; $lumaSum += $luma
                if ($luma -gt 18) { $nonBlack++ }
                [void]$colors.Add(('{0:X2}{1:X2}{2:X2}' -f ($pixel.R -shr 4),($pixel.G -shr 4),($pixel.B -shr 4)))
            }
        }
        return [pscustomobject]@{ samples=$samples; nonBlackRatio=$nonBlack/[double]$samples; uniqueRatio=$colors.Count/[double]$samples; meanLuma=$lumaSum/[double]$samples }
    } finally { $image.Dispose() }
}
function Get-ImageColorRatio([string]$Path, $Region, $Rgb, [int]$Tolerance) {
    Add-Type -AssemblyName System.Drawing
    $image = [System.Drawing.Bitmap]::new($Path)
    try {
        $x = [int]$Region[0]; $y = [int]$Region[1]; $w = [int]$Region[2]; $h = [int]$Region[3]
        if ($x + $w -gt $image.Width -or $y + $h -gt $image.Height) { Fail "Image region exceeds screenshot bounds: $Path" }
        $step = [Math]::Max(1, [int]([Math]::Max($w,$h) / 160))
        $samples = 0; $matching = 0
        for ($py = $y; $py -lt $y + $h; $py += $step) {
            for ($px = $x; $px -lt $x + $w; $px += $step) {
                $pixel = $image.GetPixel($px,$py); $samples++
                if ([Math]::Abs($pixel.R - [int]$Rgb[0]) -le $Tolerance -and
                    [Math]::Abs($pixel.G - [int]$Rgb[1]) -le $Tolerance -and
                    [Math]::Abs($pixel.B - [int]$Rgb[2]) -le $Tolerance) { $matching++ }
            }
        }
        return [pscustomobject]@{ samples=$samples; matching=$matching; ratio=$matching/[double]$samples; rgb=@([int]$Rgb[0],[int]$Rgb[1],[int]$Rgb[2]); tolerance=$Tolerance }
    } finally { $image.Dispose() }
}
function Get-ImageDifferenceMetrics([string]$FromPath, [string]$ToPath, $Region, [int]$ChangedPixelThreshold) {
    Add-Type -AssemblyName System.Drawing
    $from = [System.Drawing.Bitmap]::new($FromPath)
    $to = [System.Drawing.Bitmap]::new($ToPath)
    try {
        if ($from.Width -ne $to.Width -or $from.Height -ne $to.Height) { Fail "Compared screenshots have different dimensions: $FromPath, $ToPath" }
        $x = [int]$Region[0]; $y = [int]$Region[1]; $w = [int]$Region[2]; $h = [int]$Region[3]
        if ($x + $w -gt $from.Width -or $y + $h -gt $from.Height) { Fail "Image region exceeds compared screenshot bounds." }
        $step = [Math]::Max(1, [int]([Math]::Max($w,$h) / 160))
        $samples = 0; $changed = 0; $differenceSum = 0.0
        for ($py = $y; $py -lt $y + $h; $py += $step) {
            for ($px = $x; $px -lt $x + $w; $px += $step) {
                $a = $from.GetPixel($px,$py); $b = $to.GetPixel($px,$py)
                $difference = ([Math]::Abs($a.R-$b.R) + [Math]::Abs($a.G-$b.G) + [Math]::Abs($a.B-$b.B)) / 3.0
                $samples++; $differenceSum += $difference
                if ($difference -ge $ChangedPixelThreshold) { $changed++ }
            }
        }
        return [pscustomobject]@{ samples=$samples; changed=$changed; changedRatio=$changed/[double]$samples; meanAbsDifference=$differenceSum/[double]$samples; threshold=$ChangedPixelThreshold }
    } finally { $from.Dispose(); $to.Dispose() }
}
function Get-AppendedLog([string]$LogPath, [long]$Offset) {
    if (-not (Test-Path -LiteralPath $LogPath)) { return '' }
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        try {
            $stream = [System.IO.FileStream]::new($LogPath, [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            try {
                $length = [int]$stream.Length
                $start = [Math]::Min([Math]::Max(0,[int]$Offset), $length)
                if ($start -ge $length) { return '' }
                $stream.Position = $start
                $bytes = New-Object byte[] ($length - $start)
                [void]$stream.Read($bytes, 0, $bytes.Length)
                return [System.Text.Encoding]::UTF8.GetString($bytes)
            } finally { $stream.Dispose() }
        } catch [System.IO.IOException] {
            Start-Sleep -Milliseconds 100
        }
    }
    return ''
}
function Wait-ForPattern([string]$LogPath, [long]$Offset, [string]$Pattern, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = Get-AppendedLog $LogPath $Offset
        if ($text -match $Pattern) { return $text }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}
function Wait-ForFilePattern([string]$Path, [string]$Pattern, [bool]$MustNotMatch, [int]$TimeoutSeconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $text = if (Test-Path -LiteralPath $Path) { Get-Content -LiteralPath $Path -Raw } else { '' }
        $matches = $text -match $Pattern
        if (($MustNotMatch -and -not $matches) -or (-not $MustNotMatch -and $matches)) { return $true }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}
function Close-Editor($Process, [bool]$Force) {
    if ($null -eq $Process) { return $null }
    $current = Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
    if ($null -eq $current) { return [pscustomobject]@{ exited=$true; exitCode=$null; forced=$Force } }
    if (-not $Force) {
        [void]$current.CloseMainWindow()
        if ($current.WaitForExit(5000)) { return [pscustomobject]@{ exited=$true; exitCode=$current.ExitCode; forced=$false } }
    }
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    return [pscustomobject]@{ exited=$false; exitCode=$null; forced=$true }
}
function Invoke-Scenario($Scenario, [string]$Root, [string]$Editor, [string]$OutputRoot) {
    $scenarioDir = Join-Path $OutputRoot ([string]$Scenario.name)
    New-Item -ItemType Directory -Path $scenarioDir -Force | Out-Null
    $logPath = Join-Path $Root 'igi1ed.log'
    $logOffset = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }
    $process = $null; $latestScreenshot = $null; $screenshots = @{}; $steps = @()
    $scenarioStarted = [DateTime]::UtcNow.ToString('o')
    $inputScale = if ($null -ne $Scenario.inputScale) { [double]$Scenario.inputScale } else { 1.0 }
    $scenarioResult = 'PASS'; $scenarioFailure = $null
    try {
        foreach ($step in @($Scenario.steps)) {
            $stepStart = [DateTime]::UtcNow
            $record = [ordered]@{ id=[string]$step.id; type=[string]$step.type; status='PASS'; started=$stepStart.ToString('o') }
            try {
                switch ([string]$step.type) {
                    'launch_editor' {
                        $level = [int]$step.level
                        $logOffset = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }
                        $wmi = [wmiclass]'\\.\root\cimv2:Win32_Process'
                        $command = '"' + $Editor + '" --game-path "' + $Root + '" -level ' + $level
                        $created = $wmi.Create($command, $Root)
                        if ([int]$created.ReturnValue -ne 0) { Fail "WMI launch failed with return code $($created.ReturnValue)." }
                        $process = Wait-ForEditor ([int]$created.ProcessId) 45
                        $record.pid = [int]$created.ProcessId; $record.command = $command
                    }
                    'wait_for_window' { $process = Wait-ForEditor $process.Id ([int]$(if ($step.timeoutSeconds) { $step.timeoutSeconds } else { 45 })) }
                    'assert_process' {
                        $process = Wait-ForEditor $process.Id 5
                        $record.sessionId = $process.SessionId; $record.responding = $process.Responding
                        $record.workingSetMb = [Math]::Round($process.WorkingSet64 / 1MB, 2)
                        if ($record.workingSetMb -lt 30) { Fail "Editor working set is only $($record.workingSetMb) MB." }
                    }
                    'key' { Focus-Editor $process; Send-Key ([string]$step.key) }
                    'key_hold' { Focus-Editor $process; Send-KeyForDuration ([string]$step.key) ([double]$step.seconds) }
                    'click' { Focus-Editor $process; $right = ([string]$(if ($null -ne $step.button) { $step.button } else { 'left' })).ToLowerInvariant() -eq 'right'; [EditorE2E_Native]::Click([int]([double]$step.x * $inputScale),[int]([double]$step.y * $inputScale),$right) }
                    'type_text' { Focus-Editor $process; Send-Text ([string]$step.text) }
                    'wait' { Start-Sleep -Milliseconds ([int]([double]$step.seconds * 1000)) }
                    'wait_for_log' {
                        $found = Wait-ForPattern $logPath $logOffset ([string]$step.pattern) ([int]$(if ($step.timeoutSeconds) { $step.timeoutSeconds } else { 30 }))
                        if ($null -eq $found) { Fail "Pattern not found in appended log: $($step.pattern)" }
                    }
                    'assert_log' {
                        $mustNotMatch = [bool]$(if ($null -ne $step.mustNotMatch) { $step.mustNotMatch } else { $false })
                        $timeout = [int]$(if ($step.timeoutSeconds) { $step.timeoutSeconds } else { 5 })
                        $deadline = [DateTime]::UtcNow.AddSeconds($timeout)
                        $matched = $false
                        do {
                            $text = Get-AppendedLog $logPath $logOffset
                            $matches = $text -match ([string]$step.pattern)
                            if (($mustNotMatch -and -not $matches) -or (-not $mustNotMatch -and $matches)) { $matched = $true; break }
                            Start-Sleep -Milliseconds 250
                        } while ([DateTime]::UtcNow -lt $deadline)
                        if (-not $matched) { Fail "Log assertion failed for pattern '$($step.pattern)'." }
                    }
                    'mark_log' {
                        $logOffset = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }
                        $record.logOffset = $logOffset
                    }
                    'assert_file' {
                        $filePath = Assert-UnderRoot (Join-Path $Root ([string]$step.path)) $Root 'assert_file path'
                        $mustNotMatch = [bool]$(if ($null -ne $step.mustNotMatch) { $step.mustNotMatch } else { $false })
                        $timeout = [int]$(if ($step.timeoutSeconds) { $step.timeoutSeconds } else { 5 })
                        if (-not (Wait-ForFilePattern $filePath ([string]$step.pattern) $mustNotMatch $timeout)) { Fail "File assertion failed for '$($step.path)'." }
                    }
                    'assert_path' {
                        $path = Assert-UnderRoot (Join-Path $Root ([string]$step.path)) $Root 'assert_path path'
                        $kind = [string]$(if ($null -ne $step.kind) { $step.kind } else { 'file' })
                        $minBytes = [int]$(if ($null -ne $step.minBytes) { $step.minBytes } else { 0 })
                        $timeout = [int]$(if ($step.timeoutSeconds) { $step.timeoutSeconds } else { 5 })
                        $deadline = [DateTime]::UtcNow.AddSeconds($timeout); $valid = $false
                        do {
                            if ($kind.ToLowerInvariant() -eq 'directory') {
                                $valid = Test-Path -LiteralPath $path -PathType Container
                            } elseif (Test-Path -LiteralPath $path -PathType Leaf) {
                                $valid = ((Get-Item -LiteralPath $path).Length -ge $minBytes)
                            }
                            if (-not $valid) { Start-Sleep -Milliseconds 100 }
                        } while (-not $valid -and [DateTime]::UtcNow -lt $deadline)
                        if (-not $valid) { Fail "Path assertion failed for '$($step.path)' (kind=$kind, minBytes=$minBytes)." }
                        $record.path = $path; $record.kind = $kind; $record.bytes = if ($kind.ToLowerInvariant() -eq 'file') { (Get-Item -LiteralPath $path).Length } else { $null }
                    }
                    'assert_cursor_visible' {
                        if (-not [EditorE2E_Native]::CursorVisible()) { Fail 'Mouse cursor is not visible.' }
                        $record.cursorVisible = $true
                    }
                    'assert_cursor_hidden' {
                        if ([EditorE2E_Native]::CursorVisible()) { Fail 'Native mouse cursor is still visible.' }
                        $record.cursorVisible = $false
                    }
                    'screenshot' {
                        Focus-Editor $process
                        $file = Join-Path $scenarioDir (([string]$step.name) + '.png')
                        $latestScreenshot = Capture-Screenshot $file $step.region
                        $screenshots[[string]$step.name] = $latestScreenshot
                        $record.screenshot = $file
                    }
                    'assert_screenshot_region' {
                        if ($null -eq $latestScreenshot) { Fail 'Image assertion requires a preceding screenshot step.' }
                        $metrics = Get-ImageMetrics $latestScreenshot.path $step.region; $record.metrics = $metrics
                        if ($null -ne $step.minNonBlackRatio -and $metrics.nonBlackRatio -lt [double]$step.minNonBlackRatio) { Fail "nonBlackRatio $($metrics.nonBlackRatio) < $($step.minNonBlackRatio)." }
                        if ($null -ne $step.minUniqueRatio -and $metrics.uniqueRatio -lt [double]$step.minUniqueRatio) { Fail "uniqueRatio $($metrics.uniqueRatio) < $($step.minUniqueRatio)." }
                        if ($null -ne $step.minMeanLuma -and $metrics.meanLuma -lt [double]$step.minMeanLuma) { Fail "meanLuma $($metrics.meanLuma) < $($step.minMeanLuma)." }
                        if ($null -ne $step.maxMeanLuma -and $metrics.meanLuma -gt [double]$step.maxMeanLuma) { Fail "meanLuma $($metrics.meanLuma) > $($step.maxMeanLuma)." }
                    }
                    'assert_screenshot_color_ratio' {
                        if ($null -eq $latestScreenshot) { Fail 'Color assertion requires a preceding screenshot step.' }
                        $tolerance = [int]$(if ($null -ne $step.tolerance) { $step.tolerance } else { 12 })
                        $metrics = Get-ImageColorRatio $latestScreenshot.path $step.region $step.rgb $tolerance
                        $record.metrics = $metrics
                        if ($null -ne $step.minRatio -and $metrics.ratio -lt [double]$step.minRatio) { Fail "color ratio $($metrics.ratio) < $($step.minRatio)." }
                        if ($null -ne $step.maxRatio -and $metrics.ratio -gt [double]$step.maxRatio) { Fail "color ratio $($metrics.ratio) > $($step.maxRatio)." }
                    }
                    'assert_screenshot_difference' {
                        $fromName = [string]$step.from; $toName = [string]$step.to
                        if (-not $screenshots.ContainsKey($fromName) -or -not $screenshots.ContainsKey($toName)) { Fail "Difference assertion references unknown screenshots '$fromName' or '$toName'." }
                        $threshold = [int]$(if ($null -ne $step.changedPixelThreshold) { $step.changedPixelThreshold } else { 12 })
                        $metrics = Get-ImageDifferenceMetrics $screenshots[$fromName].path $screenshots[$toName].path $step.region $threshold
                        $record.metrics = $metrics
                        if ($null -ne $step.minChangedRatio -and $metrics.changedRatio -lt [double]$step.minChangedRatio) { Fail "changedRatio $($metrics.changedRatio) < $($step.minChangedRatio)." }
                        if ($null -ne $step.maxChangedRatio -and $metrics.changedRatio -gt [double]$step.maxChangedRatio) { Fail "changedRatio $($metrics.changedRatio) > $($step.maxChangedRatio)." }
                        if ($null -ne $step.minMeanAbsDifference -and $metrics.meanAbsDifference -lt [double]$step.minMeanAbsDifference) { Fail "meanAbsDifference $($metrics.meanAbsDifference) < $($step.minMeanAbsDifference)." }
                        if ($null -ne $step.maxMeanAbsDifference -and $metrics.meanAbsDifference -gt [double]$step.maxMeanAbsDifference) { Fail "meanAbsDifference $($metrics.meanAbsDifference) > $($step.maxMeanAbsDifference)." }
                    }
                    'close_editor' {
                        $force = [bool]$(if ($null -ne $step.force) { $step.force } else { $false })
                        $closed = Close-Editor $process $force
                        if ($null -ne $closed) {
                            $record.exited = $closed.exited; $record.exitCode = $closed.exitCode; $record.forced = $closed.forced
                            if (-not $force -and $closed.exited -and $null -ne $closed.exitCode -and $closed.exitCode -ne 0) { Fail "Editor exited with code $($closed.exitCode) during graceful close." }
                        }
                        $process = $null
                    }
                }
            } catch {
                $record.status='FAIL'; $record.error=$_.Exception.Message; throw
            } finally {
                $record.finished=[DateTime]::UtcNow.ToString('o'); $record.durationMs=[int](([DateTime]::UtcNow-$stepStart).TotalMilliseconds); $steps += [pscustomobject]$record
            }
        }
    } catch {
        $scenarioResult='FAIL'; $scenarioFailure=$_.Exception.Message
        if ($null -ne $process) {
            try { Focus-Editor $process; $latestScreenshot = Capture-Screenshot (Join-Path $scenarioDir 'failure.png') $null } catch {}
        }
    } finally { if (-not $KeepEditorOpen) { $discardedCloseResult = Close-Editor $process $true } }
    $result = [ordered]@{ name=[string]$Scenario.name; level=[int]$Scenario.level; status=$scenarioResult; started=$scenarioStarted; finished=[DateTime]::UtcNow.ToString('o'); logPath=$logPath; logOffset=$logOffset; failure=$scenarioFailure; steps=$steps }
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $scenarioDir 'scenario.json') -Encoding UTF8
    return [pscustomobject]$result
}
try {
    $runStarted = [DateTime]::UtcNow.ToString('o')
    if ([string]::IsNullOrWhiteSpace($ScenarioPath)) { $ScenarioPath = Join-Path $PSScriptRoot 'scenarios\editor-regression.json' }
    if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) { $ArtifactsRoot = Join-Path (Join-Path $PSScriptRoot '..\..\\artifacts') ('e2e\' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
    $ScenarioPath = Get-FullPath $ScenarioPath
    if (-not (Test-Path -LiteralPath $ScenarioPath)) { Fail "Scenario manifest not found: $ScenarioPath" }
    $manifest = Get-Content -LiteralPath $ScenarioPath -Raw | ConvertFrom-Json
    $scenarios = Validate-Manifest $manifest
    if ($ValidateOnly) { Write-Host "Validated $(@($scenarios).Count) editor E2E scenarios."; exit 0 }
    if (-not [string]::IsNullOrWhiteSpace($ScenarioName)) {
        $scenarios = @($scenarios | Where-Object { $_.name -like $ScenarioName })
        if ($scenarios.Count -eq 0) { Fail "No scenario matched '$ScenarioName'." }
    }
    foreach ($scenario in @($scenarios)) {
        if ([bool](Get-Property $scenario 'requiresMutation') -and -not $AllowGameDataMutation) {
            Fail "Scenario '$($scenario.name)' requires -AllowGameDataMutation."
        }
    }
    $GameRoot = Get-FullPath $GameRoot
    if (-not (Test-Path -LiteralPath $GameRoot)) { Fail "Game root not found: $GameRoot" }
    if ([string]::IsNullOrWhiteSpace($EditorPath)) { $EditorPath = Join-Path $GameRoot 'igi1ed.exe' }
    $EditorPath = Assert-UnderRoot $EditorPath $GameRoot 'Editor path'
    if (-not (Test-Path -LiteralPath $EditorPath)) { Fail "Editor executable not found: $EditorPath" }
    $ArtifactsRoot = Get-FullPath $ArtifactsRoot
    New-Item -ItemType Directory -Path $ArtifactsRoot -Force | Out-Null
    Copy-Item -LiteralPath $ScenarioPath -Destination (Join-Path $ArtifactsRoot 'scenario-manifest.json') -Force
    $results = @()
    foreach ($scenario in @($scenarios)) { Write-Host "Running $($scenario.name) (Level $($scenario.level))..."; $results += Invoke-Scenario $scenario $GameRoot $EditorPath $ArtifactsRoot }
    $report = [ordered]@{ manifest=$ScenarioPath; gameRoot=$GameRoot; editorPath=$EditorPath; started=$runStarted; finished=[DateTime]::UtcNow.ToString('o'); results=$results; passed=@($results|Where-Object status -eq 'PASS').Count; failed=@($results|Where-Object status -eq 'FAIL').Count }
    $report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $ArtifactsRoot 'run.json') -Encoding UTF8
    $lines = @('# Editor E2E report','',"- Game root: $GameRoot","- Editor: $EditorPath",'','| Scenario | Level | Status | Failure |','|---|---:|---|---|')
    foreach ($item in $results) { $lines += "| $($item.name) | $($item.level) | $($item.status) | $([string]$item.failure -replace '\|','/') |" }
    $lines | Set-Content -LiteralPath (Join-Path $ArtifactsRoot 'run.md') -Encoding UTF8
    if ($report.failed -gt 0) { exit 1 }
    exit 0
} catch { Write-Error $_.Exception.Message; exit 1 }
