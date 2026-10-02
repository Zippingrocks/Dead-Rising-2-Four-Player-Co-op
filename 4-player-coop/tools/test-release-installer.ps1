$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$installer = Join-Path $repoRoot "4-player-coop\package\Install-DR2FourPlayerCoop.ps1"
$uninstaller = Join-Path $repoRoot "4-player-coop\package\Uninstall-DR2FourPlayerCoop.ps1"
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("dr2-four-player-installer-" + [Guid]::NewGuid().ToString("N"))

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASSERTION FAILED: $Message" }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function New-FakeGame([string]$Name) {
    $root = Join-Path $testRoot $Name
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    Set-Content -LiteralPath (Join-Path $root "deadrising2.exe") -Value "fake game" -NoNewline
    Set-Content -LiteralPath (Join-Path $root "steam_api.dll") -Value "fake steam" -NoNewline
    return $root
}

function New-FakePackage([string]$Name, [string]$Version, [string]$DllBytes) {
    $root = Join-Path $testRoot $Name
    $payload = Join-Path $root "payload"
    New-Item -ItemType Directory -Force -Path $payload | Out-Null
    Set-Content -LiteralPath (Join-Path $payload "dinput8.dll") -Value $DllBytes -NoNewline
    Set-Content -LiteralPath (Join-Path $payload "four_player_coop.ini") -Value "[FourPlayerCoop]`r`nEnabled=1" -NoNewline
    $manifest = [ordered]@{
        schemaVersion = 1
        version = $Version
        protocol = "1"
        files = [ordered]@{
            "dinput8.dll" = [ordered]@{ sha256 = (Get-Sha256 (Join-Path $payload "dinput8.dll")) }
            "four_player_coop.ini" = [ordered]@{ sha256 = (Get-Sha256 (Join-Path $payload "four_player_coop.ini")) }
        }
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root "manifest.json") -Encoding UTF8
    return $root
}

try {
    New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
    $package1 = New-FakePackage "package-v1" "0.1.0-test.1" "four-player-v1"
    $package2 = New-FakePackage "package-v2" "0.1.0-test.2" "four-player-v2"

    $cleanGame = New-FakeGame "clean-game"
    & $installer -GameRoot $cleanGame -PackageRoot $package1 -NonInteractive -SkipProcessCheck
    Assert-True (Test-Path -LiteralPath (Join-Path $cleanGame "dinput8.dll")) "clean install did not place dinput8.dll"
    & $uninstaller -GameRoot $cleanGame -NonInteractive -SkipProcessCheck
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $cleanGame "dinput8.dll"))) "clean uninstall left dinput8.dll"

    $existingGame = New-FakeGame "existing-proxy-game"
    Set-Content -LiteralPath (Join-Path $existingGame "dinput8.dll") -Value "pre-existing-proxy" -NoNewline
    Set-Content -LiteralPath (Join-Path $existingGame "four_player_coop.ini") -Value "pre-existing-config" -NoNewline
    $originalDllHash = Get-Sha256 (Join-Path $existingGame "dinput8.dll")
    $originalConfigHash = Get-Sha256 (Join-Path $existingGame "four_player_coop.ini")
    & $installer -GameRoot $existingGame -PackageRoot $package1 -NonInteractive -SkipProcessCheck
    & $installer -GameRoot $existingGame -PackageRoot $package2 -NonInteractive -SkipProcessCheck
    Assert-True ((Get-Sha256 (Join-Path $existingGame "dinput8.dll")) -eq (Get-Sha256 (Join-Path $package2 "payload\dinput8.dll"))) "update did not place v2"
    & $uninstaller -GameRoot $existingGame -NonInteractive -SkipProcessCheck
    Assert-True ((Get-Sha256 (Join-Path $existingGame "dinput8.dll")) -eq $originalDllHash) "uninstall did not restore the prior proxy"
    Assert-True ((Get-Sha256 (Join-Path $existingGame "four_player_coop.ini")) -eq $originalConfigHash) "uninstall did not restore the prior config"

    $tamperGame = New-FakeGame "tamper-game"
    & $installer -GameRoot $tamperGame -PackageRoot $package1 -NonInteractive -SkipProcessCheck
    Set-Content -LiteralPath (Join-Path $tamperGame "dinput8.dll") -Value "changed-by-someone-else" -NoNewline
    $installRefused = $false
    try { & $installer -GameRoot $tamperGame -PackageRoot $package2 -NonInteractive -SkipProcessCheck } catch { $installRefused = $true }
    Assert-True $installRefused "installer overwrote a changed live proxy"
    $uninstallRefused = $false
    try { & $uninstaller -GameRoot $tamperGame -NonInteractive -SkipProcessCheck } catch { $uninstallRefused = $true }
    Assert-True $uninstallRefused "uninstaller deleted a changed live proxy"

    Write-Host "PASS: release installer clean install, update, backup/restore, and tamper refusal"
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        $resolved = (Resolve-Path -LiteralPath $testRoot).Path
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove test path outside TEMP: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
