[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunRoot,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,47}$')][string]$Label,
    [ValidateSet(2, 4)][int]$Instances = 4,
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2'
)

$ErrorActionPreference = 'Stop'
$run = (Resolve-Path -LiteralPath $RunRoot).Path
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $workspace 'runtime_logs\coop')) + '\'
if (-not $run.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run must belong to this workspace harness'
}
if (Test-Path -LiteralPath (Join-Path $run 'outcome.json')) {
    throw 'Harness already stopped observing'
}

$samples = @(Import-Csv -LiteralPath (Join-Path $run 'resources.csv') | Select-Object -Last $Instances)
if ($samples.Count -ne $Instances -or
    (@($samples.Instance | Sort-Object {[int]$_} -Unique) -join ',') -ne ((0..($Instances - 1)) -join ',') -or
    @($samples | Where-Object { $_.Alive -ne 'true' -or
        ([DateTimeOffset]::Now - [DateTimeOffset]::Parse($_.Timestamp)).TotalSeconds -gt 10 }).Count) {
    throw 'A fresh live observation of every session instance is required'
}

$pids = @()
foreach ($instance in 0..($Instances - 1)) {
    $row = @($samples | Where-Object { [int]$_.Instance -eq $instance })
    if ($row.Count -ne 1) { throw "Missing owned process for instance $instance" }
    $pidValue = [int]$row[0].Pid
    $process = Get-Process -Id $pidValue -ErrorAction Stop
    $description = Get-CimInstance Win32_Process -Filter "ProcessId=$pidValue"
    if ($process.ProcessName -ne 'deadrising2' -or
        $description.ExecutablePath -ne (Join-Path $GameRoot 'deadrising2.exe') -or
        $description.CommandLine -notmatch "(?i)(?:^|\s)-coopinstance=$instance(?:\s|$)" -or
        $description.CommandLine -notmatch '(?i)(?:^|\s)-coopsilent(?:\s|$)') {
        throw "Process $pidValue is not the expected hidden harness instance $instance"
    }
    $pids += $pidValue
}

$output = & python (Join-Path $PSScriptRoot 'snapshot_campaign_combat.py') --pid $pids
if ($LASTEXITCODE -ne 0) { throw 'Native combat snapshot failed' }
$parsed = @($output | ConvertFrom-Json)
if ($parsed.Count -ne $Instances -or
    (@($parsed.local_user | Sort-Object -Unique) -join ',') -ne ((0..($Instances - 1)) -join ',')) {
    throw 'Combat snapshot ownership does not cover every local user exactly once'
}
$path = Join-Path $run "combat-$Label.json"
if (Test-Path -LiteralPath $path) { throw "Combat snapshot already exists: $Label" }
[IO.File]::WriteAllText($path, ($parsed | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
"Combat snapshot: $path"
$parsed | ConvertTo-Json -Depth 12
