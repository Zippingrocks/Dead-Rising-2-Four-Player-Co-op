param(
    # Steps separated by ';'. Each step: "click x y" (window fractions), "wait seconds", "frame name", "key vk" (virtual-key via PostMessage).
    [string]$Steps = "wait 40; frame boot",
    [string]$GameRoot = "C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2",
    [switch]$KeepRunning
)

# Unattended co-op trace run: boots vanilla DR2 through Steam with [Coop] Trace=1, performs the scripted steps, saves
# frames and the runtime logs to runtime_logs\coop\<time>\, then closes the game and turns tracing off again.
$ErrorActionPreference = "Stop"
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$outDir = Join-Path $workspace ("runtime_logs\coop\trace_{0:yyyyMMdd_HHmmss}" -f (Get-Date))
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$iniPath = Join-Path $GameRoot "case_zero_campaign.ini"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class CoopInput {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, IntPtr e);
  public static void Click(int x, int y) {
    SetCursorPos(x, y); System.Threading.Thread.Sleep(150);
    mouse_event(0x0002, 0, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(80); mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);
  }
}
"@
function Get-Game { Get-Process deadrising2 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
function Note($text) { $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $text; Add-Content (Join-Path $outDir "events.txt") $line; Write-Output $line }
function Save-Frame($game, $name) {
    $r = New-Object CoopInput+RECT; [CoopInput]::GetWindowRect($game.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T; if ($w -le 0 -or $h -le 0) { return }
    $bmp = New-Object Drawing.Bitmap $w, $h; $g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
    $scaled = New-Object Drawing.Bitmap $bmp, ([int]($w / 2)), ([int]($h / 2)); $scaled.Save((Join-Path $outDir "$name.png"), [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose(); $scaled.Dispose()
}
function Set-Trace($on) {
    $ini = Get-Content $iniPath -Raw
    $ini = $ini -replace '(?ms)^\[Coop\]\r?\n(?:[^\[]*)', ''
    if ($on) { $ini = $ini.TrimEnd() + "`r`n[Coop]`r`nTrace=1`r`n" }
    $ini = $ini -replace '(?m)^Active=.*$', 'Active=vanilla_dr2'
    [IO.File]::WriteAllText($iniPath, $ini)
}

if (Get-Game) { throw "deadrising2.exe is already running" }
Set-Trace $true
foreach ($name in "coop_net.log", "case_zero_runtime.log") { $p = Join-Path $GameRoot $name; if (Test-Path $p) { Remove-Item -LiteralPath $p -Force } }
try {
    [CoopInput]::SetCursorPos(5, 5) | Out-Null
    Start-Process "steam://rungameid/45740"; Note "launched vanilla DR2 with co-op tracing"
    $game = $null
    for ($i = 0; $i -lt 60 -and -not $game; $i++) { Start-Sleep -Seconds 2; $game = Get-Game }
    if (-not $game) { throw "game window never appeared" }
    foreach ($step in ($Steps -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        $parts = $step -split '\s+'
        $game = Get-Game
        if (-not $game) { Note "game exited before step '$step'"; break }
        switch ($parts[0]) {
            "wait" { Start-Sleep -Seconds ([double]$parts[1]) }
            "frame" { Save-Frame $game $parts[1] }
            "click" {
                [CoopInput]::SetForegroundWindow($game.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 300
                $r = New-Object CoopInput+RECT; [CoopInput]::GetWindowRect($game.MainWindowHandle, [ref]$r) | Out-Null
                [CoopInput]::Click($r.L + [int](($r.R - $r.L) * [double]$parts[1]), $r.T + [int](($r.B - $r.T) * [double]$parts[2]))
                [CoopInput]::SetCursorPos(5, 5) | Out-Null
            }
        }
        Note "step: $step"
    }
} finally {
    if (-not $KeepRunning) {
        Get-Process deadrising2 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 3
        Set-Trace $false
    }
    foreach ($name in "coop_net.log", "case_zero_runtime.log") { $p = Join-Path $GameRoot $name; if (Test-Path $p) { Copy-Item $p $outDir } }
    Note "evidence in $outDir"
}
