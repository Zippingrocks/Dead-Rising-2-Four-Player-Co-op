[CmdletBinding()]
param(
    [string]$GameRoot = 'G:\SteamLibrary\steamapps\common\Dead Rising 2',
    [ValidateRange(15, 180)]
    [int]$InitialDelaySeconds = 60,
    [ValidateRange(2, 30)]
    [int]$StageDelaySeconds = 8,
    [ValidateRange(15, 300)]
    [int]$JoinTimeoutSeconds = 90
)

$ErrorActionPreference = 'Stop'
$expectedExe = [IO.Path]::GetFullPath((Join-Path $GameRoot 'deadrising2.exe'))
$configPath = Join-Path $GameRoot 'four_player_coop.ini'
$inputPath = Join-Path $GameRoot 'four_player_auto_input.txt'
$logPath = Join-Path $GameRoot 'coop_net.log'

if (-not (Test-Path -LiteralPath $expectedExe)) { throw "DR2 executable not found: $expectedExe" }
if (-not (Test-Path -LiteralPath $configPath)) { throw "Four-player config not found: $configPath" }
$config = Get-Content -LiteralPath $configPath -Raw
if ($config -notmatch '(?mi)^\s*BackgroundAutoJoin\s*=\s*1\s*$') {
    throw 'BackgroundAutoJoin=1 is required for private unattended input.'
}

$target = @(Get-Process -Name deadrising2 -ErrorAction SilentlyContinue | Where-Object {
    try { [IO.Path]::GetFullPath($_.Path) -eq $expectedExe } catch { $false }
})
if ($target.Count -ne 1) { throw "Exactly one matching DR2 process is required; found $($target.Count)." }
$target = $target[0]
$startTicks = $target.StartTime.ToUniversalTime().Ticks

function Assert-TargetIdentity {
    $current = Get-Process -Id $target.Id -ErrorAction SilentlyContinue
    if (-not $current -or $current.StartTime.ToUniversalTime().Ticks -ne $startTicks -or
        [IO.Path]::GetFullPath($current.Path) -ne $expectedExe) {
        throw 'The owned DR2 process exited or changed identity.'
    }
}

function Pulse-Key([int]$ScanCode, [string]$Label) {
    Assert-TargetIdentity
    [IO.File]::WriteAllText($inputPath, [string]$ScanCode)
    Write-Output ('{0} input down DIK={1} {2}' -f (Get-Date -Format 'HH:mm:ss'), $ScanCode, $Label)
    Start-Sleep -Milliseconds 1800
    Remove-Item -LiteralPath $inputPath -Force -ErrorAction SilentlyContinue
    Write-Output ('{0} input up DIK={1} {2}' -f (Get-Date -Format 'HH:mm:ss'), $ScanCode, $Label)
    Start-Sleep -Milliseconds 600
}

try {
    Remove-Item -LiteralPath $inputPath -Force -ErrorAction SilentlyContinue
    Write-Output ('{0} waiting {1}s for native title load' -f (Get-Date -Format 'HH:mm:ss'), $InitialDelaySeconds)
    Start-Sleep -Seconds $InitialDelaySeconds
    Pulse-Key 28 'leave title screen'
    Start-Sleep -Seconds $StageDelaySeconds
    Pulse-Key 208 'select online/co-op route'
    Pulse-Key 28 'confirm route'
    Start-Sleep -Seconds $StageDelaySeconds
    Pulse-Key 208 'flush prior selection'
    Pulse-Key 200 'return to Join Online Game'
    Pulse-Key 28 'enter Join Co-op Game'
    Start-Sleep -Seconds $StageDelaySeconds
    Pulse-Key 28 'dismiss client-save warning'
    Start-Sleep -Seconds $StageDelaySeconds
    Pulse-Key 28 'select first save/profile and start native lobby join'

    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $joined = $false
    do {
        Assert-TargetIdentity
        if (Test-Path -LiteralPath $logPath) {
            $log = Get-Content -LiteralPath $logPath -Raw
            if ($log -match 'production matchmaking: accepted lobby=') {
                $joined = $true
                break
            }
        }
        Start-Sleep -Seconds 1
    } while ($deadline.Elapsed.TotalSeconds -lt $JoinTimeoutSeconds)
    if (-not $joined) { throw "No native JoinLobby acceptance appeared within $JoinTimeoutSeconds seconds." }
    Write-Output ('{0} native JoinLobby accepted' -f (Get-Date -Format 'HH:mm:ss'))
} finally {
    Remove-Item -LiteralPath $inputPath -Force -ErrorAction SilentlyContinue
}
