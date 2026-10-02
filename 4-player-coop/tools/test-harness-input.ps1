$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture = Join-Path $workspace ('runtime_logs\coop\input_fixture_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$command = Join-Path $fixture 'coop_input.2.txt'
$log = Join-Path $fixture 'coop_net.2.log'
$mouseCommand = Join-Path $fixture 'coop_mouse.2.txt'
$mouseLog = Join-Path $fixture 'case_zero_runtime.2.log'
$fixtureState = @{WrongProcess=$false; MissingAck=$false; DownSent=$false; UpSent=$false; Mouse=$false; MouseSequence=0; Instance=2; Players=4}
function Get-Process {
    $value = [pscustomobject]@{ ProcessName='deadrising2'; HasExited=$false }
    $value | Add-Member -MemberType ScriptMethod -Name Refresh -Value { }
    $value
}
function Remove-Item {
    param([string]$LiteralPath, [switch]$Force, [string]$ErrorAction)
    $fixtureState.RemoveAttempts++
    if ($fixtureState.RemoveFailures -gt 0) {
        $fixtureState.RemoveFailures--
        throw 'synthetic sharing violation'
    }
    Microsoft.PowerShell.Management\Remove-Item -LiteralPath $LiteralPath -Force:$Force -ErrorAction Stop
}
function Get-CimInstance {
    [pscustomobject]@{ ExecutablePath=(Join-Path $fixture 'deadrising2.exe');
        CommandLine= if ($fixtureState.WrongProcess) { 'unrelated' } else { "-coopinstance=$($fixtureState.Instance) -coopsilent" } }
}
function python {
    $global:LASTEXITCODE = if ($fixtureState.ObserverFailure) { 1 } else { 0 }
    $owner = if ($fixtureState.ObserverWrongOwner) { 0 } else { $fixtureState.Instance }
    @(1..5 | ForEach-Object { @{pid=(4100 + $fixtureState.Instance); local_user=$owner;
        active_indices=@(1,7); buttons=@(@{index=1;state=$true;timers=@(0.5,1,2,3,4)})} }) | ConvertTo-Json -Depth 8
}
function Start-Sleep {
    if ($fixtureState.MissingAck) { throw 'synthetic missing acknowledgement' }
    if ($fixtureState.Mouse) {
        if ((Test-Path -LiteralPath $mouseCommand) -and -not $fixtureState.DownSent) {
            $values = [IO.File]::ReadAllText($mouseCommand).Split(' ')
            $fixtureState.MouseSequence = $values[0]
            $event = "sequence=$($values[0]) axes=$($values[1]),$($values[2]),$($values[3]) buttons=$($values[4])"
            [IO.File]::AppendAllText($mouseLog, "input: mouse accepted $event`ninput: mouse consumed $event`n")
            $fixtureState.DownSent = $true
        } elseif (-not (Test-Path -LiteralPath $mouseCommand) -and $fixtureState.DownSent -and -not $fixtureState.UpSent) {
            [IO.File]::AppendAllText($mouseLog, "input: mouse consumed sequence=$($fixtureState.MouseSequence) axes=0,0,0 buttons=0`n")
            $fixtureState.UpSent = $true
        }
        return
    }
    if ((Test-Path -LiteralPath $command) -and -not $fixtureState.DownSent) {
        [IO.File]::AppendAllText($log, "script input: DIK=17 down`n")
        $fixtureState.DownSent = $true
    } elseif (-not (Test-Path -LiteralPath $command) -and $fixtureState.DownSent -and -not $fixtureState.UpSent) {
        [IO.File]::AppendAllText($log, "script input: DIK=17 up`n")
        $fixtureState.UpSent = $true
    }
}
function Reset-Fixture([int]$players = 4, [int]$slot = 2) {
    $fixtureState.Players = $players
    $fixtureState.Instance = $slot
    $script:command = Join-Path $fixture "coop_input.$slot.txt"
    $script:log = Join-Path $fixture "coop_net.$slot.log"
    $fixtureState.DownSent = $false
    $fixtureState.UpSent = $false
    $fixtureState.WrongProcess = $false
    $fixtureState.MissingAck = $false
    $fixtureState.Mouse = $false
    $fixtureState.RemoveFailures = 0
    $fixtureState.RemoveAttempts = 0
    $fixtureState.ObserverFailure = $false
    $fixtureState.ObserverWrongOwner = $false
    [IO.File]::WriteAllText($log, "12:00 co-op runtime loaded: harness=1 instance=$slot requestedPlayers=$players pid=$(4100 + $slot)`nscript input: DIK=17 down`nscript input: DIK=17 up`n")
    [IO.File]::WriteAllText($mouseLog, "input: private mouse device installed; isolated`ninput: mouse consumed sequence=1 axes=0,0,0 buttons=0`n")
    @(0..($players - 1) | ForEach-Object { [pscustomobject]@{Timestamp=[DateTimeOffset]::Now.ToString('o'); Instance=$_; Pid=4100+$_; Alive='true'} }) |
        Export-Csv -LiteralPath (Join-Path $fixture 'resources.csv') -NoTypeInformation
}
function Invoke-Fixture {
    & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture -Instance $fixtureState.Instance -Instances $fixtureState.Players -ExpectedPid (4100 + $fixtureState.Instance) -ScanCode 17 -HoldMilliseconds 100
}
function Expect-Failure([scriptblock]$action, [string]$label) {
    $failed = $false
    try { & $action | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw "Expected rejection: $label" }
}
Reset-Fixture
$result = Invoke-Fixture | ConvertFrom-Json
if (-not $result.Completed -or -not $fixtureState.DownSent -or -not $fixtureState.UpSent -or (Test-Path -LiteralPath $command)) {
    throw 'Fresh isolated press/release acknowledgements did not complete'
}
Reset-Fixture
$fixtureState.WrongProcess = $true
Expect-Failure { Invoke-Fixture } 'unrelated process'
Reset-Fixture
[IO.File]::WriteAllText($log, 'stale log')
Expect-Failure { Invoke-Fixture } 'stale runtime header'
Reset-Fixture
[IO.File]::AppendAllText($log, "native desync assert: failure`n")
Expect-Failure { Invoke-Fixture } 'native failure'
Reset-Fixture
[IO.File]::WriteAllText($command, '31')
Expect-Failure { Invoke-Fixture } 'preexisting command'
if ([IO.File]::ReadAllText($command) -ne '31') { throw 'An unrelated command was modified' }
Remove-Item -LiteralPath $command
Reset-Fixture
$fixtureState.MissingAck = $true
Expect-Failure { Invoke-Fixture } 'unacknowledged key'
if (Test-Path -LiteralPath $command) { throw 'Failure left a held key' }
Reset-Fixture
[void]($fixtureState.Mouse = $true)
$result = & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture `
    -Instance 2 -ExpectedPid 4102 -Mouse -MouseX -40 -MouseY 20 -HoldMilliseconds 100 | ConvertFrom-Json
if (-not $result.Completed -or $result.Mouse.Sequence -ne 2 -or -not $fixtureState.DownSent -or (Test-Path $mouseCommand)) {
    throw 'Mouse relative motion did not require a new consumed sequence'
}
Reset-Fixture
$fixtureState.Mouse = $true
$result = & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture `
    -Instance 2 -ExpectedPid 4102 -Mouse -MouseButtons 1 -HoldMilliseconds 100 | ConvertFrom-Json
if (-not $result.Completed -or -not $fixtureState.UpSent -or (Test-Path $mouseCommand)) {
    throw 'Mouse button did not require a consumed release'
}
Reset-Fixture
$fixtureState.RemoveFailures = 3
$result = Invoke-Fixture | ConvertFrom-Json
if (-not $result.Completed -or $fixtureState.RemoveAttempts -ne 4 -or (Test-Path $command)) {
    throw 'Transient sharing violation did not retry and verify release'
}
Reset-Fixture
$fixtureState.RemoveFailures = 50
Expect-Failure { Invoke-Fixture } 'persistent release failure'
$last = Get-Content -LiteralPath (Join-Path $fixture 'gameplay-input.jsonl') -Tail 1 | ConvertFrom-Json
if ($last.Completed -or $null -ne $last.UpAcknowledgedAt -or -not (Test-Path $command)) {
    throw 'Persistent release failure was incorrectly reported as completed'
}
Remove-Item -LiteralPath $command
Reset-Fixture
[void]($fixtureState.Mouse = $true)
$capturePath = Join-Path $fixture 'coop_capture.2.flag'
$result = & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture `
    -Instance 2 -ExpectedPid 4102 -Mouse -MouseButtons 1 -HoldMilliseconds 300 -CaptureDuringHold | ConvertFrom-Json
if (-not $result.Completed -or -not $result.CaptureRequestedAt -or -not (Test-Path $capturePath) -or (Test-Path $mouseCommand)) {
    throw 'Mid-input capture did not retain verified button release'
}
Reset-Fixture
$fixtureState.Mouse = $true
Expect-Failure {
    & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture `
        -Instance 2 -ExpectedPid 4102 -Mouse -MouseButtons 1 -HoldMilliseconds 300 -CaptureDuringHold
} 'preexisting capture request'
if (-not $fixtureState.UpSent -or (Test-Path $mouseCommand)) { throw 'Capture failure left a held mouse button' }
Remove-Item -LiteralPath $capturePath
Reset-Fixture
$result = & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture `
    -Instance 2 -ExpectedPid 4102 -ScanCode 17 -HoldMilliseconds 300 -ObserveNativeInput | ConvertFrom-Json
if (-not $result.Completed -or $result.NativeInput.Count -ne 5 -or
    $result.NativeInput[0].buttons[0].timers[0] -ne 0.5 -or (Test-Path $command)) {
    throw 'Native input evidence was truncated or failed to release the key'
}
foreach ($failure in @('ObserverFailure','ObserverWrongOwner')) {
    Reset-Fixture
    $fixtureState[$failure] = $true
    Expect-Failure {
        & (Join-Path $PSScriptRoot 'invoke-harness-input.ps1') -RunRoot $fixture -GameRoot $fixture `
            -Instance 2 -ExpectedPid 4102 -ScanCode 17 -HoldMilliseconds 300 -ObserveNativeInput
    } $failure
    if (-not $fixtureState.UpSent -or (Test-Path $command)) { throw 'Observer failure left a held key' }
}
Reset-Fixture
[IO.File]::WriteAllText($log, "co-op runtime loaded: harness=1 instance=2 requestedPlayers=2 pid=4102`n")
Expect-Failure { Invoke-Fixture } 'wrong session capacity'
Reset-Fixture 2 1
$result = Invoke-Fixture | ConvertFrom-Json
if (-not $result.Completed -or (Test-Path -LiteralPath $command)) { throw 'Two-player input control failed' }
Reset-Fixture
[IO.File]::WriteAllText((Join-Path $fixture 'outcome.json'), '{}')
Expect-Failure { Invoke-Fixture } 'completed harness'
'PASS: private input identity, fresh acknowledgements, stale/fault rejection, command ownership and failure release'
"Fixture retained: $fixture"
