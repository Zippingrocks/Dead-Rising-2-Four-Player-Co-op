[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [ValidateRange(0,3)][int]$LeaderInstance = 0,
    [ValidateRange(10,3600)][int]$DurationSeconds = 900,
    [ValidateRange(100,2000)][int]$IdleMilliseconds = 150,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
$instances = 4
if ($OwnedPids.Count -ne $instances -or @($OwnedPids | Select-Object -Unique).Count -ne $instances -or
    @($OwnedPids | Where-Object { $_ -le 0 }).Count) {
    throw 'Four distinct owned PIDs in session order are required'
}
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run must belong to this workspace harness'
}
$lockPath = Join-Path $run 'formation-follow.lock'
$stopPath = Join-Path $run 'formation-follow.stop'
$logPath = Join-Path $run 'formation-follow.jsonl'
if (Test-Path -LiteralPath $lockPath) { throw 'A formation follower is already active' }
if (Test-Path -LiteralPath $stopPath) { Remove-Item -LiteralPath $stopPath -Force }
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
$lockBytes = [Text.Encoding]::ASCII.GetBytes("pid=$PID leader=$LeaderInstance")
$lock.Write($lockBytes, 0, $lockBytes.Length)
$lock.Dispose()

# Slots follow in a triangle behind the leader camera. Targets are world-space only;
# every movement still goes through that owner's ordinary isolated DirectInput path.
$formation = @{
    0 = @{Behind=0.0; Side=0.0}
    1 = @{Behind=2.2; Side=-1.4}
    2 = @{Behind=2.2; Side=1.4}
    3 = @{Behind=3.8; Side=0.0}
}

function Read-Cameras {
    $output = & python (Join-Path $PSScriptRoot 'snapshot_campaign_camera.py') --pid $OwnedPids
    if ($LASTEXITCODE -ne 0) { throw 'Native camera observation failed' }
    $cameras = @($output | ConvertFrom-Json)
    if ($cameras.Count -ne $instances) { throw 'Camera observer count does not match session' }
    foreach ($instance in 0..3) {
        if ($cameras[$instance].pid -ne $OwnedPids[$instance] -or
            $cameras[$instance].local_user -ne $instance -or
            $cameras[$instance].local_actor.user -ne $instance -or
            $cameras[$instance].local_actor.hidden -or $cameras[$instance].local_actor.render_hidden) {
            throw "Camera ownership mismatch for instance $instance"
        }
    }
    return ,$cameras
}

function Write-Event($value) {
    [IO.File]::AppendAllText($logPath, ($value | ConvertTo-Json -Compress -Depth 6) + "`n")
}

try {
    Write-Event @{event='started';time=[DateTimeOffset]::Now.ToString('o');leader=$LeaderInstance;pids=$OwnedPids}
    $ownershipProven = @{}
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds -and -not (Test-Path -LiteralPath $stopPath) -and
        -not (Test-Path -LiteralPath (Join-Path $run 'outcome.json'))) {
        $cameras = Read-Cameras
        $leader = $cameras[$LeaderInstance]
        $leaderPosition = $leader.local_actor.position
        $forward = $leader.ground_forward_xz
        $right = $leader.ground_right_xz
        foreach ($instance in 0..3) {
            if ($instance -eq $LeaderInstance) { continue }
            $offset = $formation[$instance]
            $targetX = [double]$leaderPosition[0] - [double]$forward[0] * $offset.Behind +
                [double]$right[0] * $offset.Side
            $targetZ = [double]$leaderPosition[2] - [double]$forward[1] * $offset.Behind +
                [double]$right[1] * $offset.Side
            $follower = $cameras[$instance]
            $position = $follower.local_actor.position
            $dx = $targetX - [double]$position[0]
            $dz = $targetZ - [double]$position[2]
            $distance = [Math]::Sqrt($dx * $dx + $dz * $dz)
            if ($distance -le 0.8) { continue }
            if ($distance -gt 8.0) {
                $targetX = [double]$position[0] + $dx / $distance * 8.0
                $targetZ = [double]$position[2] + $dz / $distance * 8.0
            }
            $planText = & python (Join-Path $PSScriptRoot 'campaign_navigation.py') --pid $OwnedPids[$instance] `
                --x $targetX --z $targetZ
            if ($LASTEXITCODE -ne 0) { throw "Follower planning failed for instance $instance" }
            $sample = $planText | ConvertFrom-Json
            if ($sample.camera.local_user -ne $instance -or $sample.camera.scene -ne $follower.scene -or
                $sample.camera.local_actor.address -ne $follower.local_actor.address) {
                throw "Follower ownership changed for instance $instance"
            }
            if (-not $sample.plan.arrived) {
                $hold = if ($distance -gt 10.0) { 700 } elseif ($distance -gt 5.0) { 500 } else { $sample.plan.hold_ms }
                $input = @{RunRoot=$run;GameRoot=$GameRoot;Instances=$instances;Instance=$instance;
                    ExpectedPid=$OwnedPids[$instance];ScanCode=$sample.plan.scan_code;HoldMilliseconds=$hold}
                if (-not $ownershipProven.ContainsKey($instance)) { $input.ObserveNativeInput = $true }
                & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') @input | Out-Null
                $ownershipProven[$instance] = $true
                Write-Event @{event='step';time=[DateTimeOffset]::Now.ToString('o');instance=$instance;
                    leader_position=$leaderPosition;target=@($targetX,$targetZ);distance=$distance;
                    scan_code=$sample.plan.scan_code;hold_ms=$hold;
                    native_ownership_sampled=($input.ContainsKey('ObserveNativeInput'))}
            }
        }
        Start-Sleep -Milliseconds $IdleMilliseconds
    }
} catch {
    Write-Event @{event='failed';time=[DateTimeOffset]::Now.ToString('o');error=$_.Exception.Message}
    throw
} finally {
    if (Test-Path -LiteralPath $lockPath) { Remove-Item -LiteralPath $lockPath -Force }
    if (Test-Path -LiteralPath $stopPath) { Remove-Item -LiteralPath $stopPath -Force }
    Write-Event @{event='stopped';time=[DateTimeOffset]::Now.ToString('o');leader=$LeaderInstance}
}
