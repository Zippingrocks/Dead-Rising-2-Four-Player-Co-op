# Findings

## 1. Vanilla DR2 already has four user-player slots; Off the Record cut them to two (confirmed)

OTR's PDB defines `eUserPlayers` as follows:

```text
USER_PLAYER_1 = 0, USER_PLAYER_2 = 1, MAX_USER_PLAYERS = 2, MAX_USER_PLAYERS_PLUS_NONE = 3
VIRTUAL_USER_PLAYER_FOR_TEMPORARY_PROPS = 2, _FOR_INVENTORY = 3, _FOR_BOSS_WEAPONS = 4
USER_PLAYER_NONE = 2, USER_PLAYER_ALL = 2, MAX_USER_AND_VIRTUAL_ID_SLOTS = 5, NUM_USER_BITS = 2
```

OTR has `IsValidUser(eUserPlayers)` at `0x007F72E0`: `cmp [esp+4], 1; ja` (users 0–1). Vanilla DR2 PC has the same function body at `0x007A1B20`, but with `cmp [esp+4], 3`. It has 17 direct callers. Vanilla therefore has user IDs 0–3, which is what Terror Is Reality uses.

The neighbouring `IsValidUserOrVirtual` checks `<= 4` in both builds, at `0x007A1B30` (vanilla) and `0x007F72F0` (OTR). In vanilla, 4 user IDs plus a virtual range can't all fit below 5, so vanilla's virtual and "none" values must be laid out differently. **Vanilla's exact `eUserPlayers` values have to be recovered before any per-player code is touched.**

OTR removed Terror Is Reality. Its PDB has no game-show classes, while vanilla has `cGameModeGameShow`, `cGameShowFlow`, `cFEToGameShowFlow`, `TIR_PLAYER_ID`, `Player3`/`Player4`, `bar_player3`/`bar_player4`, and `CineContestant1..4`.

## 2. Per-player engine state is indexed by eUserPlayers

Hundreds of OTR functions take `eUserPlayers`. Examples:
- OTR `cGameScene::GetHumanActor(eUserPlayers)` reaches its actor manager at scene offset `0x8C`, then indexes
  the actor-manager pointer array at `+0x0C`. Vanilla's corresponding scene offset is `0x94`.
- `cGameScene::GetPlayerAttributes(eUserPlayers)`: a stride-0x88 array at `[scene+0xA4]`, bounds-checked `< 2` in OTR.
- `cGameScene::SetLocalUserPlayer`.
- Per-user mission events (`cMissionManager::SendEvent`), cinematics (`cCinematicManager::PlayCinematic`), zombie ownership (`cZombieManager::*FreeZombie*`), pause negotiation (`cGameStateManager::SetPauseStatus`, `tNegotiateOperationData`), save data (`cPlayerDataTracker::SetClothingInfo`), and AI join/leave (`cAIManager::OnlinePlayerJoined` / `OnlinePlayerLeft`).

Vanilla's matching player-attribute array is confirmed four-wide, although its record layout differs from OTR. Vanilla
`cGameScene::CreatePlayerAttributes` is at `0x007ACDE0`; it allocates `0x1A0` and constructs four `0x68`-byte records.
OTR's `0x008031A0` allocates `0x110` and constructs two `0x88`-byte records. This means the vanilla engine already owns
four player-state records and they must not be expanded using OTR's stride. Story co-op is capped above this storage.

## 3. Story co-op path (OTR names)

| Area | Symbols |
|---|---|
| Mode | `cGameModeCoop` (vtable shares its slot with `cGameModeSingle` and `cGameModeOvertime`), `DR2Online::IsCoop`, `IsCoopGame`, `IsCoopGameActive`, `gForceCoopFlow` |
| Joining | `cFESynchronizer::EnterCOOPGame`, `RaiseHostConfirmCOOPJoinDialog` ("DlgOnHostConfirmCOOPJoin"), `cP2PServer::StartCoopGameStateTransfer` / `UpdateCoopGameStateTransfer`, `cPlayerDataTracker::SerializeForCoopJoin`, `cAIManager::SerializeForClientJoin` |
| Network groups | OTR enum constant `MAX_NUM_PEERS_PER_GROUP = 2`. The P2P layer is `DR2Online::cP2PServer` / `cP2PClient` |
| Player 2 look | `OUTFIT_COOP_DEFAULT` (Chuck in the co-op outfit), `cActorManager::GetCoopUserPlayer` |
| HUD | `w_coop_health`, `w_coop_pp_icon`, `coop_mini_view`, `cFEIGHudNPC::UpdateCoopPlayer`, `cFEIGOverlay::GetCoopPlayerName` |
| Cooperative triggers | `cOnlineAI::AllPlayersWithinCooperativeTriggerCircle`, `gOnlineCooperativeTriggerRadius` |
| Debug knobs | `cDebugOverride::mOnlineNumPlayers` (vanilla string `online_num_players`), `mNumPlayerHealthBars` |

## 4. Online backend

The game folder has the genuine GFWL `xlive.dll` (15.4 MB) and `steam_api.dll`. The vanilla exe references both `XSessionCreate` and `SteamMatchmaking`. Section 6 confirms Steamworks is the session layer. The user's screenshot shows the co-op menu with Join Online Game / Join Friends.

## 5. Case Zero

Case Zero on Xbox 360 was single-player. Our port runs Case Zero content on DR2 PC's engine (prologue mode), so DR2's co-op code is present. However, Case Zero's missions and cinematics were never authored for a partner. Every co-op fix for Case Zero must be made against DR2's co-op behaviour, not a Case Zero original.

## 6. Online transport and running several instances on one PC (confirmed from the vanilla exe)

**Transport.** Co-op runs over Steamworks. The exe's `steam_api.dll` imports include:
- `SteamMatchmaking` (lobbies) and `SteamNetworking` (P2P packets);
- `SteamUser`, `SteamFriends`, `SteamUtils`, `SteamApps`, `SteamClient`, `SteamRemoteStorage`, `SteamUserStats`;
- `SteamAPI_Init`, `SteamAPI_RestartAppIfNecessary`, `SteamAPI_RunCallbacks`, and the callback registration functions.

`xlive.dll` (genuine GFWL) is imported by ordinal only, 23 ordinals; it isn't the session layer.

**Single-instance guard** (unpacked exe, `0x008CABC2`):

```text
push L"DeadRising2"; push 0; push 0; call CreateMutexW
call GetLastError; cmp eax, 0xB7 (ERROR_ALREADY_EXISTS) / 5 (ACCESS_DENIED) -> 0x008CAE50
0x008CAE50: MessageBoxW(0, L"An instance of this application is already running.", ..., MB_ICONERROR); exit
```

**Harness design** (the user chose multi-instance testing on one PC):
1. The runtime's `CreateMutexW` import hook gives each harness instance its own mutex name.
2. The harness gives direct child launches `SteamAppId=45740` and `SteamGameId=45740`; the relaunch check occurs before
   `dinput8.dll` loads, so it cannot be bypassed reliably from that runtime.
3. Steam won't match a user with itself, so in harness mode the runtime replaces the `SteamMatchmaking` / `SteamNetworking` interfaces with a loopback implementation:
   - a lobby list plus P2P packets over localhost UDP;
   - a distinct fake SteamID per instance;
   - it only needs to cover the methods DR2 calls.
4. `SteamUser()->GetSteamID` / `SteamFriends()->GetPersonaName` are hooked per instance ("Player 1".."Player 4").

## 7. Four-instance capacity harness

`tools/test-four-instances.ps1` performs a boot-only capacity test. It temporarily selects 640x360, disables expensive
render options, launches instances 0-3 directly, minimizes them, gives background clients idle priority and four logical
processors each, and samples memory/CPU before closing them. The original render settings are restored in `finally`.

