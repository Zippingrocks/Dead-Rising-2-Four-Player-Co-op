[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidateRange(0,3)][int]$Instance,
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ExpectedPid,
    [ValidateSet(2,4)][int]$Instances = 4,
    [Parameter(Mandatory)][double]$X,
    [Parameter(Mandatory)][double]$Z,
    [ValidateRange(1,30)][int]$MaximumSteps = 24,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or $Instance -ge $Instances) {
    throw 'Invalid owned harness run or instance'
}
$trial = Join-Path $run ('navigation_{0:yyyyMMdd_HHmmss_fff}_p{1}.json' -f (Get-Date), $Instance)
$record = [ordered]@{Instance=$Instance;Pid=$ExpectedPid;TargetXZ=@($X,$Z);Steps=@();Arrived=$false;Error=$null}
$initial = $null
$previousDistance = $null
$stalled = 0
try {
    for ($step = 0; $step -le $MaximumSteps; $step++) {
        $json = & python (Join-Path $PSScriptRoot 'campaign_navigation.py') --pid $ExpectedPid --x $X --z $Z
        if ($LASTEXITCODE -ne 0) { throw 'Native camera/waypoint observation failed' }
        $sample = $json | ConvertFrom-Json
        if ($sample.camera.local_user -ne $Instance) { throw 'Local actor ownership differs from requested instance' }
        if ($null -eq $initial) { $initial = $sample.camera }
        if ($sample.camera.scene -ne $initial.scene -or $sample.camera.local_actor.address -ne $initial.local_actor.address) {
            throw 'Scene or local actor changed; stop navigation and inspect transition'
        }
        $record.Steps += $sample
        if ($sample.plan.arrived) { $record.Arrived = $true; break }
        if ($null -ne $previousDistance) {
            $stalled = if ($previousDistance - $sample.plan.distance -lt 0.02) { $stalled + 1 } else { 0 }
            if ($stalled -ge 3) { throw 'Waypoint is blocked or input direction differs; no further input sent' }
        }
        if ($step -eq $MaximumSteps) { throw 'Bounded waypoint step budget exhausted' }
        $previousDistance = $sample.plan.distance
        & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $run -GameRoot $GameRoot `
            -Instance $Instance -Instances $Instances -ExpectedPid $ExpectedPid `
            -ScanCode $sample.plan.scan_code -HoldMilliseconds $sample.plan.hold_ms `
            -ObserveNativeInput | Out-Null
        Start-Sleep -Milliseconds 150
    }
} catch {
    $record.Error = $_.Exception.Message
    throw
} finally {
    $record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $trial
}
"Observed arrival with native input only: $trial"
