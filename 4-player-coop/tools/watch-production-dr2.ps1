[CmdletBinding()]
param(
    [string]$GameRoot = 'G:\SteamLibrary\steamapps\common\Dead Rising 2',
    [int]$PollMilliseconds = 100,
    [int]$HangMilliseconds = 1500,
    [int]$StartupGraceMilliseconds = 15000,
    [int]$EvidenceMilliseconds = 1200,
    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ($PollMilliseconds -lt 50 -or $PollMilliseconds -gt 1000) { throw 'PollMilliseconds must be 50-1000.' }
if ($HangMilliseconds -lt 500 -or $HangMilliseconds -gt 30000) { throw 'HangMilliseconds must be 500-30000.' }
if ($StartupGraceMilliseconds -lt 0 -or $StartupGraceMilliseconds -gt 120000) { throw 'Invalid startup grace.' }
if ($EvidenceMilliseconds -lt 0 -or $EvidenceMilliseconds -gt 10000) { throw 'Invalid evidence interval.' }

$expectedExe = [IO.Path]::GetFullPath((Join-Path $GameRoot 'deadrising2.exe'))
if (-not (Test-Path -LiteralPath $expectedExe)) { throw "DR2 executable not found: $expectedExe" }

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot '4-player-coop\runs\production-guardian' }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$timelinePath = Join-Path $OutputRoot 'guardian.jsonl'
$monitor = Join-Path $repoRoot 'tools\dr2-crash-dump-monitor\bin\Release\net8.0\DR2CrashDumpMonitor.exe'

if (-not ('Dr2Guardian.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Dr2Guardian {
    public static class Native {
        [DllImport("user32.dll")]
        private static extern short GetAsyncKeyState(int key);
        [DllImport("user32.dll", SetLastError=true)]
        private static extern IntPtr SendMessageTimeout(IntPtr window, uint message, UIntPtr wParam,
            IntPtr lParam, uint flags, uint timeout, out UIntPtr result);

        public static bool EmergencyChordDown() {
            return (GetAsyncKeyState(0x11) & 0x8000) != 0 &&
                   (GetAsyncKeyState(0x10) & 0x8000) != 0 &&
                   (GetAsyncKeyState(0x7B) & 0x8000) != 0;
        }

        public static bool WindowResponds(IntPtr window, uint timeout) {
            if (window == IntPtr.Zero) return true;
            UIntPtr result;
            return SendMessageTimeout(window, 0, UIntPtr.Zero, IntPtr.Zero, 2, timeout, out result) != IntPtr.Zero;
        }
    }
}
'@
}

function Write-GuardianEvent([string]$Event, [hashtable]$Fields = @{}) {
    $entry = [ordered]@{
        captured_at = [DateTimeOffset]::Now.ToString('o')
        event = $Event
    }
    foreach ($key in $Fields.Keys) { $entry[$key] = $Fields[$key] }
    Add-Content -LiteralPath $timelinePath -Value ($entry | ConvertTo-Json -Compress -Depth 4) -Encoding UTF8
}

function Quote-NativeArgument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Get-ExactTarget {
    foreach ($candidate in @(Get-Process -Name deadrising2 -ErrorAction SilentlyContinue)) {
        try {
            if ([string]::Equals([IO.Path]::GetFullPath($candidate.Path), $expectedExe,
                    [StringComparison]::OrdinalIgnoreCase)) { return $candidate }
        } catch {}
    }
    return $null
}

function Test-Identity([Diagnostics.Process]$Process, [long]$StartTicks) {
    try {
        $Process.Refresh()
        return -not $Process.HasExited -and $Process.StartTime.ToUniversalTime().Ticks -eq $StartTicks -and
            [string]::Equals([IO.Path]::GetFullPath($Process.Path), $expectedExe,
                [StringComparison]::OrdinalIgnoreCase)
    } catch { return $false }
}

function Stop-ExactTarget([Diagnostics.Process]$Process, [long]$StartTicks, [string]$Reason) {
    if (-not (Test-Identity $Process $StartTicks)) {
        Write-GuardianEvent 'recovery-aborted-identity-changed' @{ pid=$Process.Id; reason=$Reason }
        return
    }
    Write-GuardianEvent 'recovery-kill' @{ pid=$Process.Id; reason=$Reason }
    Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
}

Write-GuardianEvent 'guardian-started' @{
    executable=$expectedExe; poll_ms=$PollMilliseconds; hang_ms=$HangMilliseconds
    startup_grace_ms=$StartupGraceMilliseconds; emergency_chord='Ctrl+Shift+F12'
}