The runtime recognizes `-coopinstance=0..3` and assigns unique mutexes/logs to instances 1-3. The child-only Steam
environment keeps direct launches in their original processes. This proves process coexistence only.
Steam identity, lobby callbacks, P2P transport, saves and Remote Storage are not isolated yet, so the harness must not
enter a campaign until those gates are implemented.

The shipped executable calls `SteamAPI_RestartAppIfNecessary` before it loads the `dinput8.dll` proxy, so an IAT hook in
that proxy is too late for a bare direct launch. Setting `SteamAppId=45740` and `SteamGameId=45740` in the child-only
environment is the non-destructive solution: a 15-second probe stayed alive under its original PID. The harness applies
those values only while creating its children and restores the caller's environment afterward.

The first concurrent probe kept instances 0 and 1 alive, but instances 2 and 3 faulted at `0x00AB413B`. Both crash
reports and disassembly place that address in Direct3D device setup, after a device-creation call returns an unusable
object. This is not the game mutex: every client log confirms its unique mutex. DR2's render-setting strings expose
`FULLSCREEN_DISABLED`, so the harness explicitly forces all clients into 640x360 windowed mode to avoid competing
full-screen Direct3D devices.

The INI setting alone was insufficient. The Case Zero runtime's existing `IDirect3D9::CreateDevice` diagnostic hook now
rewrites the real presentation parameters in memory for `-coopinstance=` processes: windowed, 640x360, no full-screen
refresh rate and immediate presentation. No ordinary DR2 or Case Zero process is affected.

**Gate 1 passed on 2026-09-24.** Evidence is in
`runtime_logs/coop/four_instance_20260924_161535`. All four processes remained alive for the full sample period:

| Instance | Peak working set | Peak private memory | Sampled CPU time |
|---:|---:|---:|---:|
| 0 | 392.4 MB | 628.1 MB | 47.98 s |
| 1 | 391.9 MB | 627.5 MB | 41.67 s |
| 2 | 392.3 MB | 629.0 MB | 46.70 s |
| 3 | 392.5 MB | 628.2 MB | 51.70 s |

Approximate aggregate peaks were 1.57 GB working set and 2.51 GB private memory. Clients 1-3 ran minimized, at idle
priority, each constrained to four logical processors. Every client log confirms the expected unique mutex. This proves
that three background DR2 clients are practical on the development PC. It does not yet prove a shared lobby or campaign.

To reduce unattended-test overhead further, the harness now hides client windows and passes `-coopfps=15` to clients
and `-coopfps=30` to the host. The runtime paces only `IDirect3DDevice9::Present`; it does not suspend the game or its
network/update threads. This behavior exists only when the harness arguments are present.

**Quiet-background profile passed on 2026-09-24.** Evidence is in
`runtime_logs/coop/four_instance_20260924_162814`. All four processes remained alive, no process requested a visible
window, and the foreground HWND was unchanged before and after the run. Each log records `silent=1`, successful
IXAudio2 interception and `audio: IXAudio2 mastering voice muted result=00000000`. Bink volume is independently forced
to zero when used. Approximate aggregate peaks were 1.51 GB working set and 2.46 GB private memory. Cleanup left no DR2
processes and restored the original 2560x1440 render settings. Ordinary launches are unaffected because all hiding,
dialog suppression, frame pacing and audio muting require explicit harness arguments.

**Identity half of Gate 2 passed on 2026-09-24.** The corrected x86 hooks preserve Steamworks' hidden return-buffer
ABI for `CSteamID`: DR2 expects `GetSteamID` and `GetFriendByIndex` to fill an eight-byte caller-owned object and return
its address in EAX. Evidence in `runtime_logs/coop/four_instance_20260924_163708` shows four stable clients with IDs
`0110000170000000` through `0110000170000003`. Each process reports itself as `Player 1` through `Player 4`, sees exactly
the other three IDs as online friends, and remained alive with the quiet-background profile. Storage isolation is still
required before the harness may enter a campaign.

The runtime now assigns harness instances private Steam Cloud names (`C4P0SAVE.DR2S` through `C4P3SAVE.DR2S`) while
presenting each one to DR2 as `DR2SAVE.DR2S`. This reuses the Case Zero save-namespace wrapper and keeps one owner for
the `SteamRemoteStorage` import. The title-only harness does not access the save, so the mapping is implemented but must
still be validated with a disposable campaign-load test before Gate 2 is declared complete.

**Local peer discovery passed on 2026-09-24.** Evidence is in
`runtime_logs/coop/four_instance_20260924_164208`. A named shared-memory session bus records each process PID, synthetic
Steam ID, persona and heartbeat. The host observed the other instances arrive in order from zero to three peers, and all
four clients converged on three live peers. Stale slots are rejected by checking the owning PID. This is the discovery
foundation for the harness lobby and packet queues; it does not yet replace Steam matchmaking callbacks or P2P methods.

**Local P2P packet routing passed on 2026-09-24.** Evidence is in
`runtime_logs/coop/four_instance_20260924_164420`. The shared bus has bounded, ordered 64 KiB packet queues per recipient,
and harness-mode `ISteamNetworking` slots 0-4 now implement send, availability, read, accept and close for the four local
Steam IDs. A reserved-channel ring test sent `Player 1 -> 2 -> 3 -> 4 -> 1`; every process received and validated its
packet. The test remained at the title screen, so DR2's own gameplay packet stream is not validated yet.

**Synthetic lobby callbacks passed on 2026-09-24.** The harness implements lobby list/create/join, lobby data and
four-member enumeration, including DR2's `CCallResult` callback ABI. All four clients completed the internal lobby
self-test with four members in `runtime_logs/coop/four_instance_20260924_165339`. This validates the local callback
machinery; the next gate is driving DR2's own host and join flows through it.

**Background menu capture works, with an OS focus limitation.** Runtime backbuffer capture and scripted DirectInput
proved one hidden client can navigate Press Start -> Join Co-op Game -> Join Online Game. DR2's unpacked WndProc is
`0x008C4C40`, and its active flag is `0x00E5F619`. Rewriting `WA_INACTIVE` allows a background viewport to continue,
but Windows still permits only one foreground window on the interactive desktop; multiple simultaneously progressing
clients freeze. Non-input isolated desktops do not initialize D3D9. The clients remain suitable for boot, callback and
transport tests, while a complete four-client campaign test needs a stronger headless update path or separate sessions.

The 2026-09-24 follow-up made this limit reproducible. Backbuffer capture remains valid (`S_OK`), while identical PNGs
and nearly flat process CPU time show that the frontend update workers are paused rather than merely loading slowly.
Calling the rating-logo screen's native `animation_done` transition (`0x007E87A0`) from `Present` successfully changes
the screen, but the following asynchronous title load pauses too. A private Win32 desktop is not a workaround on this
machine: DR2 exits before creating D3D9 and reports that the graphics card is unsupported. The harness therefore keeps
the menu probe diagnostic-only; it never foregrounds a test client or treats a frozen loading frame as lobby success.

## 8. Native four-player count and HUD overrides (confirmed)

Vanilla retains the debug controls needed by its four-player TIR engine:

- `cOnlineAI::GetNumActivePlayers` maps from OTR `0x00436340` to vanilla `0x004371C0` by an exact masked code shape.
- `online_num_players` is registered at `0x007BC9FE`; its storage is `cDebugOverride::mOnlineNumPlayers` at
  `0x00DDCAB0`. Retail default is `0`. Five executable readers were found at `0x007BCA12`, `0x0085412B`,
  `0x008541EB`, `0x008864D1` and `0x008865CC`. OTR symbols identify the middle readers as both overloads of
  `DR2Online::cP2PServer::ClientCollectionComplete`; the final two are inside
  `DR2Online::cOnline::BeginDirectP2PGame`. The override therefore governs peer-collection completion and direct-P2P
  game creation, not merely a debug display.
- Both `ClientCollectionComplete` readers are themselves guarded by `online_display_host_status` at `0x00DDCC2F`.
  Enabling that global would activate dozens of unrelated debug paths, so the runtime signature-checks and NOPs only
  the two nine-byte guards at `0x00854120` and `0x008541E0`. The four-player count is now active in host completion
  logic without turning on the broad debug-display system.
