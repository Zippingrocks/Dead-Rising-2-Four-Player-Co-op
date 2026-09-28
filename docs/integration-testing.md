# Local integration testing

The harness simulates local peers through a Steam shim. It does not demonstrate
remote Steam matchmaking or a playable four-player release. The current native
world-transfer control reaches the safehouse but loses its client connections.

## Prerequisites

- Use a normal, non-reparse checkout on D: and run the offline tests first.
- Build the x86 shared runtime and .NET 8 crash monitor using the root README.
- Supply your own compatible DR2 installation and a private copy of a suitable
  campaign save. Set `$GameRoot` and `$SaveFixturePath` to those local paths.
- Preserve the installed `dinput8.dll` separately and record its hash before
  staging the candidate. The harness records the installed DLL hash but does not
  install or restore the DLL itself. That remains the test operator's responsibility.
- For `-StockSafehouseContent`, provide local stock archive backups under
  `backups/dead_rising_2_pc/environment/safehouse/`. The exact eight filenames are
  listed in `Stage-StockSafehouse` inside `test-four-instances.ps1`. The harness
  preserves the currently installed set and verifies content restoration.
- Confirm that no other agent is using the game installation or changing its
  content, runtime, or render settings. Do not use another task's save as a fixture.
- Leave system commit capacity for the instances and the desktop. The harness
  checks a 2 GB desktop reserve plus pending-child allowances. Do not close unrelated
  apps or weaken this guard just to start the test.

A fresh clone deliberately does not include the game, saves, archive backups, or
prebuilt DLL. Building source alone is not sufficient to run this command safely.

## Native world-transfer control

After completing the prerequisites and staging the reviewed candidate:

```powershell
./4-player-coop/tools/test-four-instances.ps1 `
    -Instances 4 -SessionPlayers 4 -DurationSeconds 90 `
    -GameRoot $GameRoot -SaveFixturePath $SaveFixturePath `
    -LobbyProbe -CampaignMenuProbe -CampaignLaunchProbe `
    -LobbyJoinProbe -LobbyClientOnlyProbe -LobbyNativeHostProbe `
    -CampaignAdmissionProbe -ConfirmCampaignAdmission `
    -CrashMonitor -ThreadSnapshots -ActorActivationProbe `
    -HostStateTransferProbe -NativeFlowProbe -NfsOwnershipProbe `
    -MeshListenerProbe -ClothingCapacityProbe `
    -StockSafehouseContent -NetworkSnapshots
```

Instances are hidden and muted, with disposable per-instance saves. Startup and
menu navigation take additional time beyond the observation duration. Do not use
`-KeepRunning` with temporary content. This is a research harness, not a launcher
for normal gameplay.

## Evidence and cleanup

Keep outcome manifests, network summaries, thread snapshots, captures, dumps, and
runtime logs locally under the ignored `runtime_logs/coop/` directory. A completed
observation interval is not a pass. Accepted world chunks alone do not prove
gameplay, and thread stack-address hints are not fully unwound call stacks.

Verify that all eight content archives are restored to their pre-test hashes,
render settings are restored, the original save is unchanged, and all owned child
processes have closed. Restore the prior DLL only after confirming the installed
file is still the staged candidate; preserve any concurrent edit for investigation.
Do not terminate unrelated processes to clean up a run.

The next gameplay gate requires a stable, rendered four-player campaign session.
Only then can independent input, movement, combat, AI ownership, disconnect/rejoin,
and area transitions be meaningfully validated.
