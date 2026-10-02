[CmdletBinding(DefaultParameterSetName='Keyboard')]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidateRange(0, 3)][int]$Instance,
    [Parameter(Mandatory)][ValidateRange(1, 2147483647)][int]$ExpectedPid,
    [ValidateSet(2, 4)][int]$Instances = 4,
    [Parameter(Mandatory,ParameterSetName='Keyboard')][ValidateRange(1, 255)][int]$ScanCode,
    [Parameter(Mandatory,ParameterSetName='Mouse')][switch]$Mouse,
    [Parameter(ParameterSetName='Mouse')][ValidateRange(-1000,1000)][int]$MouseX = 0,
    [Parameter(ParameterSetName='Mouse')][ValidateRange(-1000,1000)][int]$MouseY = 0,
    [Parameter(ParameterSetName='Mouse')][ValidateRange(-1200,1200)][int]$MouseWheel = 0,
    [Parameter(ParameterSetName='Mouse')][ValidateRange(0,7)][int]$MouseButtons = 0,
    [ValidateRange(100, 3000)][int]$HoldMilliseconds = 300,
    [switch]$CaptureDuringHold,
    [switch]$ObserveNativeInput,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

# Private harness input only: no desktop activation, OS keyboard injection or actor writes.
$ErrorActionPreference = 'Stop'
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run must belong to this workspace harness'
}
if ($Instance -ge $Instances) { throw 'Requested instance is outside this session' }
$samples = @(Import-Csv -LiteralPath (Join-Path $run 'resources.csv') | Select-Object -Last $Instances)
if ($samples.Count -ne $Instances -or
    (@($samples.Instance | Sort-Object {[int]$_} -Unique) -join ',') -ne ((0..($Instances - 1)) -join ',') -or
    @($samples | Where-Object { $_.Alive -ne 'true' -or
        ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($_.Timestamp)).TotalSeconds -gt 10 }).Count) {
    throw 'A fresh live observation of every session instance is required before gameplay input'
}
$row = @($samples | Where-Object { [int]$_.Instance -eq $Instance })
if ($row.Count -ne 1 -or [int]$row[0].Pid -ne $ExpectedPid -or
    ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($row[0].Timestamp)).TotalSeconds -gt 10) {
    throw 'Expected PID has no fresh owned observation sample'
}
$process = Get-Process -Id $ExpectedPid -ErrorAction Stop
$description = Get-CimInstance Win32_Process -Filter "ProcessId=$ExpectedPid"
if ($process.ProcessName -ne 'deadrising2' -or
    $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
    $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$Instance(?:\s|$)" -or
    $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)') {
    throw 'Process is not the expected hidden harness instance'
}
$logName = if ($Instance -eq 0) { 'coop_net.log' } else { "coop_net.$Instance.log" }
$logPath = Join-Path $GameRoot $logName
function Read-OwnedTrace {
    $process.Refresh()
    if ($process.HasExited -or (Test-Path -LiteralPath (Join-Path $run 'outcome.json'))) {
        throw 'Harness stopped observing; refusing further input'
    }
    $stream = [IO.File]::Open($logPath, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = [IO.StreamReader]::new($stream)
    try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
    if ($text -notmatch "co-op runtime loaded:.* instance=$Instance requestedPlayers=$Instances pid=$ExpectedPid`r?`n") {
        throw 'Runtime log identity mismatch'
    }
    if ($text -match 'native desync assert:|jip broadcast queue: FAILED|pause acknowledgement: FAILED') {
        throw 'Native fault already recorded; preserve evidence instead of sending more input'
    }
    return $text
}
function Read-MouseTrace {
    [void](Read-OwnedTrace)
    $stream = [IO.File]::Open((Join-Path $GameRoot "case_zero_runtime.$Instance.log"),
        [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = [IO.StreamReader]::new($stream)
    try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
    if ($text -notmatch 'input: private mouse device installed;') { throw 'Private mouse is not installed' }
    if ($text -match 'input: mouse rejected|private device capacity exceeded') { throw 'Private mouse failure recorded' }
    return $text
}
function Wait-Acknowledgement([string]$pattern, [int]$count) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $text = if ($Mouse) { Read-MouseTrace } else { Read-OwnedTrace }
        if ([regex]::Matches($text, $pattern).Count -ge $count) {
            return [DateTimeOffset]::Now.ToString('o')
        }
        Start-Sleep -Milliseconds 50
    } while ($timer.Elapsed.TotalSeconds -lt 5)
    throw "Missing fresh input acknowledgement: $pattern"
}
$down = "(?m)script input: DIK=$ScanCode down\r?$"
$up = "(?m)script input: DIK=$ScanCode up\r?$"
$trace = Read-OwnedTrace
$contents = [string]$ScanCode
$commandPath = Join-Path $GameRoot "coop_input.$Instance.txt"
$mouseSequence = $null
if ($Mouse) {
    $trace = Read-MouseTrace
    $previous = @([regex]::Matches($trace, 'input: mouse (?:accepted|consumed) sequence=(\d+)') |
        ForEach-Object { [long]$_.Groups[1].Value })
    $mouseSequence = if ($previous.Count) { ($previous | Measure-Object -Maximum).Maximum + 1 } else { 1 }
    if ($mouseSequence -gt [uint32]::MaxValue) { throw 'Mouse sequence exhausted' }
    $contents = "$mouseSequence $MouseX $MouseY $MouseWheel $MouseButtons"
    $commandPath = Join-Path $GameRoot "coop_mouse.$Instance.txt"
    $down = "(?m)input: mouse consumed sequence=$mouseSequence axes=$MouseX,$MouseY,$MouseWheel buttons=$MouseButtons\r?$"
    $up = "(?m)input: mouse consumed sequence=$mouseSequence axes=0,0,0 buttons=0\r?$"
}
$downCount = [regex]::Matches($trace, $down).Count + 1
$upCount = [regex]::Matches($trace, $up).Count + 1
$created = $false
function Remove-OwnedCommand {
    # The game's file poll can briefly deny deletion. Never report release until
    # our exact command is gone, and never remove a replacement from another writer.
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if (-not (Test-Path -LiteralPath $commandPath)) { return }
        if ([IO.File]::ReadAllText($commandPath) -ne $contents) {
            throw 'Input command changed ownership during release'
        }
        try {
            Remove-Item -LiteralPath $commandPath -Force -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $commandPath)) { return }
        } catch {
            if ($attempt -eq 49) { throw }
        }
        Start-Sleep -Milliseconds 20
    }
    throw 'Input command could not be removed; release is unverified'
}
$record = [ordered]@{ StartedAt=[DateTimeOffset]::Now.ToString('o'); Instance=$Instance;
    Pid=$ExpectedPid; ScanCode=$ScanCode; HoldMilliseconds=$HoldMilliseconds; DownAcknowledgedAt=$null;
    UpAcknowledgedAt=$null; Completed=$false; Error=$null }