- `override_player_health_bars` is stored at `0x00DDCBA5`. Its live reader at `0x005C24C2` returns `3` when enabled,
  exposing the three partner HUD slots required beside the local player.
- `mNumPlayerHealthBars` is at `0x00DDC9F0`, but vanilla only references it in registration; it is dead at runtime and
  is intentionally not changed.

The harness now passes `-coopplayers=4`. The runtime signature-checks these retail paths, changes
`mOnlineNumPlayers` from `0` to `4`, and enables the three-partner HUD boolean only for harness processes. Evidence in
`runtime_logs/coop/four_instance_20260924_182224` shows all four hidden clients applied both overrides and stayed alive.
Aggregate peak working set was 1.57 GB and private memory was 2.38 GB. Cleanup left no game processes and restored the
2560x1440 render settings. Ordinary DR2, Case Zero and Case West launches remain unaffected.

The same runtime probe mapped `cGameScene::CreatePlayerAttributes` and confirmed the four-record layout above. The OTR
signature for `cActorManager::GetCoopUserPlayer` does not match vanilla, even with call and branch operands masked; the
vanilla/TIR implementation was materially rewritten and must be mapped independently before partner spawning is altered.

## 9. Native actor storage and remote-player activation are four-wide (confirmed)

Vanilla's actor path retains the four-player implementation that OTR reduced:

- `cActorManager::GetHumanActor(int)` is `0x00520670`; it indexes the pointer array at manager offset `0x0C`.
- `cActorManager::AddHumanActor` is `0x007A37C0`. It scans exactly four slots (`cmp eax,4`), writes the assigned user ID
  at actor offset `0x3A0`, and recounts all four entries. OTR's matching function at `0x007F91A0` scans only two.
- `cGameScene::AddActor(eRole)` is `0x007ACCA0` and routes human roles into `AddHumanActor`. Its direct call site is a
  test-bed setup path, so normal online joining activates actors through a different route rather than allocating them
  here on demand.
- Vanilla `cHumanActor::EnableRemoteHuman` is `0x00698DA0`, mapped from OTR `0x006D1860`.
- Vanilla `cP2PClient::SinglePlayerToMultiPlayer` is `0x00882C10`. Its participant loop at `0x00882CB0..0x00882CF0`
  has `0x18`-byte records and a `0x60`-byte bound: four records. Every active non-local record fetches its actor through
  the actor manager's `GetHumanActor` accessor (`0x00797BE0`) and calls `EnableRemoteHuman`. OTR's equivalent bound is
  `0x30`, two records. `0x00797BE0` itself is not a `cGameScene` method: it directly indexes `[manager+slot*4+0x0C]`.

Run `four_instance_20260927_101249` proved that invoking this conversion on the harness network worker is invalid.
All four processes faulted at `0x0093BF95`, where a thread-local engine/profiling accessor had returned null. The first
remote actor was enabled before the fault. All four actors shared vtable `0x00C9A720` and enable method `0x006EB930`,
so this was thread affinity rather than a malformed Player 3/4 actor.

Run `four_instance_20260927_101643` dispatches the unmodified conversion through DR2's window/game thread. All four
calls returned without faults. On each process, `singlePlayer` became zero, the local actor remained local, and all
three nonlocal actors reported `remoteEnabled=1`. The exact runtime is preserved in
`builds/four_actor_activation_20260927`.

## 10. Frontend save selection and host launch (confirmed visually)

Run `four_instance_20260927_101920` drove two hidden clients through Press Start, Join Co-op Game, Join Online Game,
the native save-progression warning, and its confirmation. Capture `coop_capture.0.7.png` contains `SLOT 1` through
`SLOT 3`, but these are local gamer-profile/save slots, not partner slots. This correction matters: the capture proves
frontend navigation and save selection only. It does not prove lobby discovery or a three-partner UI.

Run `four_instance_20260927_102336` combined the proven direct four-member session, successful four-actor activation,
and real frontend progression. When only the host selected `START GAME`, DR2 opened its stock `Incoming co-op call`
save/profile screen. The other three processes remained on the main menu, every endpoint stayed connected, and no
process faulted. This proves the host receives an incoming-call route while the session is live, but the three save
rows do not establish how many remote members the frontend has accepted.

Run `four_instance_20260927_102614` accepted Slot 1. The host entered the stock opening cinematic (`CAPCOM presents`,
then `A Blue Castle Games Development`) while all three clients remained at the main menu. The native session and
four-actor activation stayed valid throughout. This confirms host campaign launch and isolates the next requirement:
drive each client through `cFESynchronizer`'s lobby/join path so it consumes the host's campaign state.

Run `four_instance_20260927_103000` then kept that host campaign running while only Players 2-4 followed the native
Join Co-op path. All three independently reached their gamer-profile/save-slot picker and retained healthy native
links and actor states. A further slot confirmation is required before any lobby or state-transfer conclusion.

Run `four_instance_20260927_103424` confirmed the disposable slot on all three joiners. Each displayed
`SEARCHING FOR GAME...`, but none issued a new `RequestLobbyList`; their pre-existing direct P2P client state blocks
the frontend from starting another online operation. This is stable negative evidence, not a lobby failure. The next
control advertises the loopback lobby without constructing the direct P2P session and lets only Player 2 search.

Run `four_instance_20260927_103919` performed that control. Player 2's frontend repeatedly issued
`RequestLobbyList`, registered its real call-result object, received count 1, and called `GetLobbyByIndex(0)`, which
returned `0184000070000001`. It rejected the result and repeated the search, showing that callback ABI and discovery
are working while required lobby metadata is absent. Distinct `GetLobbyData` keys are now traced once so the synthetic
host can reproduce the exact retail contract.

This changes the implementation target materially: players 3 and 4 do not require a new actor container or a custom
spawn path. The primary remaining work is getting four real participant records through lobby/join state transfer and
making every two-player story synchronization site honor them.

The runtime-unpacked vanilla image is preserved at `4-player-coop/research/dr2_pc_unpacked_image.bin` (11,411,456
bytes, SHA-256 `D75B9CF23AD3869D18AA6AE4314235C8DD86791EE9F908445EF1F129062707C6`). `tools/pe_va_probe.py --mapped`
supports VA disassembly, absolute xrefs, relative-call xrefs, string lookup and wildcard byte patterns against it, so
most follow-up mapping no longer requires launching the game.

**Latest regression:** `runtime_logs/coop/four_instance_20260924_191801` kept all four stock-frontend harness clients
alive, silent and hidden. Every process applied `mOnlineNumPlayers=4` and the three-partner HUD override; all converged
on three live peers. Aggregate peak working set was 1.53 GB and private memory was 2.34 GB. Cleanup left no DR2 process
and restored the original 2560x1440 render settings.

The narrow client-collection guard patch passed four clients in
`runtime_logs/coop/four_instance_20260924_193108`. Every process logged both completion paths enabled, four requested
players and three live peers. Aggregate peak working set was 1.55 GB and private memory was 2.35 GB.

## 10. Host client collection is dynamically sized (confirmed)

Vanilla's two mapped `cP2PServer::ClientCollectionComplete` overloads begin at `0x00854090` and `0x00854150`. Both:

- read a runtime client count from `[server+0x58]+0x48` rather than comparing against two;
- walk `cP2PServer::tClientData` records at `[server+0x54]` with a `0x48` stride;
- test per-flow bytes beginning at record offset `0x12` and per-sync bytes beginning at `0x1C`;
- compare the collected count against `mOnlineNumPlayers` after the narrow guard described above.

The OTR PDB independently defines the same `0x48`-byte client record with `mConfirmedPlayer[4]`. This is further
evidence that the host completion machinery does not need expanded storage for four players.

