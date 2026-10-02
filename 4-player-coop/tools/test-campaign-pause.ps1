param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidateRange(0, 3)][int]$Instance,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
$count = $OwnedPids.Count
if ($count -notin @(2, 4) -or $Instance -ge $count -or
    @($OwnedPids | Select-Object -Unique).Count -ne $count -or
    @($OwnedPids | Where-Object { $_ -le 0 }).Count) { throw 'Distinct owned PIDs in session order are required' }
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Not a workspace harness run' }
$trial = Join-Path $run ('pause_{0:yyyyMMdd_HHmmss_fff}_p{1}' -f (Get-Date), $Instance)
New-Item -ItemType Directory -Path $trial | Out-Null
function Read-Pause {
    $data = & python (Join-Path $PSScriptRoot 'snapshot_campaign_pause.py') --pid $OwnedPids
    if ($LASTEXITCODE -ne 0) { throw 'Native pause snapshot failed' }
    return @($data | ConvertFrom-Json)
}
function Test-PauseState($samples, [bool]$opened) {
    if ($samples.Count -ne $count) { return $false }
    for ($peer = 0; $peer -lt $count; ++$peer) {
        $sample = $samples[$peer]
        if ($sample.pid -ne $OwnedPids[$peer] -or $sample.native_count -ne $count -or $sample.client_stage -ne 2) { return $false }
        for ($slot = 0; $slot -lt 4; ++$slot) {
            $expected = if (-not $opened -or $slot -ge $count) { 0 } elseif ($slot -eq $Instance) { 16 } else { 8 }
            if ($sample.pause_masks[$slot] -ne $expected) { return $false }
        }
    }
    return $true
}
function Capture-Frames {
    foreach ($slot in 0..($count - 1)) {
        [IO.File]::WriteAllText((Join-Path $GameRoot "coop_capture.$slot.flag"), 'capture')
    }
}
function Wait-PauseState([string]$name, [bool]$opened) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $samples = Read-Pause
        $samples | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $trial "$name.json")
        if (Test-PauseState $samples $opened) { Capture-Frames; return }
        Start-Sleep -Milliseconds 200
    } while ($timer.Elapsed.TotalSeconds -lt 5)
    Capture-Frames
    throw "Native pause state failed at $name; no further input sent. Evidence: $trial"
}
$baseline = Read-Pause
$baseline | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $trial 'before.json')
if (-not (Test-PauseState $baseline $false)) { throw 'Session is not initially unpaused; refusing menu input' }
& (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $run -Instances $count -Instance $Instance `
    -ExpectedPid $OwnedPids[$Instance] -ScanCode 1 -HoldMilliseconds 150 -GameRoot $GameRoot
Wait-PauseState 'open' $true
Start-Sleep -Seconds 2
& (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $run -Instances $count -Instance $Instance `
    -ExpectedPid $OwnedPids[$Instance] -ScanCode 1 -HoldMilliseconds 150 -GameRoot $GameRoot
Wait-PauseState 'resumed' $false
[pscustomobject]@{Instance=$Instance; Pids=$OwnedPids; NativePauseCycleCompleted=$true;
    Limitation='Native flags and isolated inputs only; inspect captures and post-resume movement separately'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $trial 'result.json')
"Native pause cycle completed: $trial"