if ($Mouse) {
    $record.Mouse = @{Sequence=$mouseSequence;X=$MouseX;Y=$MouseY;Wheel=$MouseWheel;Buttons=$MouseButtons}
    $record.ScanCode = $null
}
try {
    $file = [IO.File]::Open($commandPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $created = $true
    try {
        $bytes = [Text.Encoding]::ASCII.GetBytes($contents)
        $file.Write($bytes, 0, $bytes.Length)
    } finally { $file.Dispose() }
    $record.DownAcknowledgedAt = Wait-Acknowledgement $down $downCount
    $held = [Diagnostics.Stopwatch]::StartNew()
    if ($ObserveNativeInput) {
        $observed = & python (Join-Path $PSScriptRoot 'snapshot_campaign_input.py') --pid $ExpectedPid --samples 5
        if ($LASTEXITCODE -ne 0) { throw 'Native logical input observation failed' }
        $record.NativeInput = @($observed | ConvertFrom-Json)
        if ($record.NativeInput.Count -ne 5 -or
            @($record.NativeInput | Where-Object { $_.local_user -ne $Instance -or $_.pid -ne $ExpectedPid }).Count) {
            throw 'Native logical input owner does not match harness instance'
        }
    }
    if ($CaptureDuringHold) {
        $delay = [Math]::Min(150, [int]($HoldMilliseconds / 2))
        Start-Sleep -Milliseconds $delay
        $capturePath = Join-Path $GameRoot "coop_capture.$Instance.flag"
        $capture = [IO.File]::Open($capturePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $capture.Dispose()
        $record.CaptureRequestedAt = [DateTimeOffset]::Now.ToString('o')
    }
    $remaining = [Math]::Max(0, $HoldMilliseconds - $held.ElapsedMilliseconds)
    if ($remaining -gt 0) { Start-Sleep -Milliseconds $remaining }
} catch {
    $record.Error = $_.Exception.Message
    throw
} finally {
    if ($created) {
        try {
            Remove-OwnedCommand
            $record.UpAcknowledgedAt = if ($Mouse -and $MouseButtons -eq 0) {
                $record.DownAcknowledgedAt # Relative motion is consumed once; no held button needs releasing.
            } else { Wait-Acknowledgement $up $upCount }
        }
        catch { $record.Error = $_.Exception.Message }
    }
    $record.Completed = ($null -ne $record.DownAcknowledgedAt -and $null -ne $record.UpAcknowledgedAt -and $null -eq $record.Error)
    $record | ConvertTo-Json -Depth 10 -Compress | Add-Content -LiteralPath (Join-Path $run 'gameplay-input.jsonl')
}
if (-not $record.Completed) { throw $record.Error }
$record | ConvertTo-Json -Depth 10 -Compress
