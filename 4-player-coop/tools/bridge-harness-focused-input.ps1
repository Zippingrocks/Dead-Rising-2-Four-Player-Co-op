[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [ValidateRange(10,3600)][int]$DurationSeconds = 1800,
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

$windows = @()
foreach ($instance in 0..3) {
    $row = @($samples | Where-Object { [int]$_.Instance -eq $instance })
    $description = Get-CimInstance Win32_Process -Filter "ProcessId=$($OwnedPids[$instance])"
    if ($row.Count -ne 1 -or [int]$row[0].Pid -ne $OwnedPids[$instance] -or $null -eq $description -or
        $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
        $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$instance(?:\s|$)" -or
        $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)' -or
        $description.CommandLine -notmatch '(?i)(?:^|\s)-coopprivatemouse(?:\s|$)') {
        throw "Owned process validation failed for instance $instance"
    }
    $process = Get-Process -Id $OwnedPids[$instance]
    $process.Refresh()
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw "No window found for instance $instance" }
    $windows += $process.MainWindowHandle
}

$lockPath = Join-Path $run 'interactive-focus-router.lock'
if (Test-Path -LiteralPath $lockPath) { throw 'A focus input router is already active' }
foreach ($instance in 0..3) {
    if ((Test-Path -LiteralPath (Join-Path $GameRoot "coop_input.$instance.txt")) -or
        (Test-Path -LiteralPath (Join-Path $GameRoot "coop_mouse.$instance.txt"))) {
        throw "Owned input command already exists for instance $instance"
    }
}
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$lockBytes = [Text.Encoding]::ASCII.GetBytes("pid=$PID pids=$($OwnedPids -join ',')")
$lock.Write($lockBytes, 0, $lockBytes.Length)
$lock.Dispose()

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HarnessFocusInput {
  [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct Gamepad {
    public ushort Buttons; public byte LeftTrigger, RightTrigger;
    public short LeftThumbX, LeftThumbY, RightThumbX, RightThumbY;
  }
  [StructLayout(LayoutKind.Sequential)] public struct State { public uint Packet; public Gamepad Pad; }
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
  [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")] static extern uint XInputGetState(uint user, out State state);
  public static bool ReadController(uint user, out Gamepad pad) {
    State state; uint result = XInputGetState(user, out state); pad = state.Pad; return result == 0;
  }
}
'@

$keyMap = [ordered]@{
    0x57=17; 0x41=30; 0x53=31; 0x44=32; 0x45=18; 0x51=16; 0x52=19; 0x46=33
    0x20=57; 0x10=42; 0x11=29; 0x1B=1; 0x09=15; 0x0D=28
    0x26=200; 0x28=208; 0x25=203; 0x27=205
}
$active = -1
$lastKeys = $null
$lastButtons = 0
$lastMouseWrite = [DateTimeOffset]::MinValue
$lastControllerConnected = $null
$sequence = [uint32](([DateTimeOffset]::Now.ToUnixTimeMilliseconds() % 3000000000) + 1)
$logPath = Join-Path $run 'interactive-focus-router.jsonl'

function Keyboard-Path([int]$instance) { Join-Path $GameRoot "coop_input.$instance.txt" }
function Mouse-Path([int]$instance) { Join-Path $GameRoot "coop_mouse.$instance.txt" }
function Invoke-CommandFileRetry {
    param(
        [Parameter(Mandatory)][scriptblock]$Operation,
        [Parameter(Mandatory)][string]$Action,
        [Parameter(Mandatory)][string]$Path
    )
    $lastError = $null
    foreach ($attempt in 1..25) {
        try {
            & $Operation
            return $true
        } catch [IO.IOException] {
            $lastError = $_.Exception.Message
        } catch [UnauthorizedAccessException] {
            $lastError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 20
    }
    try {
        Write-Event @{event='file-contention';time=[DateTimeOffset]::Now.ToString('o');
            action=$Action;path=[IO.Path]::GetFileName($Path);error=$lastError}
    } catch {}
    return $false
}
function Set-CommandText([string]$path, [string]$value) {
    Invoke-CommandFileRetry -Action 'write' -Path $path -Operation {
        [IO.File]::WriteAllText($path, $value, [Text.Encoding]::ASCII)
    }
}
function Remove-CommandFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $true }
    Invoke-CommandFileRetry -Action 'remove' -Path $path -Operation {
        Remove-Item -LiteralPath $path -Force -ErrorAction Stop
    }
}
function Release-Instance([int]$instance) {
    if ($instance -lt 0) { return }
    $keyboardPath = Keyboard-Path $instance
    if (Test-Path -LiteralPath $keyboardPath) {
        [void](Set-CommandText $keyboardPath '')
        [void](Remove-CommandFile $keyboardPath)
    }
    $mousePath = Mouse-Path $instance
    if (Test-Path -LiteralPath $mousePath) {
        $script:sequence++
        [void](Set-CommandText $mousePath ("{0} 0 0 0 0" -f $script:sequence))
        [void](Remove-CommandFile $mousePath)
    }
}
function Write-Event($value) {
    [IO.File]::AppendAllText($logPath, ($value | ConvertTo-Json -Compress -Depth 5) + "`n")
}

try {
    Write-Event @{event='started';time=[DateTimeOffset]::Now.ToString('o');pids=$OwnedPids;
        controls='Click a Player window; keyboard, mouse and Xbox controller route only to that instance; F12 releases'}
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if ((Test-Path -LiteralPath (Join-Path $run 'outcome.json')) -or
            @($OwnedPids | Where-Object { -not (Get-Process -Id $_ -ErrorAction SilentlyContinue) }).Count) { break }
        if (([HarnessFocusInput]::GetAsyncKeyState(0x7B) -band 0x8000) -ne 0) { break }

        $foreground = [HarnessFocusInput]::GetForegroundWindow()
        $selected = [Array]::IndexOf($windows, $foreground)
        if ($selected -ne $active) {
            Release-Instance $active
            $previous = $active
            $active = $selected
            $lastKeys = $null
            $lastButtons = 0
            Write-Event @{event='focus';time=[DateTimeOffset]::Now.ToString('o');from=$previous;active=$active}
        }
        if ($active -lt 0) { Start-Sleep -Milliseconds 30; continue }

        $held = @()
        foreach ($entry in $keyMap.GetEnumerator()) {
            if (([HarnessFocusInput]::GetAsyncKeyState([int]$entry.Key) -band 0x8000) -ne 0) { $held += [int]$entry.Value }
        }

        $pad = [HarnessFocusInput+Gamepad]::new()
        $controllerConnected = [HarnessFocusInput]::ReadController(0, [ref]$pad)
        if ($controllerConnected -ne $lastControllerConnected) {
            Write-Event @{event='controller';time=[DateTimeOffset]::Now.ToString('o');connected=$controllerConnected}
            $lastControllerConnected = $controllerConnected
        }
        if ($controllerConnected) {
            if ($pad.LeftThumbY -gt 9000) { $held += 17 }
            if ($pad.LeftThumbY -lt -9000) { $held += 31 }
            if ($pad.LeftThumbX -lt -9000) { $held += 30 }
            if ($pad.LeftThumbX -gt 9000) { $held += 32 }
            if ($pad.Buttons -band 0x1000) { $held += 57 }  # A: jump
            if ($pad.Buttons -band 0x2000) { $held += 18 }  # B: interact
            if ($pad.Buttons -band 0x0100) { $held += 16 }  # LB: inventory left
            if ($pad.Buttons -band 0x0200) { $held += 19 }  # RB: inventory right
            if ($pad.Buttons -band 0x0010) { $held += 1 }   # Start: pause
            if ($pad.Buttons -band 0x0020) { $held += 15 }  # Back
            if ($pad.Buttons -band 0x0001) { $held += 200 }
            if ($pad.Buttons -band 0x0002) { $held += 208 }
            if ($pad.Buttons -band 0x0004) { $held += 203 }
            if ($pad.Buttons -band 0x0008) { $held += 205 }
        }
        $keys = ($held | Sort-Object -Unique) -join ' '
        if ($keys -ne $lastKeys) {
            $keyboardPath = Keyboard-Path $active
            if ($keys) { [void](Set-CommandText $keyboardPath $keys) }
            elseif (Test-Path -LiteralPath $keyboardPath) {
                [void](Set-CommandText $keyboardPath '')
                [void](Remove-CommandFile $keyboardPath)
            }
            $lastKeys = $keys
        }

        $rectangle = [HarnessFocusInput+Rect]::new()
        $point = [HarnessFocusInput+Point]::new()
        if ([HarnessFocusInput]::GetWindowRect($windows[$active], [ref]$rectangle) -and
            [HarnessFocusInput]::GetCursorPos([ref]$point)) {
            $centerX = [int](($rectangle.L + $rectangle.R) / 2)
            $centerY = [int](($rectangle.T + $rectangle.B) / 2)
            $x = [Math]::Max(-120, [Math]::Min(120, $point.X - $centerX))
            $y = [Math]::Max(-120, [Math]::Min(120, $point.Y - $centerY))
            $buttons = 0
            if (([HarnessFocusInput]::GetAsyncKeyState(0x01) -band 0x8000) -ne 0) { $buttons = $buttons -bor 1 }
            if (([HarnessFocusInput]::GetAsyncKeyState(0x02) -band 0x8000) -ne 0) { $buttons = $buttons -bor 2 }
            if ($controllerConnected) {
                if ([Math]::Abs([int]$pad.RightThumbX) -gt 7000) { $x += [int]($pad.RightThumbX / 1800) }
                if ([Math]::Abs([int]$pad.RightThumbY) -gt 7000) { $y -= [int]($pad.RightThumbY / 1800) }
                if (($pad.Buttons -band 0x4000) -or $pad.RightTrigger -gt 30) { $buttons = $buttons -bor 1 }
                if (($pad.Buttons -band 0x8000) -or $pad.LeftTrigger -gt 30) { $buttons = $buttons -bor 2 }
            }
            $now = [DateTimeOffset]::Now
            if ($x -ne 0 -or $y -ne 0 -or $buttons -ne $lastButtons -or
                ($buttons -ne 0 -and ($now - $lastMouseWrite).TotalMilliseconds -ge 100)) {
                $sequence++
                [void](Set-CommandText (Mouse-Path $active) "$sequence $x $y 0 $buttons")
                $lastButtons = $buttons
                $lastMouseWrite = $now
            }
            [void][HarnessFocusInput]::SetCursorPos($centerX, $centerY)
        }
        Start-Sleep -Milliseconds 30
    }
} finally {
    foreach ($instance in 0..3) {
        try { Release-Instance $instance } catch {}
    }
    [void](Remove-CommandFile $lockPath)
    try { Write-Event @{event='stopped';time=[DateTimeOffset]::Now.ToString('o');active=$active} } catch {}
}