Native construction is now confirmed too. Vanilla `cP2PServer::AllocClientData` is `0x00865300`; both of its callers
pass the requested count through to an allocation of `count * 0x48`. A harness-only direct-host probe calls vanilla
`cP2P::InitAsServer` at `0x0087E320`, reuses the `cLocalServer` already created by the first-party host setup, and allows
the server to continue past first-party-only readiness and empty-hardware-message checks. In
`runtime_logs/coop/four_instance_20260924_200500`, the real vanilla host called `AllocClientData(count=4)`, returned
success from `InitAsServer`, and retained four native records with slot indices `0..3`. This is a diagnostic bypass,
not a production replacement for matchmaking or transport.

All four records initially contain zero peer IDs and are unconfirmed. The next unresolved edge is therefore narrowed
to native client connection, record population and confirmation before join state transfer. OTR shows the likely entry
as `cP2PClient::Connect(const Online::tIPAddress&, ushort, bool)` at `0x008E5D30`. Its vanilla counterpart is now mapped
at `0x0087C800`: it has the expected three stack arguments, returns with `ret 0x0C`, and is called by
`cOnline::BeginDirectP2PGame` at `0x00886613`. The incomplete first-party session calls it with an empty address, so the
next probe supplies Player 1's synthetic Steam endpoint and checks whether the legacy socket API advances the native
client. Route any resulting socket traffic through the local bus and verify that players 2-4 transition the host records
from empty to identified and confirmed.

The focused two-process probes narrowed that transition further:

- `runtime_logs/coop/four_instance_20260924_201241`: `BeginDirectP2PGame(..., false)` constructs the native client and
  topology but its incomplete first-party state never opens a peer socket.
- `runtime_logs/coop/four_instance_20260924_201756`: the first native `Connect` call receives the sentinel endpoint
  `0x10`, not zero, and creates a `cRemoteServer` while leaving `cP2PClient::mClient` null.
- `runtime_logs/coop/four_instance_20260924_202402`: vanilla `cP2PClient::Update` is virtual slot 6 at `0x0087C6F0`.
  A bounded 180-tick pump runs cleanly but does not open the lower transport socket by itself.
- `runtime_logs/coop/four_instance_20260924_202601`: the matching outer `cOnline` update is virtual slot 3 at
  `0x00880970`. It advances the first-party checks, enumerates the synthetic friends, then removes the remote server
  before any `CreateP2PConnectionSocket` call. This proves the current blocker is remote-server/link eligibility or
  first-party membership state, not packet routing yet.

The teardown path is now mapped and instrumented:

- OTR's public symbols identify the remote-server lifecycle vtable methods; vanilla's corresponding contiguous vtable
  block is at `0x00CBC840`. The verified absolute block indices are `Die=10`, `HandleLinkConnected=13`,
  `HandleLinkShutdown=14` and `Shutdown=18`. Vanilla's `Shutdown` additionally receives the affected node pointer.
- `runtime_logs/coop/four_instance_20260924_204513` shows the real sequence. At update tick 91,
  `cStarTopology::HandleEventLinkShutdown` (`0x008629F0`) receives the remote server itself as the shutdown node. Its
  branch at `0x00862A02..0x00862A1A` sees role bit `0x10`, calls remote `Shutdown`, which calls `Die`, clears the link,
  and finally clears `cStarTopology::mServer`. This is deterministic and not the watcher closing the game.
- A controlled suppression run (`four_instance_20260924_204936`) retained the remote server through all 180 updates
  but neither created `mClient` nor requested `CreateP2PConnectionSocket`. The teardown is therefore a symptom, not
  the missing connection step.
- Vanilla `cP2PClient::Connect` builds the link init record at `0x0087C90E..0x0087C980`. Its first two bytes are
  `mIsP2P` and `mBypassFirstParty`; the latter comes from global `0x00DDCC2F`. The opt-in direct probe now enables this
  engine-owned bypass. `four_instance_20260924_205138` proves the flag reaches the path but does not alone open the
  peer socket.
- The exact `cLinkManager::GetOrCreate` call is `0x0087B3F3` -> `0x00872620`. The transparent trace in
  `four_instance_20260924_205410` records `isP2P=1`, `bypassFirstParty=1`, the complete synthetic host Steam ID
  (`0x0110000170000000`) in the two address words, transport type `6`, a successful return, and a real native `cLink`
  pointer. About 1.7 seconds later the same early link-shutdown event occurs without a legacy connection-socket call.

The native link and first-party transition are now mapped substantially further:

- `four_instance_20260924_211314` shows `cConnection::InitLink` succeeds and creates the synthetic host peer, but leaves
  it in first-party state `4`, `ready=0`. That incomplete asynchronous state is the cause of the early teardown.
- In the direct-probe-only completion experiment, changing the peer to DR2's connected state `6`, `ready=1` prevents
  teardown and keeps the native `cRemoteServer` alive. This is diagnostic proof, not the final callback implementation.
- `cLink::Update` uses an exact 3.0-second readiness delay. A 240-tick pump reaches the post-ready event, changes the
  link's `initResult` byte from zero to one and preserves the remote server (`four_instance_20260924_212231`).
- The connection owns a first-party child at `+0x100` and transport child at `+0x104`. The transport has a real backend
  at `+0x8C`; native `cTransport::Activate` is `0x008784D0` and advances transport state `1 -> 2` after initializing
  that backend.
- The post-ready event is encoded by `0x00878600` and enqueued through `0x0086A430`. Evidence
  `four_instance_20260924_212953` proves the backend outbound queue changes from count `0` to `1`.
- The generic backend dequeue is `0x0086A680`, reached from backend operation switch `0x0087EC40`. The missing owner
  service is reproduced cleanly by calling full `cConnection::Update`, virtual slot 9 at `0x0088AAF0`, once per probe
  tick. In `four_instance_20260924_220233`, every post-ready queue write is followed immediately by the matching read
  cursor advance (`0->1`, `1->2`, `2->3`) on both instances. This closes the previously suspected queue-scheduler gap.
- Extending that update pump to 720 ticks/12 seconds remains stable but leaves the client link endpoint null and its
  node count zero. Timing is therefore not the missing ingredient.
- `cOnline::Update` at `0x00880970` guards one network phase behind global `0x00DDEA04`. A controlled probe proved this
  global is an object pointer, not a boolean; writing `1` causes the expected access violation. That probe was removed
  and the stable queue-consumer path does not depend on it.

The next boundary is now the host roster rather than transport servicing. `cP2P::InitAsServer` at `0x0087E320` takes
session capacity, owner ID, match ID, visibility and hardware metadata; the harness's leading value `4` is confirmed as
capacity. Its native platform session allocates four member objects, but reports zero members because the harness has no
real Steam lobby feeding it. Native roster parsing stores member count at `platform+0x48`, four member pointers at
`platform+0x54 + slot*0x18`, and the Steam ID at `member+0x08`. Join state byte `1` is dispatched by `0x008618B0`, while
state byte `3` is the leave path at `0x00861970`.

Vanilla's `cP2PServer` event handler is `0x00874F20`. Event type `9` is member-join: it claims the next empty native
`0x48` record, copies the member Steam ID, stores its slot metadata and marks that record's own confirmation byte.
Event type `10` is leave. The harness fills the already-constructed platform member objects and feeds type-9 events
through this real handler; it does not manually manufacture or resize host records.

This four-member bootstrap is dynamically confirmed in `runtime_logs/coop/four_instance_20260924_223826`. The host
waited until all four background identities were live, accepted four native join events, and produced:

- slot 0: `0x0110000170000000`, confirmation row `1,0,0,0`;
- slot 1: `0x0110000170000001`, confirmation row `0,1,0,0`;
- slot 2: `0x0110000170000002`, confirmation row `0,0,1,0`;
- slot 3: `0x0110000170000003`, confirmation row `0,0,0,1`.