while ($true) {
    $target = Get-ExactTarget
    if ($null -eq $target) {
        Start-Sleep -Milliseconds $PollMilliseconds
        continue
    }

    $startTicks = $target.StartTime.ToUniversalTime().Ticks
    $observedAt = [Diagnostics.Stopwatch]::StartNew()
    $unresponsiveAt = $null
    $runDir = Join-Path $OutputRoot ('pid-{0}-{1:yyyyMMdd-HHmmss}' -f $target.Id, $target.StartTime)
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
    Write-GuardianEvent 'target-acquired' @{ pid=$target.Id; start_ticks=$startTicks; run_dir=$runDir }

    $crashMonitor = $null
    if (Test-Path -LiteralPath $monitor) {
        $arguments = @('--pid', $target.Id, '--out-dir', (Quote-NativeArgument $runDir), '--first-chance',
            '0xc0000409,0xc0000005,0xe06d7363', '--max-dumps', '6')
        $crashMonitor = Start-Process -FilePath $monitor -ArgumentList $arguments -WindowStyle Hidden -PassThru
        Write-GuardianEvent 'crash-monitor-started' @{ pid=$target.Id; monitor_pid=$crashMonitor.Id }
    } else {
        Write-GuardianEvent 'crash-monitor-missing' @{ pid=$target.Id; path=$monitor }
    }

    while (Test-Identity $target $startTicks) {
        if ([Dr2Guardian.Native]::EmergencyChordDown()) {
            Write-GuardianEvent 'emergency-chord' @{ pid=$target.Id }
            Stop-ExactTarget $target $startTicks 'emergency-chord'
            Start-Sleep -Milliseconds 500
            break
        }

        $target.Refresh()
        $windowResponsive = [Dr2Guardian.Native]::WindowResponds($target.MainWindowHandle, 100)
        $responsive = $target.Responding -and $windowResponsive
        if ($observedAt.ElapsedMilliseconds -lt $StartupGraceMilliseconds) { $responsive = $true }

        if ($responsive) {
            $unresponsiveAt = $null
        } elseif ($null -eq $unresponsiveAt) {
            $unresponsiveAt = [Diagnostics.Stopwatch]::StartNew()
            Write-GuardianEvent 'hang-suspected' @{
                pid=$target.Id; process_responding=$target.Responding; window_responding=$windowResponsive
            }
        } elseif ($unresponsiveAt.ElapsedMilliseconds -ge $HangMilliseconds) {
            $stamp = [DateTime]::Now.ToString('yyyyMMdd_HHmmss_fff')
            $dumpPath = Join-Path $runDir "hang_$stamp.dmp"
            $threadsPath = Join-Path $runDir "hang_$stamp.threads.json"
            Write-GuardianEvent 'hang-confirmed' @{ pid=$target.Id; duration_ms=$unresponsiveAt.ElapsedMilliseconds }
            if (Test-Path -LiteralPath $monitor) {
                $dumpWriter = Start-Process -FilePath $monitor -ArgumentList @(
                    '--pid', $target.Id, '--dump-path', (Quote-NativeArgument $dumpPath),
                    '--threads-path', (Quote-NativeArgument $threadsPath)
                ) -WindowStyle Hidden -PassThru
                if (-not $dumpWriter.WaitForExit($EvidenceMilliseconds)) {
                    Stop-Process -Id $dumpWriter.Id -Force -ErrorAction SilentlyContinue
                    Write-GuardianEvent 'hang-evidence-timeout' @{ pid=$target.Id; timeout_ms=$EvidenceMilliseconds }
                } else {
                    Write-GuardianEvent 'hang-evidence-finished' @{
                        pid=$target.Id; exit_code=$dumpWriter.ExitCode; dump=$dumpPath; threads=$threadsPath
                    }
                }
            }
            Stop-ExactTarget $target $startTicks 'sustained-window-hang'
            break
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    }

    if ($null -ne $crashMonitor) {
        try {
            $crashMonitor.Refresh()
            if (-not $crashMonitor.HasExited) { $crashMonitor.WaitForExit(3000) | Out-Null }
            if (-not $crashMonitor.HasExited) { Stop-Process -Id $crashMonitor.Id -Force -ErrorAction SilentlyContinue }
        } catch {}
    }
    Write-GuardianEvent 'target-released' @{ pid=$target.Id }
}
