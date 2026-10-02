[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2',
    [ValidateRange(10, 60)][int]$SettleSeconds = 25,
    [ValidateRange(30, 180)][int]$TransitionTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$instances = 4
if ($OwnedPids.Count -ne $instances -or
    @($OwnedPids | Select-Object -Unique).Count -ne $instances -or
    @($OwnedPids | Where-Object { $_ -le 0 }).Count) {
    throw 'Four distinct owned PIDs in session order are required'
}
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run must belong to this workspace harness'
}
if (Test-Path -LiteralPath (Join-Path $run 'outcome.json')) {
    throw 'Harness already stopped observing'
}

function Assert-CheckpointAvailable([string]$name) {
    if (Test-Path -LiteralPath (Join-Path $run $name)) {
        throw "Checkpoint already exists: $name"
    }
}

function Assert-OwnedChildren {
    $samples = @(Import-Csv -LiteralPath (Join-Path $run 'resources.csv') | Select-Object -Last $instances)
    if ($samples.Count -ne $instances -or
        (@($samples.Instance | Sort-Object {[int]$_} -Unique) -join ',') -ne '0,1,2,3' -or
        @($samples | Where-Object { $_.Alive -ne 'true' -or
            ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($_.Timestamp)).TotalSeconds -gt 10 }).Count) {
        throw 'A fresh live observation of all four harness children is required'
    }
    foreach ($instance in 0..3) {
        $row = @($samples | Where-Object { [int]$_.Instance -eq $instance })
        if ($row.Count -ne 1 -or [int]$row[0].Pid -ne $OwnedPids[$instance]) {
            throw "Resource ownership mismatch for instance $instance"
        }
        $process = Get-Process -Id $OwnedPids[$instance] -ErrorAction Stop
        $description = Get-CimInstance Win32_Process -Filter "ProcessId=$($OwnedPids[$instance])"
        if ($process.ProcessName -ne 'deadrising2' -or
            $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
            $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$instance(?:\s|$)" -or
            $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)') {
            throw "Process $($OwnedPids[$instance]) is not owned harness instance $instance"
        }
    }
}

function Read-Players {
    $output = & python (Join-Path $PSScriptRoot 'snapshot_campaign_players.py') --pid $OwnedPids
    if ($LASTEXITCODE -ne 0) { throw 'Campaign player snapshot failed' }
    $parsed = @($output | ConvertFrom-Json)
    if ($parsed.Count -ne $instances) { throw 'Campaign snapshot does not contain four observers' }
    foreach ($observer in 0..3) {
        if ($parsed[$observer].pid -ne $OwnedPids[$observer] -or
            @($parsed[$observer].actors).Count -ne $instances) {
            throw "Campaign ownership mismatch for observer $observer"
        }
    }
    return ,$parsed
}

function Save-Players([string]$name, $samples) {
    $path = Join-Path $run $name
    Assert-CheckpointAvailable $name
    [IO.File]::WriteAllText($path, ($samples | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
}

function Send-Key([int]$instance, [int]$scanCode, [int]$milliseconds) {
    & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $run -GameRoot $GameRoot `
        -Instances $instances -Instance $instance -ExpectedPid $OwnedPids[$instance] `
        -ScanCode $scanCode -HoldMilliseconds $milliseconds -ObserveNativeInput | Out-Null
}

function Move-To([int]$instance, [double]$x, [double]$z) {
    & (Join-Path $PSScriptRoot 'invoke-harness-waypoint.ps1') -RunRoot $run -GameRoot $GameRoot `
        -Instances $instances -Instance $instance -ExpectedPid $OwnedPids[$instance] -X $x -Z $z | Out-Null
}

function Test-SharedTransition($before, $after) {
    foreach ($observer in 0..3) {
        $beforeActors = @{}
        foreach ($actor in $before[$observer].actors) { $beforeActors[[int]$actor.slot] = $actor }
        foreach ($actor in $after[$observer].actors) {
            $slot = [int]$actor.slot
            if (-not $beforeActors.ContainsKey($slot) -or $actor.hidden -or $actor.render_hidden) { return $false }
            $first = $beforeActors[$slot].position
            $last = $actor.position
            $distance = [Math]::Sqrt(
                [Math]::Pow([double]$last[0] - [double]$first[0], 2) +
                [Math]::Pow([double]$last[1] - [double]$first[1], 2) +
                [Math]::Pow([double]$last[2] - [double]$first[2], 2))
            if ($distance -lt 20) { return $false }
        }
    }
    return $true
}

Assert-OwnedChildren
foreach ($name in @('local-control-before.json', 'local-control-after.json', 'transition-before.json',
        'transition-after-arrival.json', 'transition-after-control.json', 'combat-phase2-baseline.json')) {
    Assert-CheckpointAvailable $name
}

$localBefore = Read-Players
Save-Players 'local-control-before.json' $localBefore
foreach ($instance in 0..3) { Send-Key $instance 17 350 }
Start-Sleep -Seconds 2
Save-Players 'local-control-after.json' (Read-Players)

# Open the bathroom route using the exact interaction duration from the stable control.
Move-To 0 4.2 23.7
Send-Key 0 18 700
Start-Sleep -Seconds 20

$route = @(@(0.0,23.7), @(0.0,29.0), @(-1.0,29.0), @(-5.0,29.0),
    @(-4.0,32.0), @(-4.0,35.0), @(-4.5,38.0))
$ventTargets = @(@(-8.05,38.57), @(-6.5,39.0), @(-5.5,40.5), @(-7.0,40.5))
foreach ($instance in @(0,3,2,1)) {
    foreach ($point in $route) { Move-To $instance $point[0] $point[1] }
    Move-To $instance $ventTargets[$instance][0] $ventTargets[$instance][1]
}

$transitionBefore = Read-Players
Save-Players 'transition-before.json' $transitionBefore
Start-Sleep -Seconds $SettleSeconds
Send-Key 0 18 700

$timer = [Diagnostics.Stopwatch]::StartNew()
$arrival = $null
do {
    Start-Sleep -Seconds 1
    Assert-OwnedChildren
    $candidate = Read-Players
    if (Test-SharedTransition $transitionBefore $candidate) { $arrival = $candidate; break }
} while ($timer.Elapsed.TotalSeconds -lt $TransitionTimeoutSeconds)
if ($null -eq $arrival) { throw 'All four owners did not complete the shared transition before timeout' }
Save-Players 'transition-after-arrival.json' $arrival

$controlTargets = @(@(-304.5,-154.8), @(-305.9,-153.0), @(-308.2,-155.4), @(-306.4,-157.5))
foreach ($instance in 0..3) {
    Move-To $instance $controlTargets[$instance][0] $controlTargets[$instance][1]
}
Save-Players 'transition-after-control.json' (Read-Players)

& (Join-Path $PSScriptRoot 'snapshot-harness-combat.ps1') -RunRoot $run -Label 'phase2-baseline' `
    -Instances $instances -GameRoot $GameRoot | Out-Null
[pscustomobject]@{
    Run = $run
    SharedTransitionCompleted = $true
    LocalControlCheckpoints = $true
    PostTransitionControlCheckpoints = $true
    CombatBaselineReady = $true
    Pids = $OwnedPids
} | ConvertTo-Json -Depth 4