All four processes stayed alive. Peak aggregate working set was 1.51 GB and private memory was 2.31 GB. This is the
first proof that vanilla DR2's campaign host can hold four distinct identities simultaneously through its native join
path. It is still a diagnostic bootstrap: each record remains globally unconfirmed, client link endpoints remain null,
and no real DR2 gameplay packet has crossed the loopback transport yet. A control run that applied only the native
first-party ready callback (`state 4`, `ready 1`) tore the client down at tick 90, proving state 4 must receive and parse
the host handshake before it can legitimately become state 6. The immediate target is host link creation and the
type-9 join follow-up that emits that handshake, followed by removal of the state-6 diagnostic shortcut.

### Rendering-enhancement confound audit

### Native reliable handshake correction (2026-09-25)

The object at `cConnection+0x100` is not a first-party child. Its vtable is `0x00CB8E74` and its layout matches OTR's
`DR2Online::cReliableLayer`: state at `+0x0C`, socket at `+0x78`, endpoint count at `+0x7C`, and three `cEndPoint2*`
slots at `+0x80`. The earlier direct-probe shortcut wrote `cEndPoint2.mState` at `+0x98` from 4 to 6 and
`mHasRecvedRebind`/ready at `+0x9C` to one. It therefore bypassed the reliable handshake; it was removed.

Vanilla's recovered native lifecycle is now:

- `cReliableLayer::Listen` `0x008888F0`, `Connect` `0x008889D0`, and `Update` `0x00888AB0`;
- connection wrappers `0x00889C00` (listen) and `0x00889C30` (connect);
- `cEndPoint2::Connect` `0x008790B0` and `Update` `0x00886060`;
- endpoint state 2 is free, 3 listen, 4 connecting, 5 accepting, 6 connected, and 8 error.

The packet-audited hidden run proves a connecting endpoint queues one retransmit and emits DR2's real 22-byte connect
packet on Steam channel 5679 about every 100 ms. A synthetic `P2PSessionRequest_t` callback (Steam callback 1202) makes
DR2 itself call `AcceptP2PSessionWithUser`. After adding a shared probe-ready barrier, Player 1 successfully called
`IsP2PPacketAvailable` and `ReadP2PPacket` twice for Player 2's authentic packet. This is the first native DR2 gameplay
transport traffic to cross the loopback bus.

The symmetric-connector experiment is intentionally diagnostic: both peers in state 4 exchange connect packets, and
the client-side role faults when it receives the host connector packet. The next implementation target is the proper
role split: initialize Player 1 through `cReliableLayer::Listen` with the server's valid `IQueryConnect`, keep Players
2-4 on `Connect`, then verify state transitions `3 -> 5 -> 6` (host) and `4 -> 6` (clients). The callback must not be
marked delivered until at least one callback object has registered; bus version 3 and `directProbeReady` now enforce
that startup ordering.

### Native listener and multi-peer handshake (2026-09-25)

The host role split now works. The connection object's copied vtable was not a reliable source for its inherited
`Listen` method: slot 17 resolved to `0x0087F560` and returned the invalid value 64. The runtime now validates the
retail signature and calls the native connection wrapper at `0x00889C00` directly. In
`runtime_logs/coop/four_instance_20260925_134119`, Player 1 entered listener state 3, accepted Player 2 through state 5
to state 6, and Player 2 advanced from state 4 to state 6. Both sides exchanged native 14-byte and 135-byte packets and
remained alive. This is the first symmetric, non-forced DR2 reliable connection over the local transport.

The four-instance run in `runtime_logs/coop/four_instance_20260925_134316` proved all three clients can independently
reach state 6 against one host. The host simultaneously retained the listener and two remote endpoints (`mNumEndPoints`
reached 3); the third remote was subsequently displaced. This is a native capacity boundary, not a Steam-bus failure.

Unpacked disassembly explains the boundary precisely. `cReliableLayer` embeds three endpoint pointers at `+0x80`, and
literal three-entry loops exist at `0x0084BDA5`, `0x0084BDE6`, `0x00888B45`, `0x00888BAF`, and `0x00888C4F`.
`cEndPointManager2`, however, preconstructs four endpoint objects at `0x008886C5`; the allocator at `0x00888700` can
therefore supply the required listener plus three remote endpoints. A fourth pointer would occupy `+0x8C`, currently
bandwidth-measurement slot zero. The next controlled experiment is to expand the literal loops to four while preventing
the bandwidth event path at `0x00888BF0` from writing across the repurposed endpoint slot and adjacent state.

The first expansion experiment also established an ordering constraint. Applying the four-entry loops before
`cP2P::InitAsServer` makes construction of the server's second reliable-layer object fault after client-table allocation:
its stock bandwidth value at `+0x8C` is observed as a fourth endpoint before that object has been converted. The runtime
therefore keeps the patch probe host-only and delays it until `InitAsServer` has returned, then clears `+0x8C` on the
already-live listener. The patch replaces the reliable reset's bandwidth-zero write with an equal-length fourth-slot
null write and suppresses the per-endpoint bandwidth event write that would overwrite either slot four or
`mCurrRecvEndPointIdx`. This delayed variant is built and staged but not yet handshake-validated: the final three hidden
attempts all failed in `Direct3DCreate9::CreateDevice` before the delayed network probe ran.

### Reliable-layer storage boundary and relocation plan (2026-09-25)

Subsequent hidden runs closed the ambiguity around the fourth slot. Runs
`four_instance_20260925_143721` and `four_instance_20260925_144333` kept all four processes alive and showed the host's
expanded endpoint manager using four of five objects: one connection listener, one server listener and two connected
remote endpoints. All three clients can independently reach endpoint state 6, but the third remote still replaces a
previous remote because the reliable layer retains only three pointers.

OTR PDB symbols identified vanilla `cReliableLayer::Accept` at `0x0087F880`; its stock free-slot loop ends at
`0x0087F8CF`. Expanding that compare alone is necessary but not sufficient. Two packet address/port lookup loops at
`0x0087FA61` and `0x0087FBD1`, a pointer lookup at `0x0087FCD6`, a bandwidth scan at `0x0087FD4D`, and the reliable
update loop at `0x00889D34` are also three-wide. A controlled build expanded this family and produced an immediate,
fully explained fault in `four_instance_20260925_144944`: `+0x8C` contained `0xC7F12000` and was dereferenced as an
endpoint at `0x00880467`.

The OTR type records prove why. `DR2Online::cReliableLayer` is a separately allocated `0xA8` object with this tail:

- `+0x7C`: endpoint count;
- `+0x80`: `cEndPoint2* mEndPoints[3]` (12 bytes);
- `+0x8C`: `tBandwidthMeasurement mBandwidthMeasurements[3]` (12 bytes); `0xC7F12000` is its initialized sentinel;
- `+0x98`: current receive endpoint index;
- `+0x9C`: listener handle;
- `+0xA0`: 64-bit current-time value.

Vanilla allocates that object with `push 0xA8` at `0x0087F796`, calls its constructor at `0x0087F7B3`, and stores the
returned pointer at `cConnectionGameClient+0x100`. This is good news: the reliable object is heap allocated, not embedded
in the connection. The correct four-peer implementation is therefore a `0xB0` reliable object with four endpoint
pointers, four bandwidth samples, and the remaining tail shifted by eight bytes. It must patch the allocation,
constructor/initialization, every reliable-layer tail-field access and all verified loops as one atomic layout change.
The unsafe expanded-loop experiment was removed and the staged DLL was rebuilt from the prior stable patch set (SHA-256
`42722B4F5DA7D3772C5E71E73732901E0096E917233CF3CBBBF01FD5BCDA5C14`).

The harness also now retries HAL `CreateDevice` in bounded intervals and can fall back to D3D9 NULLREF only in harness
mode. A one-second launch stagger remains the most reliable way to obtain four hardware devices without bringing any
game window to the foreground.

