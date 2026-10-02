$ErrorActionPreference = 'Stop'
$workspace = Join-Path (Split-Path -Parent $PSScriptRoot) ("scratch\content-swap-test_" + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $workspace) { throw "Fixture already exists: $workspace" }
$GameRoot = Join-Path $workspace 'game'
$runRoot = Join-Path $workspace 'run'
$StockSafehouseContent = $true
$StockStreamedAssetsContent = $false
$contentSwap = @()
$outcome = @{ Status = 'test'; Reason = $null }
$originalRoot = Join-Path $workspace 'backups\dead_rising_2_pc\environment\safehouse'
$liveRoot = Join-Path $GameRoot 'data\models\environment\safehouse'
$names = @('safehouse.big', 'safehouse_after.big', 'safehouse_breach.big', 'safehouse_laptop.big',
    'safehouse_persistent.big', 'safehouse_poker.big', 'zonelist.big', 'zonelist_safehouse_poker.big')
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'test-four-instances.ps1'), [ref]$null, [ref]$null)
foreach ($name in @('Get-Sha256', 'Stage-StockSafehouse', 'Restore-TestContent')) {
    $definition = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if (-not $definition) { throw "Missing function: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
New-Item -ItemType Directory -Path $originalRoot, $liveRoot, $runRoot -Force | Out-Null
foreach ($name in $names) {
    [IO.File]::WriteAllText((Join-Path $originalRoot $name), "stock:$name")
    [IO.File]::WriteAllText((Join-Path $liveRoot $name), "ported:$name")
}
Stage-StockSafehouse
if ($contentSwap.Count -ne 8) { throw 'Expected eight archived files' }
foreach ($entry in $contentSwap) {
    if ((Get-FileHash -LiteralPath $entry.Live).Hash -ne $entry.StockSha256) { throw 'Stage failed' }
}
Restore-TestContent
if (@($contentSwap | Where-Object { -not $_.Restored }).Count) { throw 'Restore failed' }

# A global weapon archive is also isolated before exercising area transitions.
$stockStreamed = Join-Path $workspace 'backups\dead_rising_2_pc\streamedassets\streamedassets.big.original'
$liveStreamed = Join-Path $GameRoot 'data\streamedassets.big'
New-Item -ItemType Directory -Path (Split-Path -Parent $stockStreamed) -Force | Out-Null
[IO.File]::WriteAllText($stockStreamed, 'stock global archive')
[IO.File]::WriteAllText($liveStreamed, 'preserved port experiment')
$StockStreamedAssetsContent = $true
$runRoot = Join-Path $workspace 'global-run'
Stage-StockSafehouse
if ($contentSwap.Count -ne 9) { throw 'Expected safehouse plus global archive' }
if ([IO.File]::ReadAllText($liveStreamed) -ne 'stock global archive') { throw 'Global archive not staged' }
Restore-TestContent
if ([IO.File]::ReadAllText($liveStreamed) -ne 'preserved port experiment') { throw 'Global experiment not restored' }
if (@($contentSwap | Where-Object { -not $_.Restored }).Count) { throw 'Nine-file restore failed' }

$StockSafehouseContent = $false
$runRoot = Join-Path $workspace 'global-only-run'
Stage-StockSafehouse
if ($contentSwap.Count -ne 1) { throw 'Global-only scope includes unrelated files' }
Restore-TestContent
if ([IO.File]::ReadAllText($liveStreamed) -ne 'preserved port experiment') { throw 'Global-only restore failed' }
$StockSafehouseContent = $true
$StockStreamedAssetsContent = $false

# A concurrent edit must survive cleanup, with the original backup still recoverable.
$runRoot = Join-Path $workspace 'conflict-run'
Stage-StockSafehouse
[IO.File]::WriteAllText($contentSwap[0].Live, 'concurrent edit')
Restore-TestContent
if ($outcome.Status -ne 'content-restore-failed') { throw 'Concurrent edit was not reported' }
if ([IO.File]::ReadAllText($contentSwap[0].Live) -ne 'concurrent edit') { throw 'Concurrent edit overwritten' }
if (@($contentSwap | Where-Object { $_.Restored }).Count -ne 7) { throw 'Unrelated files were not restored' }
'PASS: safehouse/global/combined stage and restore, manifest hashes, concurrent-edit preservation'
"Fixture evidence retained: $workspace"
