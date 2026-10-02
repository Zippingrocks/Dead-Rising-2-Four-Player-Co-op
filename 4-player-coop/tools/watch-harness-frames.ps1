[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [ValidateRange(5,60)][int]$IntervalSeconds = 15,
    [ValidateRange(10,7200)][int]$DurationSeconds = 3600,
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
$logPath = Join-Path $run 'interactive-frame-watcher.jsonl'
$flags = @(0..3 | ForEach-Object { Join-Path $GameRoot "coop_capture.$_.flag" })
$timer = [Diagnostics.Stopwatch]::StartNew()
$sequence = 0
try {
    [IO.File]::AppendAllText($logPath, (@{event='started';time=[DateTimeOffset]::Now.ToString('o');
        pids=$OwnedPids;interval_seconds=$IntervalSeconds} | ConvertTo-Json -Compress) + "`n")
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $alive = @($OwnedPids | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
        if ($alive.Count -eq 0) { break }
        for ($instance = 0; $instance -lt 4; $instance++) {
            if ((Get-Process -Id $OwnedPids[$instance] -ErrorAction SilentlyContinue) -and
                -not (Test-Path -LiteralPath $flags[$instance])) {
                [IO.File]::WriteAllText($flags[$instance], "interactive-frame-$sequence", [Text.Encoding]::ASCII)
            }
        }
        [IO.File]::AppendAllText($logPath, (@{event='capture-requested';time=[DateTimeOffset]::Now.ToString('o');
            sequence=$sequence;alive=$alive.Count} | ConvertTo-Json -Compress) + "`n")
        $sequence++
        Start-Sleep -Seconds $IntervalSeconds
    }
} finally {
    foreach ($flag in $flags) {
        if (Test-Path -LiteralPath $flag) { Remove-Item -LiteralPath $flag -Force }
    }
    [IO.File]::AppendAllText($logPath, (@{event='stopped';time=[DateTimeOffset]::Now.ToString('o');
        sequences=$sequence} | ConvertTo-Json -Compress) + "`n")
}