The project does contain an accepted `500/500` maximum-visibility and fade/LOD rule for converted Case West Storage
Pens static placements (Test280). It is encoded in those converted map assets, not applied globally to DR2's renderer.
The 2026-09-24 live audit found no Steam launch options for app 45740, no separate renderer/LOD injection DLL, and no
recent vanilla `data` replacement outside the additive `data/case_zero` and `data/case_zero_launcher` trees. The capacity
harness explicitly selected `vanilla_dr2` and never entered a world map, so Test280 and Case Zero residency/draw-distance
work did not inflate the four-process boot result. Future gameplay benchmarks must record the selected campaign/map and
visibility policy because a fully resident Case Zero or 500/500 Case West area will cost more than a vanilla title boot.

### Four-endpoint and host-link boundary (2026-09-25 evening, interpretation withdrawn)

The event-routing and object-registration conclusions below were disproven by the subsequent disassembly audit.
Keep these paragraphs only as experiment history; do not reproduce their routing or registration code.

The hidden harness now expands vanilla's reliable layer to four endpoint slots and its endpoint manager to five
objects (one listener plus three remote peers). The host reliably reaches `count=4` with listener state `3` and all
three remote endpoints in state `6`. The shared-memory Steam P2P replacement uses 8,192 packet slots per player and
2,048-byte payloads; observed native packets top out at 1,264 bytes, and no queue-send failures occur. The host's
receive-drain wrapper consumes queued packets without retransmitting endpoints during extra passes.

The native four-member roster creates four `cLink` records, but only Player 2's link receives the normal higher-level
initialization path. `cLinkManager::GetOrCreate` returns false but supplies valid existing links for Players 3 and 4.
Calling `cLink::Init` directly on those server-side roster links is invalid: it returns false and causes the connection
service to remove the associated endpoint. Do not repeat that experiment.

An endpoint-scoped trampoline at the `cEndPoint2::ProcessPacket` call (`0x00889ED0` -> `0x00888DD0`) proved decoded
events can be attributed to their exact Steam peer and routed to the matching host link. Unbounded rerouting grows the
dormant links' event counters beyond the normal ten-entry behavior and backpressures host receives. Limiting each extra
link to ten events prevents that flood but all clients still time out after roughly 30 seconds of active networking.
Therefore event attribution is solved, but event registration/servicing is not.

The unpacked caller at `0x0087B3D0` identifies the missing native follow-up. After `GetOrCreate` at `0x0087B3F3`, it
checks the returned link pointer, allocates a `0xE8` event object through `0x00A2BD40`, constructs it at `0x00871620`,
and registers it through `0x008504D0`; the resulting object is stored at owner `+0x7C`. The next target is to recover
the owner object and reproduce this per-link registration for the additional server roster links, rather than calling
`cLink::Init` or forwarding decoded events into unregistered links. Full-duration validation must measure active
networking time after the harness's 20-24 second startup barrier; process duration alone previously hid the timeout.

### Rejected four-peer transport experiment (2026-09-25 night)

**Correction:** the apparent stable result described below was not reproducible. Stress run
`four_instance_20260925_193057` left all processes alive but all three clients entered endpoint ERROR state 8.
The directory named `builds/stable_four_peer_transport_20260925` is historical evidence, NOT a known-good build.
The claim about deferred event-source retention is false: the hook intercepted outgoing Send, not receive dispatch.

Live xref scanning of the unpacked process found the sole caller of `0x0087B3D0` at `0x0087C980`. Its owner is an
eight-slot event source: the vanilla helper stores one primary peer object at owner `+0x7C`, but registration through
`0x008504D0` accepts up to eight objects. The runtime now captures that owner, allocates a native `0xE8` peer object for
each additional host link through `0x00A2BD40`, constructs it with `0x00871620`, and registers it in an unused listener
slot. In the validating run, Players 3 and 4 occupied slots 1 and 2 and both links received `cLink::Update` every frame.

`cEndPoint2::ProcessPacket` defers decoded link-event dispatch until after the function returns. Consequently, clearing
the endpoint source at function return routes no extra-peer events. Retaining the endpoint source through the deferred
batch is correct for the engine's observed queue ordering. The full run in
`runtime_logs/coop/four_instance_20260925_192837` produced 331 Player 3 and 543 Player 4 host reroutes, sustained native
P2P reads and writes on all four processes, zero state-8 endpoint timeouts, zero exceptions, and four surviving
processes. This is the first validated stable one-host/three-client native transport result.

The next boundary is no longer endpoint or transport capacity. It is promotion of the stable four-peer session into
the game's campaign/player layer: confirm the server's four peer objects are represented in session membership, then
expand player allocation, active-player queries, HUD slots, and transition ownership while preserving vanilla one- and
two-player behavior.

### Native host reconstruction audit (2026-09-25, supersedes previous transport claims)

Binary matching against OTR symbols and the vanilla mapped image established:

- `0x00887010` is `cConnLink2::Send(Packet::tBase*)`, returning bool. Its caller-selected link must not be
  changed using the most recently received peer, and its AL result must survive tracing. The runtime now preserves both.
- `0x0087B3D0` is the remote-server overload of `cStarTopology::Host`. Its `0xE8` object constructed at
  `0x00871620` is a `cRemoteServer`, not a generic per-player event object.
- The local-server overload at `0x00862800` allocates `0x368`, calls `0x00861E90`, and produces vtable `0x00CB9F28`.
  Reusing a remote-server pointer (vtable `0x00CBC840`) as that object caused local-only fields such as `+0x324`
  to be accessed outside the remote allocation. Extra fabricated remote nodes and roster seeding are disabled.
- Host `BeginDirectP2PGame` was taking its client connector path because its synthetic first-party address did not
  identify the host. The harness now defers that connector, then invokes native `InitAsServer` on an empty topology.
  It no longer bypasses the hardware-message initialization result.
- Control `four_instance_20260925_194134` proves correct local-server construction, native initialization success,
  and four native client records without fabricated roster writes. It does NOT prove a joined session.
- The host also needs its real local client. Native `cP2PClient::Connect` detects an existing topology server and calls
  `0x00873160` to construct/join the local client. Its `+0x90` is that local node; `+0xB4` is not.
- A link-manager game connection is created lazily by the prefix at `0x008723F0` and initialized at `0x00863C30`.
  Manually creating a second listener (`four_instance_20260925_194606`) left the client in connecting state 4 for
  approximately 58 seconds. That is not a successful handshake; the native local-client join is the next control.

The direct probe now assigns one thread to `cOnline::Update`, preventing the harness and frontend from updating
the same online state concurrently. This is harness-only and is not a shipping campaign-update implementation.
Periodic endpoint snapshots and `tools/report_harness.py` distinguish process survival, failed connections,
unproven connections, last-observed connected endpoints, and confirmed roster slots. No report currently certifies
four-player campaign gameplay. Run its focused tests with:

```powershell
python -B -m unittest discover -s 4-player-coop/tools -p test_report_harness.py -v
```

### Native campaign admission and receive ownership (2026-09-25, ongoing)

The OTR game-type enum is NOT interchangeable with vanilla DR2. In vanilla, matchmaker `+0x20`
uses TIR=0, COOP=1, and unset=2. Our synthetic matchmaker remained unset, so the native slot
policy rejected even the host's local client. `ConfigureHarnessCampaign` now checks the native
matchmaker getter (`cUser` vtable slot 33, `0x0084E060`) and assigns the campaign type and host
flag. Both `BeginDirectP2PGame` and `InitAsServer` receive COOP=1.

Control `four_instance_20260925_195223` produced native acceptance of the local host and Player 2,
with both records confirmed and confirmations `1,1,0,0`. The physical endpoint remained state 6
for approximately 43 seconds of observed probe activity. There were no synthetic roster writes.
This is two-member native session evidence, not four-player gameplay.

Three verified campaign-only admission instructions retain the two-player policy:

- `0x0085119E`: `cmp edi,2` in `cLocalServer::IsFull`.
- `0x008623CC`: `lea eax,[edx+2]` in public-slot accounting.
- `0x008623F8`: `lea eax,[ecx+2]` in private-slot accounting.

