[CmdletBinding()]
param(
    [string]$GameRoot,
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
    if ($NonInteractive) { throw "Dead Rising 2 was not found. Pass -GameRoot." }
    while ($true) {
        $entered = Read-Host "Enter the Dead Rising 2 folder"
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

if (-not $SkipProcessCheck -and (Get-Process -Name "deadrising2" -ErrorAction SilentlyContinue)) {
    throw "Dead Rising 2 is running. Close every game instance before uninstalling the mod."
}

$GameRoot = Resolve-Dr2Root $GameRoot
$liveDll = Join-Path $GameRoot "dinput8.dll"
$liveConfig = Join-Path $GameRoot "four_player_coop.ini"
$stateDir = Join-Path $GameRoot "DR2FourPlayerCoop"
$statePath = Join-Path $stateDir "install-state.json"
$backupDll = Join-Path $stateDir "dinput8.preinstall.dll"
$backupConfig = Join-Path $stateDir "four_player_coop.preinstall.ini"
if (-not (Test-Path -LiteralPath $statePath)) {
    throw "No managed Four-Player Co-op installation was found in $GameRoot."
}

$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if (Test-Path -LiteralPath $liveDll) {
    $liveHash = Get-Sha256 $liveDll
    if ($liveHash -ne ([string]$state.installedDinput8Sha256).ToUpperInvariant()) {
        throw "dinput8.dll changed after installation. Refusing to delete another mod or manual update."
    }
    Remove-Item -LiteralPath $liveDll -Force
}

if ([bool]$state.hadPreviousDinput8) {
    if (-not (Test-Path -LiteralPath $backupDll)) { throw "The pre-install dinput8.dll backup is missing." }
    if ((Get-Sha256 $backupDll) -ne ([string]$state.previousDinput8Sha256).ToUpperInvariant()) {
        throw "The pre-install dinput8.dll backup failed hash verification."
    }
    Copy-Atomic $backupDll $liveDll
}

if (Test-Path -LiteralPath $liveConfig) { Remove-Item -LiteralPath $liveConfig -Force }
if ([bool]$state.hadPreviousConfig) {
    if (-not (Test-Path -LiteralPath $backupConfig)) { throw "The pre-install config backup is missing." }
    if ((Get-Sha256 $backupConfig) -ne ([string]$state.previousConfigSha256).ToUpperInvariant()) {
        throw "The pre-install config backup failed hash verification."
    }
    Copy-Atomic $backupConfig $liveConfig
}

Remove-Item -LiteralPath $stateDir -Recurse -Force
Write-Host "Dead Rising 2 Four-Player Co-op was removed."
if ([bool]$state.hadPreviousDinput8) { Write-Host "The previous dinput8.dll was restored." }

