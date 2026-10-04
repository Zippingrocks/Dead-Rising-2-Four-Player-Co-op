[CmdletBinding()]
param(
    [string]$OutputDir,
    [string]$VcVarsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$packageSource = Join-Path $repoRoot "4-player-coop\package"
$buildRoot = Join-Path $repoRoot "4-player-coop\builds"
if (-not $OutputDir) { $OutputDir = Join-Path $buildRoot "releases" }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$version = (Get-Content -LiteralPath (Join-Path $packageSource "VERSION") -Raw).Trim()
$protocol = "2"
if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$') {
    throw "Invalid package VERSION: $version"
}

$runtimeOutput = Join-Path $buildRoot "release-runtime"
$stagingParent = Join-Path $buildRoot "release-staging"
$releaseName = "Dead-Rising-2-Four-Player-Co-op-$version"
$staging = Join-Path $stagingParent $releaseName
$payloadRoot = Join-Path $staging "Dead Rising 2"
$zipPath = Join-Path $OutputDir "$releaseName.zip"

foreach ($path in @($runtimeOutput, $stagingParent, $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $path | Out-Null
}
if (Test-Path -LiteralPath $staging) {
    $resolvedStaging = (Resolve-Path -LiteralPath $staging).Path
    $resolvedBuildRoot = (Resolve-Path -LiteralPath $buildRoot).Path
    if (-not $resolvedStaging.StartsWith($resolvedBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clear staging outside the repository build directory: $resolvedStaging"
    }
    Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $payloadRoot | Out-Null

$buildScript = Join-Path $repoRoot "tools\case-zero-runtime\build.ps1"
$builtDll = & $buildScript -OutputDir $runtimeOutput -VcVarsPath $VcVarsPath | Select-Object -Last 1
if (-not (Test-Path -LiteralPath $builtDll)) { throw "Runtime build did not produce dinput8.dll." }

foreach ($name in @("README.md", "VERSION")) {
    Copy-Item -LiteralPath (Join-Path $packageSource $name) -Destination (Join-Path $payloadRoot $name)
}
Copy-Item -LiteralPath $builtDll -Destination (Join-Path $payloadRoot "dinput8.dll")
Copy-Item -LiteralPath (Join-Path $packageSource "four_player_coop.ini") -Destination (Join-Path $payloadRoot "four_player_coop.ini")

$dllHash = (Get-FileHash -LiteralPath (Join-Path $payloadRoot "dinput8.dll") -Algorithm SHA256).Hash.ToUpperInvariant()
$configHash = (Get-FileHash -LiteralPath (Join-Path $payloadRoot "four_player_coop.ini") -Algorithm SHA256).Hash.ToUpperInvariant()
$manifest = [ordered]@{
    schemaVersion = 1
    name = "Dead Rising 2 Four-Player Co-op"
    version = $version
    protocol = $protocol
    steamAppId = 45740
    architecture = "x86"
    builtAtUtc = [DateTime]::UtcNow.ToString("o")
    files = [ordered]@{
        "dinput8.dll" = [ordered]@{ sha256 = $dllHash }
        "four_player_coop.ini" = [ordered]@{ sha256 = $configHash }
    }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $payloadRoot "manifest.json") -Encoding UTF8

if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Compress-Archive -Path $payloadRoot -DestinationPath $zipPath -CompressionLevel Optimal

$result = [ordered]@{
    release = $zipPath
    version = $version
    protocol = $protocol
    dinput8Sha256 = $dllHash
    zipSha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToUpperInvariant()
}
$result | ConvertTo-Json
