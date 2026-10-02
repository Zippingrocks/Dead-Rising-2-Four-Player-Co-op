$ErrorActionPreference = 'Stop'
$fixture = Join-Path (Split-Path -Parent $PSScriptRoot) ('scratch\admission-test_' + [guid]::NewGuid().ToString('N'))
$GameRoot = $fixture
$started = @([pscustomobject]@{Id=4100}, [pscustomobject]@{Id=4101})
$outcome = @{Reason='fixture memory abort'}
New-Item -ItemType Directory -Path $fixture | Out-Null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'test-four-instances.ps1'), [ref]$null, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('Read-InstanceTrace','Wait-InstanceTrace','Pulse-InstanceKeys','Pulse-AllKeys')) {
    $definition = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if (-not $definition) { throw "Missing $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Assert-HarnessProcessesAlive { }
function Test-MemoryBudget { return $true }
function Start-Sleep { param($Milliseconds, $Seconds) }
function Expect-Failure([scriptblock]$Action, [string]$Name) {
    $failed = $false
    try { & $Action } catch { $failed = $true }
    if (-not $failed) { throw "Expected failure: $Name" }
}
$header = '12:00:00.000 [42] co-op runtime loaded: trace=1 harness=1 silent=1 instance=1 requestedPlayers=4 pid=4101'
$path = Join-Path $fixture 'coop_net.1.log'
[IO.File]::WriteAllText($path, "$header`n12:00:01.000 [42] loopback lobby: Player 2 joined lobby`n")
$joinPattern = '(?m)loopback lobby: Player 2 joined lobby\r?$'
Wait-InstanceTrace 1 $joinPattern 1 0
$writer = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
try { Wait-InstanceTrace 1 $joinPattern 1 0 } finally { $writer.Dispose() }
Expect-Failure { Wait-InstanceTrace 1 $joinPattern 2 0 } 'historical event cannot satisfy a new acknowledgement'
Expect-Failure { Wait-InstanceTrace 1 'Player 3 joined lobby' 1 0 } 'wrong player'
$started[1].Id = 9999
if (Read-InstanceTrace 1) { throw 'Stale process log accepted' }
Expect-Failure { Wait-InstanceTrace 1 $joinPattern 1 0 } 'stale PID'
$started[1].Id = 4101
[IO.File]::WriteAllText($path, $header.Replace('instance=1', 'instance=3'))
if (Read-InstanceTrace 1) { throw 'Wrong instance header accepted' }
[IO.File]::WriteAllText($path, "$header`nscript input: DIK=28 down`nscript input: DIK=28 up`n")

$script:acks = @()
function Wait-InstanceTrace([int]$instance, [string]$pattern, [int]$requiredMatches = 1, [int]$timeoutSeconds = 20) {
    $held = Test-Path -LiteralPath (Join-Path $GameRoot "coop_input.$instance.txt")
    $down = $pattern.Contains(' down')
    if ($held -ne $down) { throw 'Press/release file lifetime is incorrect' }
    if ($requiredMatches -ne 2) { throw 'Pulse did not require a fresh acknowledgement' }
    $script:acks += $down
}
Pulse-InstanceKeys @(1) 28 100
if ($acks.Count -ne 2 -or -not $acks[0] -or $acks[1]) { throw 'Missing independent press/release acknowledgements' }
function Wait-InstanceTrace { throw 'synthetic missing acknowledgement' }
Expect-Failure { Pulse-InstanceKeys @(1) 28 100 } 'missing acknowledgement'
if (Test-Path -LiteralPath (Join-Path $GameRoot 'coop_input.1.txt')) { throw 'Failed pulse left the key held' }
'PASS: fresh PID/instance evidence, exact joiner, new event count, press/release acknowledgement, failure releases keys'
"Fixture retained: $fixture"