The direct harness validates all three signatures before changing their constants to four.
Four-instance run `four_instance_20260925_195421` still confirmed only host and Player 2;
Players 3 and 4 stayed in connecting state 4 for approximately 87 seconds. No process crash
does not mean a successful four-member session.

The current receive experiment addresses the shared Steam channel: native `cReliableLayer::Update`
drains the socket even when none of its endpoints accepts a packet. The established connection
can therefore consume another peer's join packet before the join listener sees it. A wrapper
at `0x0088A9C1` scopes the currently updating reliable layer; the loopback queue uses native
`cEndPoint2::CanProcessPacket` (`0x0085A0B0`) before removing a packet. This changes only the
harness receive queue, not outgoing destinations, endpoint updates, or server records.

Startup failures in `four_instance_20260925_195721` prevented this experiment from being evaluated.
Run `four_instance_20260925_200137` also exited before the probe, with access violation exit codes
on both children. Memory pressure was present earlier but is not an established cause of these
access violations. The harness now records memory headroom and pre-cleanup exit codes, refuses
launches without 2 GB reserve plus 700 MB per planned child, stops on early exits/low memory,
and optionally attaches the existing crash debugger (`-CrashMonitor`). All evidence stays on D:.

Two-instance filtered control `four_instance_20260925_200315` completed with both native members confirmed,
fresh connected endpoints, and roughly 67 seconds of probe activity. Four-instance runs `200517` (128 seconds)
and `200822` (78 seconds) admitted only the first arriving remote. Therefore the receive-filter theory alone
did not explain the admission failure.

The callback audit found another ABI error: P2PSessionRequest callback vtable `0x00CCAEF0` slot 0
(`0x008D25B0`) takes payload, IO-failure, and API-call arguments and returns with `ret 0x10`.
Its slot 1 (`0x008D25E0`) tail-calls the one-payload member. Calling slot 0 with one argument
unbalanced the stack by 12 bytes per notification. The former outer register/stack fence did
not make this correct. `steam_callback_abi.h` now dispatches the correct overload; the fence
has been removed. `tools/test-native-abi.ps1` exercises 2,000 x86 calls with runtime stack checks.
The correction removed observed corrupted callback peer IDs, but did not by itself admit all peers.

Transparent `cLinkManager::CanListen` telemetry in `four_instance_20260925_201107` identified the
actual current rejection: after accepting one remote, `full=1`, `duplicate=0`, `reallyConnected=0`,
and `limit=1`. The limit is `cLinkManager+0xF4` (or `IQueryConnect+0xE4`), not the server record count.
At `0x0087C851`, `cP2PClient::Connect` loads EBX=1. The campaign branch at `0x0087C8C7` compares
matchmaker type against EBX, then writes that one to the link manager. The other branch at
`0x0087C8D4` writes three, as used by TIR. The current harness validates the full branch bytes
and changes `JNE` at `0x0087C8CA` to `JMP`, using the existing three-remote path. IsFull, duplicate
checks, native acceptance, and confirmations remain active. Validation is in progress; no
four-player gameplay claim follows from this patch alone.

### Confirmed four-member native handshake (2026-09-25, run 201328)

`four_instance_20260925_201328` completed with all four native server records confirmed and all
four confirmation rows equal to `1,1,1,1`. Arrival order was instances 2, 1, 3, occupying native
slots 1, 2, 3 respectively; slot number must not be treated as harness instance number. The host
retained three connected remote endpoints, and all three clients retained a connected host endpoint.
The final endpoint samples were under one second old, no endpoint errors or trace faults were
reported, and approximately 107 seconds of active probe logs were retained. All four processes
survived until intentional harness cleanup. This passes the native handshake gate only.

Snapshot: `builds/native_handshake_filtered_20260925`. It includes the exact DLL and runtime sources.
The next controls remove the receive-filter experiment and then use the original transport layouts.
The real topology has one separate listener plus three remote endpoints, so four reliable slots
and five manager objects may have been needed only by the previously incorrect topology. Their
necessity must be re-tested rather than assumed from those rejected experiments.

The harness deliberately closes only its own children at the observation deadline, including a
successful run. A game window disappearing at that deadline is not evidence of a crash. Its
`outcome.json` records whether each child had already exited and its exit code before cleanup.

### Stock lobby metadata acceptance (2026-09-27)

The local Steam lobby provider now exposes the full vanilla metadata set. Reverse engineering the writer and parser
established decimal numeric values, lowercase-hex `HostId`, decimal `hostSteamID`, and zero initial filled-slot counts.
Instrumentation at the stock candidate parser call `0x008D117C` then exposed the decisive frontend predicate: Join
Co-op expected `GameMode=1` and `RankedMatch=0`. The earlier synthetic lobby used `GameMode=0`, so DR2 correctly found
it and rejected it before `JoinLobby`. The corrected value is staged, but renderer-unavailable controls `110443`,
`110524`, and `110818` never exercised it. A clean run must show candidate compatibility and a frontend-originated
`JoinLobby` before this boundary is considered passed.

### Real four-member lobby and campaign-admission boundary (2026-09-27)

Run `four_instance_20260927_113226` passed the renderer-backed frontend test with four processes. Players 2-4 each
discovered the synthetic lobby, passed the stock metadata parser, issued `JoinLobby`, and constructed DR2's native
client topology. Player 1 retained four confirmed native records and three connected remote endpoints. Every process
reported multiplayer mode, its own local slot from 0 through 3, and three remote-enabled campaign actors. This is the
first real four-member frontend/native-session proof.

Starting a campaign does not automatically dismiss the joiners' search modal. Run `113752` showed Player 1 in the
opening campaign with the stock incoming-call banner while all three connected clients remained searching. The game
data maps call answering to `COMMAND_AI_INTERACT_WITH_PHONE`: keyboard C, controller D-pad right. A long C pulse opens
the Friends screen through `COMMAND_FRONTEND_RIGHT_HELD`; a 100 ms C pulse clears the incoming-call banner and causes
fresh stock traffic. With an isolated copy of the user's post-transceiver save, run `120735` reached that state without
touching the original save. The client still did not leave its search frontend, and its flow-command queue advanced
without being consumed. The next reverse-engineering target is the stock client completion/travel handoff after host
admission, not transport, lobby discovery, actor allocation, or host input.

### Native campaign transition chain (2026-09-27)

The OTR PDB plus instruction-shape matching resolved the vanilla PC transition family:

- `cP2PClient::StartGame`: `0x00889850`
- `cP2PClient::ProcessFlow`: `0x0088A210`
- `cP2PClient::SignalFlowBasic`: `0x00882040`
- `cP2PServer::StartCoopGameStateTransfer`: `0x008744B0`
- `cP2PServer::UpdateCoopGameStateTransfer`: `0x008750D0`
- `cP2PServer::UpdateFlowCommand`: `0x00875500`
- `cP2PServer::Update`: `0x0087D8B0`

Run `123758` called the exact client StartGame routine after native actor conversion. It returned normally and changed
PC `cP2PClient+0x251` (`mGameStarted`) from zero to one, but the search modal remained and the JIP size, buffer, and
type fields stayed empty. StartGame is downstream of the missing transfer and cannot solve the transition alone.

Runs `131142` and `131848` invoked the exact host transfer-start routine. It returned normally with two users,
`active=1`, stage 0, next-user index 0, and item cursor -1. The updater did not advance. Read-only telemetry showed an
empty healthy packet arena (`used=0`, `capacity=160`) but a flow gateway in state 1; the updater requires state 3.
This excludes packet-arena exhaustion and places the missing condition before JIP/NFS serialization.

Run `132406` called the complete stock `UpdateFlowCommand(command=3)` routine. It returned without fault but left the
transfer inactive because its `ClientCollectionComplete` predicate had not received command-3 Flow acknowledgements.
Instrumentation established that `cP2PServer::ProcessFlow` is PC `0x0087DAB0`: ordinary commands set
`tClientData.mFlowRecvMsgs[command]` at record offset `0x12 + command`, then immediately call
`UpdateFlowCommand(command)`.

