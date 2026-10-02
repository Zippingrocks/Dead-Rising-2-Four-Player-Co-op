param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidateRange(0, 3)][int]$Instance,
    [Parameter(Mandatory)][int[]]$OwnedPids,
    [ValidateSet(2,4)][int]$Instances = 4,
    [ValidateSet(17, 30, 31, 32)][int]$ScanCode = 31,
    [ValidateRange(100, 2000)][int]$HoldMilliseconds = 600,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
if ($OwnedPids.Count -ne $Instances -or @($OwnedPids | Select-Object -Unique).Count -ne $Instances -or $Instance -ge $Instances) {
    throw 'Owned PIDs and target must match the explicit session capacity'
}
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) { throw 'Not a workspace harness run' }
$trial = Join-Path $run ('motion_{0:yyyyMMdd_HHmmss_fff}_p{1}' -f (Get-Date), $Instance)
New-Item -ItemType Directory -Path $trial | Out-Null
function Capture-Players([string]$name) {
    $value = & python (Join-Path $PSScriptRoot 'snapshot_campaign_players.py') --pid $OwnedPids
    if ($LASTEXITCODE -ne 0) { throw 'Player sample failed' }
    $value | Set-Content -LiteralPath (Join-Path $trial "$name.json")
}
Capture-Players 'before'
& (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $run -Instance $Instance -ExpectedPid $OwnedPids[$Instance] `
    -ScanCode $ScanCode -HoldMilliseconds $HoldMilliseconds -GameRoot $GameRoot -Instances $Instances
Start-Sleep -Seconds 3
Capture-Players 'after'
$result = & python (Join-Path $PSScriptRoot 'compare_campaign_motion.py') (Join-Path $trial 'before.json') `
    (Join-Path $trial 'after.json') --pid $OwnedPids --player $Instance --players $Instances
if ($LASTEXITCODE -ne 0) { throw 'Movement comparison rejected the samples' }
$result | Set-Content -LiteralPath (Join-Path $trial 'comparison.json')
foreach ($index in 0..($Instances - 1)) {
    $value = & python (Join-Path $PSScriptRoot 'snapshot_connection_mesh.py') --pid $OwnedPids[$index]
    if ($LASTEXITCODE -ne 0) { throw "Network sample failed for instance $index" }
    $value | Set-Content -LiteralPath (Join-Path $trial "network.$index.json")
    [IO.File]::WriteAllText((Join-Path $GameRoot "coop_capture.$index.flag"), 'capture')
}
"Trial: $trial"
$result
