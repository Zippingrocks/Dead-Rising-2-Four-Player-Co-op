$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$output = Join-Path $PSScriptRoot '..\builds\abi-tests'
$output = (Resolve-Path -LiteralPath $output).Path
$source = Join-Path $PSScriptRoot 'test_thread_snapshot_target.cpp'
$exe = Join-Path $output 'test_thread_snapshot_target.exe'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat'
$command = 'call "{0}" >nul && cl.exe /nologo /std:c++17 /EHsc /W4 /WX "{1}" /Fo"{2}\\" /Fe"{3}"' -f $vcvars, $source, $output, $exe
& $env:ComSpec /d /c $command
if ($LASTEXITCODE -ne 0) { throw 'Snapshot target compilation failed' }
$monitor = Join-Path $workspace 'tools\dr2-crash-dump-monitor\bin\Release\net8.0\DR2CrashDumpMonitor.exe'
$report = Join-Path $output ('thread_snapshot_' + [guid]::NewGuid().ToString('N') + '.json')
$target = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 500
    & $monitor --pid $target.Id --threads-path $report
    if ($LASTEXITCODE -ne 0) { throw 'Snapshot failed' }
    $snapshot = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    if (@($snapshot.threads).Count -eq 0) { throw 'No threads captured' }
    if (@($snapshot.threads | Where-Object { -not $_.resumed -or $_.capture.error }).Count) {
        throw 'Thread capture or balanced resume failed'
    }
    if (@($snapshot.threads | Where-Object { $_.capture.architecture -eq 'x86' -and $_.capture.registers.eip -ne '0x00000000' }).Count -eq 0) {
        throw 'No valid x86 instruction pointer'
    }
    if (@($snapshot.threads.capture.stackAddressHints | Where-Object { $_.location -like 'test_thread_snapshot_target.exe+*' }).Count -eq 0) {
        throw 'No fixture return address found on the x86 stack'
    }
    $target.Refresh()
    if ($target.HasExited) { throw 'Fixture exited unexpectedly' }
    Write-Output "WOW64 snapshot: $($snapshot.threads.Count) x86 threads captured/resumed; target remained alive"
} finally {
    if (-not $target.HasExited) { Stop-Process -Id $target.Id -Force }
    $target.WaitForExit()
}