Runs `135816` and `140311` completed this native Flow path. The frontend-native client needed the signal attempt in
the shared lobby observer rather than only the old direct-client pump. Host and client now each call
`SignalFlowBasic(3, null, null)` only after native actor conversion, P2P stage 2, and local-node state 3. The host
record table proved both confirmed members reached `flow[3]=1`; no record bytes or transport packets were synthesized.

Run `140922` waited until the record table itself showed 2/2 command-3 acknowledgements, then dispatched the exact
`StartCoopGameStateTransfer` on the game thread. It returned `state=0`, `active=1`, `users=2`, `cursor=-1`; both
processes and endpoints remained stable. The updater stayed at descriptor stage 0. Disassembly of PC
`UpdateCoopGameStateTransfer` shows its first hard gate: the object returned by `0x00854590` must have state 3 at
`+0x3C`. That getter returns global `0x00DDEA04`; telemetry shows this gateway remains state 1. Only after state 3 does
the updater allocate a 0x1C-byte packet and begin walking its two-user descriptor. Current staged DLL SHA256 is
`CF755FFC772ADFF7E0DA485029ACA28783CEEB432115EEF245B1930F6700448B`. Campaign gameplay remains unverified; the next
target is the gateway's real state-1-to-state-3 packet/lifecycle transition, followed by JIP population and frontend
dismissal.

### Player 2 transition chain observed; four-client mesh listener isolated (2026-09-27)

PC `cP2PClient::SignalFlowDataTransfer` is `0x008897D0`. Its START path emits client flow command 7. PC
`cP2PClient::ProcessFlow` handles command 7 at `0x0088A625`, stores the transfer descriptor at client `+0xAC`, sets
`mDataTransferActive` at `+0x97`, and purges stale player data. In two-slot run `145241`, this complete native path
executed automatically: command 3 completed, the 701-byte payload arrived, command 7 completed, Player 2 consumed its
JIP state, and both processes remained stable. Manual StartGame is no longer part of the required path.

Four-slot run `145707` confirmed four native records, local network IDs 0-3, and all remote actor slots, but all four
connection meshes remained state 2. Player 2's endpoints to Players 3 and 4 remained endpoint state 4 with one
retransmit packet. Both receivers got `P2PSessionRequest_t` and accepted Player 2, but never polled channel 5679 after
their host connection was established. Mesh state 3 is emitted after PC `cMeshTopology` receptor event 20, so
writing the mesh state directly would bypass the handshake and is rejected.

Exact PC listener matches recovered from OTR are: `Listen 0x00851A90`, `Pop 0x00851B10`,
`HandleConnectResultSuccess 0x00863680`, and `HandleEvent 0x00863740`. PC's success handler additionally receives the
connection event pointer. Listener experiments showed a subtle ownership split: `cConnListener2+0x70` points at the
connected endpoint, while `mListenConnection+0x100` references a different reliable-layer ownership view. Calling
high-level Listen after teardown faults; replaying the success handler leaves listener state 1 even when the active
endpoint is state 6/ready 1. The experimental path is opt-in only (`-coopmeshlistenerprobe`) and is not staged. The
ownership interpretation above is superseded by the read-only control below; do not replay callbacks or force state.

### Clean-content control and missing client listener startup (2026-09-27)

The old endpoint-rearm hypothesis was wrong. PC `cTopologyManager::HandleEventLinkAccepted` at `00886E10` reads
listener `+70` itself, calls native endpoint acceptance, then invokes virtual Listen(true) at `00886EDF`. It does not
use Pop. Its listener is at manager `+BC`, with vtable `00CBA244`. The manager is available both through P2P `+18`
and mesh constructor info `+34`; live snapshots verified these point to the same manager in the current build.

Unchanged-DLL control `161517` reproduced the apparent regression without listener hooks. Its host loaded a replaced
safehouse under a vanilla save, fell through the map, and the first client showed the native host-declined dialog.
The earlier four-member control `145707` never left the host save picker. Clean-content control `162213` temporarily
loaded the eight original PC safehouse archives: Chuck stood at the bathroom save door and all four native members
confirmed. This is strong evidence that the intervening admission failures were confounded by map state, not proof
of a listener-hook regression. Existing converted archives are backed up per run and restored after the test.

In `162213`, every mesh reached state 2 with four players, but all three client topology listeners still had state 0,
started=0, connection=null. The host's listener was initialized. Read-only snapshots are retained as
`network-final.0.json` through `network-final.3.json`. Native PC `cTopologyManager::Listen` is `008517E0`, matched to
OTR `008B8B40`; it builds InitInfo for port 5679, calls listener Init, then Listen(true). Unlike raw
`cConnListener2::Listen`, this is a valid startup entry for an idle listener with no connection. The next gated
experiment calls it on the game thread, only for genuinely uninitialized listeners. No ownership fields are written.

The observed command-3/command-7 and JIP path remains valuable evidence, but none of these controls yet proves
four controllable campaign players, movement replication, combat, or a level transition.

PC `cP2PConnMesh::HandleReceptorEvent` is `00854710` (vtable reference `00CBD168`), matched to OTR `008BBA10`.
Both read event subtype at `+1C`, but the values differ: PC subtracts 1 and then `0x13`, giving subtype 20; OTR
decrements then subtracts `0x0E`, giving subtype 15. That branch writes mesh state 3. Event 1 clears the local member
when its pointer matches (`mesh+90` on PC, `+80` on OTR). Earlier notes using OTR's 15 for PC were incorrect.

Control `162816` validated idle-listener startup: native Listen returned true on Players 2-4, created a connection,
and each listener reached state 1 with an endpoint in LISTEN (3). Players 3/4 then repeatedly consumed Player 2's
22-byte handshakes on channel 5679. They still did not enter SYN-ACK/connected, and every mesh stayed at 2. The run
was resource-aborted (1,923 MB remaining commit versus a 2,048 MB reserve), not crashed. All processes survived to
cleanup and all content was restored. Treat this as a narrowed boundary, not a completed mesh pass.

Offline dispatch tracing identifies PC `cEndPoint2::HandleIncomingData` at `00888CB0`; its state-3 jump-table branch
at `00888D39` calls `00879690`, matching OTR `HandleIncomingDataListen` (`008D8BE0`). The PC listener parser calls
`00849320`, reads peer identity into endpoint `+78/+7C` and transport address into `+80/+84`, and progresses to
state 5 only through the SYN/header-queue path. Do not confuse these two 64-bit fields: existing endpoint logs label
the transport address as "peer" and the identity as "connectPrefix". The read-only snapshot exposes both explicitly.
The next candidate records bounded raw headers and observes native client-side CanListen without changing its result.

### Client link capacity and first completed mesh gate (2026-09-27)

Control `164738` identified the rejection: frontend clients retained a one-remote-link capacity, already occupied by
the host. Native CanListen returned `full=1`, not a duplicate/identity failure. The host-only installer did not run
on frontend clients; installing from the direct Connect hook also had no effect (`165444`) because those clients
bypass that hook. The idempotent installer now runs before the synthetic JoinLobby result is delivered. It applies
the existing validated four-player capacity policy; ordinary two-player mode retains stock limits and no acceptance
checks are bypassed.

Control `170127`, DLL `2B59E356E31AA7FAEFA6C8ECC2BFBDFB1A06C2412B7FB2A07A4628BF274D6067`, accepted
the missing peer links and all four passed the mesh-state-3 gate into flow command 3. Three clients dispatched native
data-transfer START on the game thread. Host and Players 3/4 then terminated with access violations. The available
host dump is a teardown snapshot with no exception stream and no surviving game thread, so no fault address is yet
proved. The harness-only logger candidate enables the existing vectored reporter for vanilla co-op, where it had
previously been inactive. Preserve the distinction between completed handshake progression and stable gameplay.
