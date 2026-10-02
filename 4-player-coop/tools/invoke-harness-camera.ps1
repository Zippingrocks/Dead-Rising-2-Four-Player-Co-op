[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidateRange(0,3)][int]$Instance,
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$ExpectedPid,
    [ValidateSet(2,4)][int]$Instances = 4,
    [ValidateRange(-120,120)][int]$X = 0,
    [ValidateRange(-120,120)][int]$Y = 0,
    [ValidateRange(1,60)][int]$Steps = 12,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
if ($X -eq 0 -and $Y -eq 0) { throw 'At least one camera axis must be nonzero' }
# DR2 clamps each sample. A large one-frame delta is not a sustained camera turn.
# Every pulse revalidates ownership/liveness and requires actual device consumption.
for ($step = 0; $step -lt $Steps; $step++) {
    & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $RunRoot -GameRoot $GameRoot `
        -Instance $Instance -Instances $Instances -ExpectedPid $ExpectedPid `
        -Mouse -MouseX $X -MouseY $Y -HoldMilliseconds 100 | Out-Null
}
"Consumed $Steps private camera samples for instance $Instance. Inspect a fresh native frame to verify response."
