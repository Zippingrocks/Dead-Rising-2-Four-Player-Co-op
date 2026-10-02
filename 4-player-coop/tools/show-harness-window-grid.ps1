[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
if ($OwnedPids.Count -ne 4 -or @($OwnedPids | Select-Object -Unique).Count -ne 4) {
    throw 'Four distinct owned PIDs in session order are required'
}
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run must belong to this workspace harness'
}
$samples = @(Import-Csv -LiteralPath (Join-Path $run 'resources.csv') | Select-Object -Last 4)
if ($samples.Count -ne 4 -or @($samples | Where-Object {
        $_.Alive -ne 'true' -or ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($_.Timestamp)).TotalSeconds -gt 10
    }).Count) {
    throw 'Fresh live samples for all four owned processes are required'
}

$processes = @()
foreach ($instance in 0..3) {
    $row = @($samples | Where-Object { [int]$_.Instance -eq $instance })
    $description = Get-CimInstance Win32_Process -Filter "ProcessId=$($OwnedPids[$instance])"
    if ($row.Count -ne 1 -or [int]$row[0].Pid -ne $OwnedPids[$instance] -or $null -eq $description -or
        $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
        $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$instance(?:\s|$)" -or
        $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)') {
        throw "Owned process validation failed for instance $instance"
    }
    $process = Get-Process -Id $OwnedPids[$instance]
    $process.Refresh()
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw "No window found for instance $instance" }
    $processes += $process
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HarnessWindowGrid {
  [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
  [DllImport("user32.dll", EntryPoint="SetWindowLongPtrW")] public static extern IntPtr SetWindowLongPtr(IntPtr window, int index, IntPtr value);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr window);
  [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr window, IntPtr update, IntPtr region, uint flags);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr window, string text);
}
'@

$working = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$gap = 12
$height = [int](($working.Height - (3 * $gap)) / 2)
$width = [int]([Math]::Floor($height * 16 / 9))
if ((2 * $width) + (3 * $gap) -gt $working.Width) {
    $width = [int](($working.Width - (3 * $gap)) / 2)
    $height = [int]([Math]::Floor($width * 9 / 16))
}

$GWL_STYLE = -16
$GWL_EXSTYLE = -20
$WS_POPUP = 0x80000000L
$WS_OVERLAPPEDWINDOW = 0x00CF0000L
$WS_EX_TOOLWINDOW = 0x80L
$WS_EX_APPWINDOW = 0x40000L
$WS_EX_NOACTIVATE = 0x08000000L
$SWP_SHOWWINDOW_FRAMECHANGED = 0x0060
$HWND_TOPMOST = [IntPtr](-1)

$layout = @()
foreach ($instance in 0..3) {
    $window = $processes[$instance].MainWindowHandle
    $style = [HarnessWindowGrid]::GetWindowLongPtr($window, $GWL_STYLE).ToInt64()
    $style = ($style -band (-bnot $WS_POPUP)) -bor $WS_OVERLAPPEDWINDOW
    [void][HarnessWindowGrid]::SetWindowLongPtr($window, $GWL_STYLE, [IntPtr]$style)
    $exStyle = [HarnessWindowGrid]::GetWindowLongPtr($window, $GWL_EXSTYLE).ToInt64()
    $exStyle = ($exStyle -band (-bnot ($WS_EX_TOOLWINDOW -bor $WS_EX_NOACTIVATE))) -bor $WS_EX_APPWINDOW
    [void][HarnessWindowGrid]::SetWindowLongPtr($window, $GWL_EXSTYLE, [IntPtr]$exStyle)
    [void][HarnessWindowGrid]::SetWindowText($window, "DR2 Four-Player Test - Player $($instance + 1)")
    $column = $instance % 2
    $row = [Math]::Floor($instance / 2)
    $x = $working.X + $gap + ($column * ($width + $gap))
    $y = $working.Y + $gap + ($row * ($height + $gap))
    [void][HarnessWindowGrid]::ShowWindow($window, 9)
    if (-not [HarnessWindowGrid]::SetWindowPos($window, $HWND_TOPMOST, $x, $y, $width, $height,
            $SWP_SHOWWINDOW_FRAMECHANGED)) {
        throw "Could not position instance $instance"
    }
    [void][HarnessWindowGrid]::RedrawWindow($window, [IntPtr]::Zero, [IntPtr]::Zero, 0x0085)
    $layout += [ordered]@{Instance=$instance;Pid=$OwnedPids[$instance];Window=('0x{0:X}' -f $window.ToInt64());
        X=$x;Y=$y;Width=$width;Height=$height}
}
[void][HarnessWindowGrid]::BringWindowToTop($processes[0].MainWindowHandle)
[void][HarnessWindowGrid]::SetForegroundWindow($processes[0].MainWindowHandle)

$record = [ordered]@{event='four-window-grid';time=[DateTimeOffset]::Now.ToString('o');topmost=$true;layout=$layout;
    controller='Native controller remains hardware-owned; Windows focus selects the foreground DirectInput client.'}
[IO.File]::AppendAllText((Join-Path $run 'interactive-window-grid.jsonl'),
    ($record | ConvertTo-Json -Compress -Depth 5) + "`n")
$record | ConvertTo-Json -Depth 5
