# Dead Rising 2 Four-Player Co-op

Experimental native campaign co-op research for Dead Rising 2 PC. The intended mod will let players choose
solo, standard two-player co-op, or four-player co-op. **This is not a playable release yet.**

## Current Status

Latest integration checkpoint: `four_instance_20260927_191316`.

- Four hidden, muted local instances establish native membership and a connection mesh through a local Steam shim.
- The game activates four human actor slots; separate clothing storage avoids the earlier allocation crashes.
- All three remote clients now receive and natively apply the campaign world data.
- All four instances render the safehouse, but the remote clients lose their host connection after loading.
- Stable four-player campaign play, independent input/replication, remote Steam sessions, and the mode selector
  are not verified or finished. A connected lobby or rendered room is not a gameplay pass.

**Next target:** trace and fix the first post-world-application disconnect/transition while preserving the proven
data-transfer behavior. See [current state](4-player-coop/docs/current-state.md),
[world transition](4-player-coop/docs/world-transition-hold.md), and
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
dotnet build tools/dr2-crash-dump-monitor/DR2CrashDumpMonitor.csproj -c Release
```

The native scripts default to VS 2022 Community. Pass `-VcVarsPath` pointing to your installation's
`VC/Auxiliary/Build/vcvars32.bat` for another edition or location. Building does **not** install the DLL or start DR2.
The Python suite has 70 tests and the native suite has seven executable fixtures at this checkpoint.
Optional reverse-engineering tools use `python -m pip install -r requirements-research.txt` and locally supplied
images/symbols; neither the proprietary inputs nor their generated symbol tables are included.

## Game Testing

Read [integration testing](docs/integration-testing.md) before using the harness. It is a development tool,
not an installer or a public multiplayer service. Tests require your own compatible game, save, and content backups.
CI runs offline checks only: no game launches, Steam login, proprietary assets, or automatic releases.

Keep game archives, executables, symbols, saves, captures, dumps, credentials, and generated builds out of Git.
Historical run names and hashes in the research docs identify local evidence, not files shipped by this repository.
