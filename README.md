# Dead Rising 2 Four-Player Co-op

Experimental native campaign co-op research for Dead Rising 2 PC. The intended mod will let players choose
solo, standard two-player co-op, or four-player co-op. **Local four-player campaign gameplay is now proven,
but this is not a public release yet.**

## Current Status

Latest visual-identity checkpoint: `four_instance_20261001_175129`.

- Four local instances complete native frontend admission, retain a connected four-way mesh, and activate four
  independently owned campaign actors in the same room.
- Private keyboard, mouse, camera, and focused-window controller routing let each owner move independently.
- Four-way movement replication, inventory transfer, pause/resume, a native shared area transition, and retained
  post-transition control have passed bounded watched tests.
- Separate clothing storage prevents the original Players 3/4 allocation crash. Actor-local wardrobe calls now
  preserve Players 1/2, give Player 3 a full TIR suit, and give Player 4 a yellow TIR jacket; the four-instance
  visual test passed and all temporary content was restored.
- User-supervised local testing passed four-owner combat, replicated damage, KO, and teammate revive behavior.
  Complete partner HUD behavior, campaign-wide scripting, and production Steam session validation remain unfinished.
- The production runtime is now always four-player when `four_player_coop.ini` is installed. It adds the strict
  `dr2_4p_protocol=3` Steam lobby filter, advertises the same tag through lobby metadata and rich presence, forces a
  four-member limit, and rejects incompatible outgoing or incoming joins. Its default base-game compatibility mode
  reports the four optional skill packs as disabled during the modded process so differing DLC ownership cannot split
  the test group. Vanilla menus are unchanged. Remote Steam behavior still needs live validation.
- An alpha release builder now produces a hash-manifested drag-and-drop ZIP containing one top-level
  `Dead Rising 2` folder. Testers merge that folder into Steam's `steamapps\common` directory without an installer.

**Next target:** validate the production Steam session, protocol filter, and proven gameplay behavior across the
remote four-person test group. See
[current state](4-player-coop/docs/current-state.md),
[gameplay validation](4-player-coop/docs/four-player-gameplay-validation.md), and
[network-file ownership](4-player-coop/docs/nfs-multi-client-ownership.md).

## Repository Layout

| Directory | Purpose |
| --- | --- |
| `4-player-coop/runtime/` | Co-op hooks, native-layout guards, and ownership helpers |
| `4-player-coop/tools/` | Hidden integration harness, read-only probes, reports, and offline tests |
| `4-player-coop/docs/` | Research findings, evidence references, current blockers, and roadmap |
| `tools/case-zero-runtime/` | Existing shared `dinput8.dll` host, input isolation, and disposable-save support |
| `tools/dr2-crash-dump-monitor/` | Authored crash-monitor and WOW64 thread-snapshot source |
| `tools/analyze-minidump-basic.mjs` | Local dump parser used by synthetic regression tests |

The shared runtime keeps its existing directory name to preserve include paths and behavior. This repository
does not contain the Case Zero or Case West content ports. See [repository boundaries](docs/repository-boundaries.md).

## Build And Test

Use Windows and a normal, non-junction checkout on **D:**. Disposable-save validation intentionally rejects C:
and reparse-point directories. Prerequisites: Visual Studio 2022 C++ x86 tools and Windows SDK, Python 3.12,
Node.js with `node --test`, and .NET SDK 8.

From the repository root in PowerShell:

```powershell
python -m unittest discover -s 4-player-coop/tools -p 'test_*.py'
node --test 4-player-coop/tools/test_hang_dump.mjs
./4-player-coop/tools/test-native-abi.ps1
./4-player-coop/tools/test-content-swap.ps1
./tools/case-zero-runtime/build.ps1
./4-player-coop/tools/build-release.ps1
dotnet build tools/dr2-crash-dump-monitor/DR2CrashDumpMonitor.csproj -c Release
```

The native scripts default to VS 2022 Community. Pass `-VcVarsPath` pointing to your installation's
`VC/Auxiliary/Build/vcvars32.bat` for another edition or location. Building does **not** install the DLL or start DR2.
The Python suite has 173 tests, the native suite has 11 executable fixtures, and the Node dump parser has two
regression tests at this checkpoint.
Optional reverse-engineering tools use `python -m pip install -r requirements-research.txt` and locally supplied
images/symbols; neither the proprietary inputs nor their generated symbol tables are included.

## Game Testing

Read [integration testing](docs/integration-testing.md) before using the harness. It is a development tool,
not an installer or a public multiplayer service. Tests require your own compatible game, save, and content backups.
CI runs offline checks only: no game launches, Steam login, proprietary assets, or automatic releases.

Keep game archives, executables, symbols, saves, captures, dumps, credentials, and generated builds out of Git.
Historical run names and hashes in the research docs identify local evidence, not files shipped by this repository.

## Alpha Distribution

`build-release.ps1` compiles the x86 proxy and writes an ignored, redistributable ZIP under
`4-player-coop/builds/releases/`. The ZIP contains only the mod DLL, marker config, hash manifest, version, and
instructions; it never contains the game executable, archives, saves, symbols, or other Capcom content.

Each tester drags the ZIP's `Dead Rising 2` folder into Steam's `steamapps\common` directory while the game is closed,
merges it with the existing game folder, then uses the stock co-op menus. The exact protocol filter keeps this alpha
pool separate from vanilla DR2 and incompatible mod builds, subject to the pending real-Steam validation. Removing the
mod means deleting those two extra files. Steam verification repairs official files but normally leaves extra files.
