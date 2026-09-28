param(
    [string]$OutputDir = (Join-Path $PSScriptRoot "..\bin\case-zero-runtime"),
    [string]$VcVarsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "cz_runtime.cpp"
# 4-player co-op module; a stub keeps the runtime buildable without the co-op project.
$coopSource = Join-Path $PSScriptRoot "..\..\4-player-coop\runtime\coop_net.cpp"
if (-not (Test-Path $coopSource)) { $coopSource = Join-Path $PSScriptRoot "coop_stub.cpp" }
$coopSource = (Resolve-Path $coopSource).Path
$vcvars = (Resolve-Path -LiteralPath $VcVarsPath).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path $OutputDir).Path
$output = Join-Path $OutputDir "dinput8.dll"

# Static CRT so the proxy has no redistributable dependency inside the game folder.
$command = 'call "{0}" >nul && cl.exe /nologo /std:c++17 /EHa /O2 /MT /W4 /LD "{1}" "{4}" /Fo"{2}\\" /Fe"{3}" /link /DLL /MACHINE:X86 kernel32.lib user32.lib ws2_32.lib ole32.lib' -f $vcvars, $source, $OutputDir, $output, $coopSource
& $env:ComSpec /d /c $command
if ($LASTEXITCODE -ne 0) { throw "case zero runtime build failed with exit code $LASTEXITCODE" }
Write-Output $output
