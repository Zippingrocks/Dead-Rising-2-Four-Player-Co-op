param(
    [string]$VcVarsPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat'
)

$ErrorActionPreference = 'Stop'
$output = Join-Path $PSScriptRoot '..\builds\abi-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$output = (Resolve-Path -LiteralPath $output).Path
$vcvars = (Resolve-Path -LiteralPath $VcVarsPath).Path
foreach ($name in @('test_steam_callback_abi', 'test_save_namespace', 'test_local_save_storage', 'test_harness_keyboard', 'test_harness_sync', 'test_clothing_capacity', 'test_nfs_request_owner')) {
    $source = Join-Path $PSScriptRoot "$name.cpp"
    $exe = Join-Path $output "$name.exe"
    $command = 'call "{0}" >nul && cl.exe /nologo /std:c++17 /EHsc /Od /RTC1 /MDd /W4 /WX "{1}" /Fo"{2}\\" /Fe"{3}"' -f $vcvars, $source, $output, $exe
    & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
    if ($name -eq 'test_local_save_storage') {
        $fixtures = Join-Path $output ('local_save_' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixtures | Out-Null
        & $exe $fixtures
    } else { & $exe }
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
