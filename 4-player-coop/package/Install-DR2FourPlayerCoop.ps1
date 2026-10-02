[CmdletBinding()]
param(
    [string]$GameRoot,
    [string]$PackageRoot = $PSScriptRoot,
    [switch]$NonInteractive,
    [switch]$SkipProcessCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Resolve-Dr2Root([string]$RequestedRoot) {
    $candidates = @()
    if ($RequestedRoot) { $candidates += $RequestedRoot }
    if (${env:ProgramFiles(x86)}) {
        $candidates += (Join-Path ${env:ProgramFiles(x86)} "Steam\steamapps\common\Dead Rising 2")
    }
    if ($env:ProgramFiles) {
        $candidates += (Join-Path $env:ProgramFiles "Steam\steamapps\common\Dead Rising 2")
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate "deadrising2.exe"))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    if ($NonInteractive) {
        throw "Dead Rising 2 was not found. Pass -GameRoot with the folder containing deadrising2.exe."
    }

    while ($true) {
        $entered = Read-Host "Enter the Dead Rising 2 folder (the folder containing deadrising2.exe)"
        if ($entered -and (Test-Path -LiteralPath (Join-Path $entered "deadrising2.exe"))) {
            return (Resolve-Path -LiteralPath $entered).Path
        }
        Write-Warning "That folder does not contain deadrising2.exe."
    }
}

function Copy-Atomic([string]$Source, [string]$Destination) {
    $temporary = "$Destination.dr2fourplayer.tmp"
    Copy-Item -LiteralPath $Source -Destination $temporary -Force
    Move-Item -LiteralPath $temporary -Destination $Destination -Force
}

function Write-JsonAtomic($Value, [string]$Destination) {
    $temporary = "$Destination.tmp"
    $Value | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Destination -Force
}

if (-not $SkipProcessCheck -and (Get-Process -Name "deadrising2" -ErrorAction SilentlyContinue)) {
    throw "Dead Rising 2 is running. Close every game instance before installing the mod."
}

$PackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$manifestPath = Join-Path $PackageRoot "manifest.json"
$payloadDll = Join-Path $PackageRoot "payload\dinput8.dll"
$payloadConfig = Join-Path $PackageRoot "payload\four_player_coop.ini"
foreach ($required in @($manifestPath, $payloadDll, $payloadConfig)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Release package is incomplete: missing $required" }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expectedDllHash = [string]$manifest.files.'dinput8.dll'.sha256
$expectedConfigHash = [string]$manifest.files.'four_player_coop.ini'.sha256
if ((Get-Sha256 $payloadDll) -ne $expectedDllHash.ToUpperInvariant()) {
    throw "The packaged dinput8.dll does not match manifest.json. Re-download the release."
}
if ((Get-Sha256 $payloadConfig) -ne $expectedConfigHash.ToUpperInvariant()) {
    throw "The packaged four_player_coop.ini does not match manifest.json. Re-download the release."
}

$GameRoot = Resolve-Dr2Root $GameRoot
if (-not (Test-Path -LiteralPath (Join-Path $GameRoot "steam_api.dll"))) {
    throw "The selected folder is not a supported Steam Dead Rising 2 install (steam_api.dll is missing)."
}

$liveDll = Join-Path $GameRoot "dinput8.dll"
$liveConfig = Join-Path $GameRoot "four_player_coop.ini"
$stateDir = Join-Path $GameRoot "DR2FourPlayerCoop"
$statePath = Join-Path $stateDir "install-state.json"
$backupDll = Join-Path $stateDir "dinput8.preinstall.dll"
$backupConfig = Join-Path $stateDir "four_player_coop.preinstall.ini"
$state = $null
if (Test-Path -LiteralPath $statePath) {
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
}

if ($state -and (Test-Path -LiteralPath $liveDll)) {
    $liveHash = Get-Sha256 $liveDll
    if ($liveHash -ne ([string]$state.installedDinput8Sha256).ToUpperInvariant()) {
        throw "dinput8.dll changed after this mod was installed. Refusing to overwrite another change; uninstall or restore it manually first."
    }
}

New-Item -ItemType Directory -Force -Path $stateDir | Out-Null

$hadPreviousDll = $false
$previousDllHash = $null
$hadPreviousConfig = $false
$previousConfigHash = $null
if ($state) {
    $hadPreviousDll = [bool]$state.hadPreviousDinput8
    $previousDllHash = $state.previousDinput8Sha256
    $hadPreviousConfig = [bool]$state.hadPreviousConfig
    $previousConfigHash = $state.previousConfigSha256
} else {
    if (Test-Path -LiteralPath $liveDll) {
        $liveHash = Get-Sha256 $liveDll
        if ($liveHash -ne $expectedDllHash.ToUpperInvariant()) {
            Copy-Item -LiteralPath $liveDll -Destination $backupDll -Force
            $hadPreviousDll = $true
            $previousDllHash = $liveHash
        }
    }
    if (Test-Path -LiteralPath $liveConfig) {
        Copy-Item -LiteralPath $liveConfig -Destination $backupConfig -Force
        $hadPreviousConfig = $true
        $previousConfigHash = Get-Sha256 $liveConfig
    }
}

Copy-Atomic $payloadDll $liveDll
if (-not (Test-Path -LiteralPath $liveConfig) -or -not $state) {
    Copy-Atomic $payloadConfig $liveConfig
}

$newState = [ordered]@{
    schemaVersion = 1
    modVersion = [string]$manifest.version
    protocol = [string]$manifest.protocol
    installedAtUtc = [DateTime]::UtcNow.ToString("o")
    installedDinput8Sha256 = Get-Sha256 $liveDll
    installedConfigSha256 = Get-Sha256 $liveConfig
    hadPreviousDinput8 = $hadPreviousDll
    previousDinput8Sha256 = $previousDllHash
    hadPreviousConfig = $hadPreviousConfig
    previousConfigSha256 = $previousConfigHash
}
Write-JsonAtomic $newState $statePath

Write-Host "Dead Rising 2 Four-Player Co-op $($manifest.version) installed."
Write-Host "Game: $GameRoot"
Write-Host "Protocol: $($manifest.protocol)"
Write-Host "Every participant must install this exact release before joining through the normal co-op menu."

