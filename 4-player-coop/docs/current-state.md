# Four-player campaign co-op: current state

Updated 2026-10-01. This is experimental local gameplay, not a finished release.

Run `four_instance_20261001_175129` passed the independent four-player visual-identity control. The runtime used
the native actor-local clothing path on the game window thread: Players 1 and 2 remained unchanged, Player 3
received the full TIR outfit (helmet, torso/legs, gloves, boots, and overlay), and Player 4 received only the yellow
TIR jacket. All four native admissions and confirmations remained live, watcher captures showed the intended
group, and the user confirmed the result in the exposed four-window grid. The run remained stable until intentional
shutdown. Cleanup restored all eight safehouse archives plus `streamedassets.big`; no DR2 process remained.
This proves independent local wardrobe identity for the current harness, not production network replication.

Run `four_instance_20260928_162320` cleanly admitted all four owners and captured
the Phase 2 baseline at 400/400 health, then routed all four through the bathroom
to the safehouse vent with private native input. The attempted shared transition
did not reach `cP2PClient::ProcessFlow`. The host's native update counter advanced
briefly from 53009 to 53037, then remained at 53037 for 266 seconds while its game
thread waited in the pre-flow asynchronous loader loop. About 16 seconds after
the host stopped servicing the session, all three clients changed their host
endpoint from state 6 to state 8 within 197 ms and their reliable/listener states
closed. Captured stacks on all four processes show the same loader wait family.

`analyze_transition_stall.py` now recognizes this complete signature and the
harness report exposes it as `transition_stall_analysis`. It classifies this run
as `pre-processflow-loader-stall`, while Phase 2 combat remains false because no
enemy-area combat checkpoint was reached. This is evidence that the client link
timeout followed the host loader stall; it is not evidence that the networking
layer initiated the failure. The next live run should instrument the asynchronous
load completion path and preserve the known-good two-interaction vent sequence.

That read-only instrumentation is now built behind `-cooploaderwaitprobe`. It
validates the original call at `009E7D58`, observes the scheduler and completion
object once per second, then executes the untouched `009A68E0` maintenance call.
It never writes the completion byte. Candidate SHA-256:
`588CC71BD446D5DB014A55E7D799CD8E47BD5F04AF7413CB6BD84D16C1326DDF`.

Phase 2 instrumentation is now live. `snapshot_campaign_combat.py` validates the
retail PC executable signatures and records each process's authoritative local
health and zombie-kill count plus all four replicated status-health, maximum-health,
KO-ready, and revival-percent fields. Run `four_instance_20260928_152832` admitted
all four players, moved all four owners through the bathroom and native vent using
ordinary private input, and placed the party together in the main safehouse. The
post-transition snapshot retains four-way agreement at 400/400 health for every
slot and zero kill deltas. This proves that the combat observer survives the shared
transition without inventing damage or ownership. It is not yet attack, damage,
KO, or revive proof; those remain the next Phase 2 live gate.

The 30-minute observation completed without an early process exit or recorded
runtime fault. Peak private memory was about 3.01 GiB across the four hidden
instances. Cleanup verified the baseline DLL, original save, render settings,
and all nine temporarily staged stock archives; no DR2 process remained.

The same run exposed a scene-dependent inventory representation: host item values
observed as 168/167/166/164 before the vent appeared as 16388/16387/16386/16384
afterward. Combat verdicts therefore use the independently validated kill and
health fields; inventory IDs are not compared across that transition until the
encoding is decoded.

The harness reporter now has a fail-closed Phase 2 gate. It requires synchronized
named checkpoints for one independently attributed kill by each owner, replicated
damage, a KO, and a teammate's native E interaction followed by revival. Attack
and revive inputs must be acknowledged by the correct process and present in its
native-input samples between the matching snapshots. Missing files or input
ownership leave `four_player_phase2_combat_verified=false`.

Run `four_instance_20260928_144959` completes the Phase 1 local campaign-control
gate. All three clients joined through DR2's frontend and were admitted by the
host, all four native meshes remained connected, and every process retained four
visible actor slots with the expected local/remote ownership. A bounded safehouse
trial moved all four local owners independently; every movement replicated in all
four process snapshots. The party then used the native safehouse vent, moved
roughly 355-358 world units into the next loaded area, and all four owners moved
independently again by roughly 1.8-2.4 units. The reporter now returns
`four_player_local_control_verified=true`,
`four_player_post_transition_control_verified=true`, and
`four_player_campaign_verified=true` for this defined Phase 1 gate.

The 20-minute observation completed without an early process exit. Peak private
memory was about 2.97 GiB across the four hidden instances. Cleanup verified the
baseline runtime DLL, original save, render settings, and all nine temporary
stock content archives; no DR2 process remained. This is proof of local
four-player campaign admission, control, a shared transition, and retained
post-transition control. It is not yet proof of combat/damage, down/revive,
complete partner HUDs, campaign scripting, production Steam sessions, or a
release-ready solo/2P/4P selector. Combat and damage are the next gate.

Run `four_instance_20260928_135823` restores the four-player campaign path after
the frontend navigation harness was made tolerant of missing menu-key release
acknowledgements during screen transitions. The wrapper completed normally and
verified baseline DLL, original save, render settings, and all nine temporary
stock content archives were restored; no DR2 process remained. Host admission
accepted peers `...0001`, `...0002`, and `...0003`; final mesh snapshots show all
three remote endpoint states at 6, native meshes connected, four-player clothing
capacity active with four live heap sets, and all four actor slots visible and
unhidden in the safehouse. After admission, private W-key pulses were sent to
P1/P2/P3/P4 with exact down/up acknowledgements; the post-input snapshot at
14:09:09 shows player positions replicated across peers and players 2-4 moved
materially from the admission cluster. This is now real local four-player
campaign control evidence. Combat/damage, revive/down states, complete partner
HUDs, another area transition, production Steam, and a user-facing solo/2P/4P
selector remain open.

Run `four_instance_20260928_002321` also proved a host-to-P4 bat transfer across
all four inventories and gathered all four at the native safehouse vent. The
normal exit interaction started a transition but crashed in all four processes
while loading a modified SawBlade vertex declaration. That installed compressed
asset fails its checksum; the preserved PC original passes. A new opt-in stock
global-archive swap isolates this content confound without discarding port work.
The later `005833` control proved the same exit can survive with stock streamed
assets; the `135823` control restored reliable four-player admission afterward.
The next gate is to combine post-transition movement/control with combat and
damage checks. See the gameplay validation document for hashes and evidence.

Completed run `four_instance_20260927_234621` captured four distinct physical
Chuck bodies in one room and recognized independent private mouse/camera input.
The read-only PC logical-button observer confirms held attacks reach native
button records, but no damage verdict is claimed. Native bathroom door opening
and the host's replicated movement through it are now observed. An area
transition, combat/damage, revive, complete partner HUDs, production Steam and
the user-facing mode selector remain open. All four independently traversed the
bathroom door into the corridor. The 30-minute observation completed without an
early process exit or recorded native fault, and restoration verified. See
`four-player-gameplay-validation.md` for exact evidence and limitations.

Latest control `four_instance_20260927_224611`: the corrected acknowledgement
protocol passed a full native pause/resume cycle initiated by each of P1/P2/P3/P4.
Every participant reached the expected paused masks and then all-zero masks.
Post-resume isolated movement/convergence passed for all four owners, with the
original tolerances unchanged. Earlier crowded-spawn failures remain recorded.
Mouse/cursor camera response has since been observed; combat remains unproven;
partner HUDs, transitions, revive and production Steam still remain open.

Latest gameplay control `four_instance_20260927_215228`: all four owners passed
separate private-input movement/convergence trials. The host dropped a bat and
Player 3 picked it up; all four inventories agree on the transfer of item 0xA8.
The ten-minute observation retained connected meshes without recorded faults or
teardown. **That run exposed the pause/resume blocker now addressed above:** after native menus closed, P1/P2/P3
remained waiting on another player. Client camera framing and partner HUDs also
need work. At that point four-body visual proof, combat, revive, transitions and
production Steam were unverified. See `four-player-gameplay-validation.md` for the exact
trials, failed trials, evidence boundaries and pause diagnostic. Run `221105`
confirmed native player count four and a stuck [8,8,8,0] pause quorum on all peers;
that historical failure was compared with two-player pause behavior before the
acknowledgement fix above was implemented.

Four-player control `four_instance_20260927_214248` delivered and natively applied the host world on all three
clients, activated all four actor slots with correct local/remote ownership, and retained connected native
meshes through the 90-second observation interval. No runtime faults, native teardown calls, quit requests,
shutdown events or desync assertions were recorded. Final captures show actual safehouse gameplay, but do not
yet establish four independently controlled bodies in one unobstructed view. Movement was unverified in
that run and was subsequently checked in `215228` above. **Complete gameplay and production Steam
support remain unverified. This is a campaign-entry milestone, not a playable release.**

Diagnostics `212632`/`213436` identified a real ordering race: native PlayerCycleItems event 0x16 referred to
item 0xA8 before the receiving client had restored that item during world application. The same ID resolves
successfully milliseconds later during native inventory restoration. The four-player harness-only
`-coopjipbroadcastqueue` candidate retains incoming gameplay event bytes during JIP, then replays them through
the original native processor after actual world completion. It does not suppress assertions or fabricate
readiness. Run `214248` replayed 30/34/35 events on P2/P3/P4; candidate SHA256
`D69909459A881E8231229CCF54FBC86D12C82D990739F4928EE73B60B8C264C2`.
Two-player control `201427` retains connected final meshes and safehouse views without faults or teardown.

Sequential input and joins now require fresh acknowledgements from each owned PID. A failed/missing frontend
join cannot be counted as success. GPU frame capture runs only on Present, not the scripted-input poller.
A read-only actor sampler records validated identities, ownership, hidden flags and positions; existing samples
do not establish four rendered actors or independent movement replication. 89 Python tests, eight x86 native
fixtures, and the admission-control fixture pass. Completed controls restored baseline DLL, original save,
settings and eight archives. See `docs/native-teardown-investigation.md` and
`docs/four-player-gameplay-validation.md` for evidence and the acceptance gates.

See `docs/world-transition-hold.md`. Historical snapshot `game_active` readings before `183105` are invalid:
the decoder used +0x85 instead of the PC instruction-validated +0x8D. Preserved artifacts are not rewritten.

## Proven

- Four hidden, muted local instances can exchange native DR2 protocol traffic through the loopback Steam shim.
- Genuine `cLocalServer` plus its local client admits three genuine remote clients. No fabricated native roster writes.
- Runs `four_instance_20260925_201328` and `four_instance_20260925_201612` confirmed all four native records
  with confirmation rows `1,1,1,1`, fresh state-6 endpoints, no endpoint errors, and no premature process exits.
- The latter run removed the speculative receive filter and retained approximately 158 seconds of probe activity.
- Explicit two-player control `203120` confirmed two members with the original native admission/link limits unchanged.
- Integrated run `203937` repeated four-member confirmation for about 126-127 seconds and passed all four
  in-process local save probes. No unhandled exceptions, stale endpoints, or premature exits were observed.
- Harness filename save I/O now goes to disposable per-instance directories on D:, not Steam Cloud.
  The same typed wrappers pass four in-memory round trips, four disk/reopen/atomic-replacement tests,
    and 12,000 wide-return ABI calls. The Steam callback helper passes 2,000 dispatches; the reporter has 24 tests.

## Corrections That Matter

1. Vanilla game type is COOP=1, not OTR's COOP=0. The synthetic matchmaker must not remain unset (2).
2. A remote server is not a local server. Reusing one caused out-of-bounds fields and false membership evidence.
3. `cConnLink2::Send` sends outgoing packets and returns bool. Never reroute it by the last received peer.
4. Callback vtable slot 0 is the three-argument call-result overload; slot 1 is the one-argument callback.
5. Four server records do not raise the campaign's separate remote-link limit. `0x0087C8CA` selects one vs three.
6. Native admission can reject overlapping joins while `cLocalServer+0x324` is busy. Do not clear that flag.

## Current Validation

Run `four_instance_20260925_201943` used original transport layouts but lost one simultaneous admission to
native busy handling. It is not proof of insufficient transport capacity. Control `202242` completed:
original layouts, no receive filter, confirmation-gated sequential joins. All four members confirmed,
with fresh connected endpoints, about 116-117 seconds of probe activity, and no premature exits or
unhandled exceptions. Peak total working set was 1,653 MB; private memory was 2,650 MB.

The default harness now uses stock transport and sequential joins. These serialize the test's admission
requests without altering native busy handling. Simultaneous join retry remains a production requirement.

The former `builds/stable_four_peer_transport_20260925` is explicitly rejected. The later filtered checkpoint
is `builds/native_handshake_filtered_20260925`. Preferred checkpoint is
`builds/native_handshake_stock_20260925`, DLL SHA256
`3B66DC9ABB3232637982C99570496AA130A230A3C1AD4805F26F8381518658F2`.
Both are network-only evidence, not campaign releases.

Latest integrated checkpoint: `builds/native_handshake_local_saves_20260925`, DLL SHA256
`2F467518FB1F3322008DF9BDBD3BD32AE9C76E1A632B3A5F6CF4F990F6E13C2C`.
The live disposable probes validate the wrapped filename I/O path, not game-authored save/load serialization.
Harness storage fails closed if `DR2_COOP_SAVE_ROOT` is missing or is not an existing non-reparse D: directory.

Frontend control `204240` retained the same loading frame across hidden captures despite healthy processes.
This is a separate blocker before campaign testing. A full dump exposed only WOW64 host contexts; the watcher
now also supports `--threads-path` for actual x86 registers and stack-address hints, with balanced per-thread
suspend/resume. A disposable x86 fixture validated that mode. Stack hints are not an unwound call stack.

Frontend control `210944` resolved that blocker for two simultaneous background clients. DR2's fixed names
`ThreadPoolEvent0..3` and `HWJobManagerShutdownEvent` let separate processes consume one another's worker signals.
The harness now gives only those engine job objects process-private names. Both clients reached the real main menu,
produced five distinct hardware-rendered captures, passed isolated save probes, remained alive, and reported no
debugger faults. Windows remain nonactivating tool windows positioned beyond the complete virtual desktop.

The harness also now owns a thread-safe synthetic keyboard queue rather than merging scripted state into an
unfocused hardware device. Press and release transitions survive independently of DirectInput acquisition. Unit
tests cover immediate state, buffered reads, peek, flush, bounded overflow, and invalid arguments.

Four-instance control `20260927_100034` then combined that frontend-safe build with the stock native transport.
All four native records confirmed, all endpoints remained in state 6 for approximately 116-117 seconds, all four
local-save probes passed, every process survived to deliberate cleanup, and the attached debuggers reported no
faults. Runtime SHA256: `E4D51A9024EAFADB5F9BDC11D2F72E34FCC10F10D934DA5CD8CFB2F733BD29ED`.
This validates compatibility between the worker-object isolation and native handshake; it is still not campaign play.

Lobby control `20260927_100345` drove two background clients from startup through the real `Join Co-op Game` path.
Both reached DR2's native warning that a client cannot save story progression, produced seven distinct frames, passed
their local-save probes, and survived without debugger faults. The warning itself is now a harness checkpoint before
the scripted confirm that opens the lobby browser; lobby enumeration and joining remain the next frontend gate.

Frontend control `20260927_101920` dismissed that warning and reached DR2's gamer-profile/save-slot picker. Its
`SLOT 1` through `SLOT 3` rows are local save choices, not remote-player slots. Both clients produced eight distinct
frames, passed isolated save probes, and survived without faults. Lobby discovery still required another confirmation.

Read-only actor control `20260927_100703` established that DR2 already allocates four human actor objects in each
process and assigns local slots 0, 1, 2, and 3 from the native session. All initially remained in single-player mode.

Activation control `20260927_101249` called vanilla `cP2PClient::SinglePlayerToMultiPlayer` from the harness network
worker. Every process enabled its first nonlocal actor and then faulted at `0x0093BF95`: a null result from the
engine's thread-local profiling accessor. All four actors had the same valid vtable and enable method, ruling out an
actor-class mismatch. The exception was caught, but this run is deliberately failed evidence.

Control `20260927_101643` dispatches that same unmodified native conversion through DR2's window/game thread.
All four conversions returned without faults. Every process changed to multiplayer mode, retained its own local actor,
and enabled the other three actors as remote humans. Native membership and endpoint state remained healthy for about
50-52 seconds, every isolated save probe passed, and all four processes survived until deliberate cleanup. Runtime
SHA256: `3DB59C3C58504A12F991764EEB2970052A721362F50155A0BC5E036B5EBFBFF3`. This proves native four-actor activation;
it does not yet prove a loaded campaign, movement replication, HUDs, combat, saves, or transitions.

Combined frontend/session control `20260927_102336` kept that four-member, four-actor state healthy for roughly
91-93 seconds while all four instances reached the main menu. Selecting `START GAME` on the host alone opened DR2's
native `Incoming co-op call` save/profile picker; all joiner instances remained at the main menu. The three visible
rows are save slots and must not be counted as remote members. No row was accepted in this control.

Host-launch control `20260927_102614` accepted the selected incoming-call row. The host entered the real DR2 opening
cinematic while all three joiners remained at the main menu. Native endpoints stayed connected for roughly 119-121
seconds, the four-actor invariant remained intact, isolated saves passed, and no process faulted. This proves a host
campaign can launch without destroying the four-member session; clients have not yet consumed campaign state.

Role-split control `20260927_103000` kept the host in its opening campaign while Players 2-4 independently traversed
Join Co-op Game and dismissed the no-story-save warning. Each joiner reached its own local gamer-profile/save-slot
picker while native endpoints and four-actor activation remained healthy for roughly 172-173 seconds. The next input
must choose a disposable slot before lobby discovery or campaign state transfer can occur.

Control `20260927_103424` selected those disposable slots. All three joiners reached `SEARCHING FOR GAME...`, but no
new Steam `RequestLobbyList` call appeared in their traces; the already-connected direct P2P clients cannot begin a
second frontend online session. The host continued through startup, all endpoints remained connected for roughly
200-201 seconds, and no process faulted. Lobby discovery must be validated before direct client connection, then used
to initiate that connection rather than layered on top of it.

Pre-connection lobby control `20260927_103919` let only Player 2 search while Player 1 advertised the synthetic lobby.
The game repeatedly called `RequestLobbyList`, received one result through its real call-result object, and resolved
that result to local lobby `0184000070000001`. It then rejected the lobby and immediately searched again. This proves
frontend discovery and callback delivery; the synthetic host has not yet supplied the metadata DR2 uses to accept and
display a result.

Controls `20260927_104313` through `20260927_105417` recovered the vanilla lobby schema and confirmed its string
formats. DR2 reads `AppId`, `Name`, `GameMode`, `RankedMatch`, `SearchType`, four public/private open/filled counts,
and decimal `hostSteamID`; a host also writes hexadecimal `HostId`. Numeric values use decimal `%d`, `HostId` uses
lowercase `%llx`, and `hostSteamID` uses decimal `%I64d`. Merely publishing all fields did not stop the rejection loop.

Scoped parser telemetry in control `20260927_110057` found the exact remaining mismatch: the Join Co-op route's live
manager expected `GameMode=1` and `RankedMatch=0`, while the synthetic host advertised `GameMode=0`. Every other parsed
field was stable. The host metadata now advertises `GameMode=1`; the reporter separately records discovery, candidate
compatibility, and a frontend-originated `JoinLobby` request. Three immediate validation retries (`110443`, `110524`,
`110818`) were not game-logic tests because hardware Direct3D initialization was unavailable and the harness failed
closed with exit code 50. The corrected metadata still needs one clean renderer-available control before lobby joining
is claimed. Candidate checkpoint: `builds/lobby_metadata_candidate_20260927`, SHA256
`C3151965393DA6154C5358735F689974BA2A98ACA2DBD0DB18B31FACACA9B9CD`.

Clean hardware-rendered control `20260927_113226` passed the corrected lobby boundary for all four processes. Players
2-4 each used DR2's real Join Co-op frontend, issued `JoinLobby`, connected to Player 1 through the stock P2P stack,
and remained at endpoint state 6. The host retained four confirmed native member records. All four processes switched
to multiplayer mode with local slots 0-3 respectively and exactly three remote-enabled actor slots per process. No
debugger fault occurred. This is a real four-member lobby/session and four-actor proof, not yet campaign gameplay.

Campaign control `20260927_113752` kept the host in the opening campaign while all three clients joined, but their
`SEARCHING FOR GAME...` modals remained open. The host displayed DR2's stock `Incoming co-op call...` banner. The
harness now has an explicit `-CampaignAdmissionProbe`; the correct PC action is a short `C` press
(`COMMAND_AI_INTERACT_WITH_PHONE`), not Enter, right-arrow, or a held C press. Save-fixture control
`20260927_120735` loaded an isolated copy of the user's Day 1 post-transceiver save and cleared the incoming-call
banner. Fresh stock traffic followed, but the client remained in the search modal. Its flow-command queue continued
to fill while the frontend did not hand control to campaign processing. Client-side post-admission frontend/travel
completion is the remaining gate before movement testing.

Transition controls `20260927_122811` through `140922` localized that gate. OTR-to-DR2 structural matching identifies
PC `cP2PClient::StartGame` at `0x00889850`, `cP2PClient::ProcessFlow` at `0x0088A210`,
`cP2PServer::StartCoopGameStateTransfer` at `0x008744B0`, `cP2PServer::UpdateCoopGameStateTransfer` at
`0x008750D0`, and `cP2PServer::UpdateFlowCommand` at `0x00875500`. Direct client `StartGame` safely set
`mGameStarted`, but did not dismiss search or create JIP data. Runs `135816` and `140311` closed the command-3
consensus boundary: host and client call PC `cP2PClient::SignalFlowBasic` at `0x00882040`, and the host's native
records show `flow[3]=1` for both confirmed members. Run `140922` waited for that native consensus before calling
`StartCoopGameStateTransfer`; transfer armed with `active=1`, two users, and no faults. It remains at stage 0 because
the global native gateway at `0x00DDEA04` is alive but state 1. The exact updater requires `gateway+0x3C == 3` before
allocating the first transfer packet. This gateway lifecycle is the next gate. The staged diagnostic DLL is SHA256
`CF755FFC772ADFF7E0DA485029ACA28783CEEB432115EEF245B1930F6700448B`.

### Native Player 2 transition and four-client mesh boundary (2026-09-27)

Two-slot control `four_instance_20260927_145241` completed the stock transition chain. The client emitted flow command
7 through `cP2PClient::SignalFlowDataTransfer`, the host completed both command collections and delivered the 701-byte
state payload, and Player 2's native client changed `mDataTransferActive` from 0 to 1, accepted descriptor 1, populated
then consumed its JIP fields, and continued without a debugger exception. This proves that transition chain can run;
it does not yet prove a correctly loaded, controllable campaign with movement replication.

Four-slot control `four_instance_20260927_145707` again proved native records, local slots 0-3, and all four actor
sets. It also localized the next boundary: every `cP2PConnMesh` stayed in CONNECTING state 2. Player 2 created outgoing
mesh endpoints for Players 3 and 4, but their 22-byte channel-5679 handshakes remained queued after the receivers
accepted the Steam P2P session. The receivers continued polling only their established host path.

OTR-to-PC matching resolved `cConnListener2::Listen` at PC `0x00851A90`, `Pop` at `0x00851B10`,
`HandleConnectResultSuccess` at `0x00863680`, and `HandleEvent` at `0x00863740`. Opt-in experiments
`151102`, `151814`, and `152440` proved that replaying the listener success callback or calling high-level Listen is
not yet valid: the listener's reliable layer and the active game connection hold different ownership views of the
same transferred endpoint. Those experiments were contained, all children survived, and the candidate is now gated
behind `-coopmeshlistenerprobe`. The installed DLL was restored to the proven SHA256
`16B3CAA063F6E33ACFD416661B0F8E5CE2364B3BAD270476A96F01277EAED86E`.

### Control audit and clean campaign content (2026-09-27 afternoon)

Controls `153504` and `155246` lost the first client even with the listener vtable hooks disabled. The preserved
DLL repeated that failure in `161517`: the client explicitly displayed "The host player declined your request to
join", while the host loaded level 40 and fell through an empty environment. Do not attribute this to listener hooks.
The earlier four-member control `145707` remained at the host save picker, not a loaded safehouse. Its four-member
proof remains valid, but those two host states are not interchangeable campaign controls.

The installed `safehouse.big` and `safehouse_persistent.big` differ from the original PC backups; the save fixture is
unchanged (SHA256 `A7012AC4EA8841B052CFB9CA9E4055BCBA9FE1C547EF2036C51083208FADAF44`). Opt-in
`-StockSafehouseContent` preserves all eight installed safehouse archives under the run's D: `content-before` directory,
stages original PC backups, records hashes, and restores the pre-test content after cleanup. Concurrent edits are
preserved and reported as restore failures, not silently overwritten. `-KeepRunning` cannot be combined with this mode.
Control `162213` visibly loaded Chuck at the real PC safehouse bathroom door, admitted all four members with
confirmation rows `1,1,1,1`, and activated four actors. All four meshes still remained in CONNECTING state 2. Saved
`network-final.0..3.json` snapshots show the host listener initialized and all three client listeners idle with null
connections, despite their mesh state being 2 and player count 4. This isolates the next listener startup experiment
from the earlier content mismatch; correct geometry and membership still do not prove replicated gameplay.

Read-only `tools/snapshot_connection_mesh.py --pid <harness-pid>` now exposes primary and mesh topology managers,
listener ownership, endpoint state, and mesh handshake counters without injecting code. `-NetworkSnapshots` records
these alongside each hidden frame capture. Listener vtable is PC `00CBA244`, not `00CBA208`; stock reliable layers have
three endpoint pointers, with bandwidth data at `+8C`. The native `cTopologyManager::HandleEventLinkAccepted`
(`00886E10`) consumes listener `+70` directly, then rearms Listen. Waiting for Pop was an invalid hypothesis.

An opt-in candidate uses PC `cTopologyManager::Listen` (`008517E0`) on the game thread for an uninitialized mesh
listener only. It constructs the connection through native Init before Listen(true), checks the mapped instruction
signature, and leaves active listeners alone. This is gated by `-MeshListenerProbe`.

Candidate control `162816` (DLL SHA256 `A65847D05E404E9C586D0C4973524584F286486A37FBDC2EFB47381397E1CC12`)
initialized all three client listeners successfully on their game threads. Receivers now consume Player 2's 22-byte
channel-5679 packets, closing the earlier unpolled-channel boundary. All four native members remained confirmed, but
the receiving endpoints stayed LISTEN (3), outgoing endpoints stayed CONNECTING (4), and meshes stayed CONNECTING (2).
This is listener-startup/packet-consumption evidence, NOT a completed mesh or four-player campaign pass.

The harness stopped this run via its memory guard: 1,923 MB of system commit headroom remained versus the 2,048 MB
reserve. All four processes were alive before deliberate cleanup, with no debugger fault; peak combined private
memory was approximately 2,325 MB. All eight content files were restored. The installed DLL is again baseline
`16B3CAA063F6E33ACFD416661B0F8E5CE2364B3BAD270476A96F01277EAED86E`.

Acceptance diagnostic `four_instance_20260927_164738` completed with all four processes alive, no debugger faults,
four confirmed native members, and four actor sets. DLL SHA256:
`5D2B8C05B7707D22F8C3BC38AE4E057F616E7DC9C3EB8F3CEA81D0CD9130D08D`. Meshes remained CONNECTING.
Native CanListen on Players 3 and 4 explicitly rejected Player 2 with `full=1 duplicate=0 reallyConnected=0 limit=1`.
Host admission used `limit=3` and accepted all three joiners. All 22 captured SYN observations decoded without an
identity mismatch, with matching send/receive bytes. The limitation is native client link capacity, not a demonstrated
packet-identity failure. The diagnostic does not bypass acceptance. All eight temporary archives were restored.

`InstallCampaignAdmissionCapacity` previously ran only in `DirectHostProbeThread`, which frontend joiners do not
execute. Candidate `mesh-client-capacity` (`93BDD81D99572D72766D56D6A3A6A59C5C859A3878AA1C3793D4B597F766B8DC`)
placed it in the direct connector hook. Control `four_instance_20260927_165444` showed that frontend clients bypass
that hook too: no capacity installation occurs before their initial native connection. Do not treat it as a fix.
This control completed with all four children alive and no debugger faults; all eight archives were restored.

Corrected candidate `tools/bin/case-zero-runtime-mesh-lobby-capacity/dinput8.dll` installs the validated policy before
the local frontend JoinLobby callback is delivered, with an idempotent installation guard. Failed validation returns
a failed lobby-entry result without adding the member. Four-player mode takes the existing three-remote-link branch;
two-player mode verifies and retains stock capacity. SHA256:
`2B59E356E31AA7FAEFA6C8ECC2BFBDFB1A06C2412B7FB2A07A4628BF274D6067`.
Exact source and harness checkpoint: `builds/mesh_lobby_capacity_candidate_20260927`.

Control `four_instance_20260927_170127` accepted the previously rejected client-to-client connections. Players 3/4
reported `accepted=1 full=0 duplicate=0 reallyConnected=0 limit=3`. All four emitted the mesh-state-3-gated command 3;
all three clients then invoked native `SignalFlowDataTransfer(START)` on their game threads. The host health log
independently records gateway state 3. This is advancement beyond the handshake blocker, NOT a stable mesh/campaign
pass: host PID 8836 and clients 10256/9156 exited with `0xC0000005` around 17:06:10; Player 2 survived until cleanup.
Thread-exit fallback dumps were captured for all three crashes, but the host dump has no ExceptionStream and its
faulting game thread is already absent. Earlier final snapshots precede mesh startup and must not be treated as the
post-handshake state. All eight live content hashes and the user's original save hash were verified unchanged after
restoration. The baseline runtime `16B3...D86E` was restored.

Closing Chrome/Discord at the user's request freed enough commit for these controls. The failed run's last sample
had 7,693 MB of available commit, well above the 2,048 MB reserve; this was not a memory-guard abort.
The existing in-process exception logger was installed only for Case Zero. Harness candidate
`tools/bin/case-zero-runtime-mesh-crash-trace/dinput8.dll` now enables it for vanilla co-op tests too, including
addresses outside the game module, while preserving `EXCEPTION_CONTINUE_SEARCH`. SHA256:
`A500950AB33A4BCCE0AB99B84ABC3001C11E01BD982E4C6D99CF567758D7D470`.
Control `four_instance_20260927_171047` reproduced the three crashes and captured the fault successfully:
`EIP=00A2B9E0`, null read at `0000001C`, on each native game thread. All eight archives were restored and the
baseline DLL reinstated. The exact source/tool checkpoint is `builds/mesh_crash_trace_candidate_20260927`.

The allocator was called for 0x2C8 bytes with heap ID -1. Its caller is the clothing async-completion path
`004D1070 -> 004D1563 -> 00A92EB0 -> 00A2BD40 -> 00A2B670`. The preserved host dump confirms all four
clothing actor mappings, reserved count 2, valid clothing heap IDs for slots 0/1, and all 13 heap IDs equal to -1
for each of slots 2/3. The failing completion token `64100000` selects player slot 2, clothing record 0. This is
not evidence of system commit exhaustion. See `docs/clothing-capacity-crash.md` for exact offsets and guardrails.

An opt-in clothing-capacity candidate now provides two side-owned native parent heaps and routes the existing
four-wide allocator to them, with signature-checked reserve/selection/destructor hooks. Solo/two-player requests
remain unchanged. Native tests validate allocation rollback, owner lifecycle, heap-slot budgeting, and 2,000 x86
register/flags/stack checks. Read-only snapshots independently check all 52 child heaps against the live registry.
Control `172900` was memory-guard-aborted before the clothing reserve ran; all four children were alive and all
content/runtime/save restoration checks passed. It must not be called a clothing fix or campaign pass.
Revised candidate SHA256 `74FF3DF5F2B325DBC002E13015DDA1A53AF4A2B32FF839F4488C34DE337A95AC`
is preserved in `builds/clothing_capacity_v2_candidate_20260927`. Control `173745` completed its 90-second
observation without a crash: all four native meshes connected, all four actor sets remained activated, and every
process retained four distinct sets of 13 live clothing heaps. The earlier `00A2B9E0` fault did not recur.
All four reported safehouse level enum 40, but final frames still showed loading screens, not playable safehouse
views. The host's native join confirmation remained visible even after one recorded private-input Enter pulse
(`additional-input.jsonl`). Final clients had `game_started=1`, `transfer_active=1`. The old `game_active=0`
field was misdecoded and must not be used as evidence; see the offset correction above.
This proves the guarded clothing-capacity fix in this control, NOT completion of step 3 or readiness for step 4.
All eight installed archive hashes, the original save, and the baseline DLL were verified restored; no test
children remained. Peak combined sampled private memory was about 2,361 MB.

Bounded control `174848` adds original-call tracing around native state-transfer startup, ProcessFlow,
and world-data application, plus thread snapshots. Candidate SHA256:
`56CBE6E76D5CA3C41C6EAE77B9772E09FBF757B33986017E08B92AB9797F97E9`, checkpoint
`builds/world_transition_trace_20260927`. It does not override flow state or force deserialization.
The control completed without a crash. Native host transfer starts automatically with four users; each client
calls world application before its JIP data is ready. Player 2 later holds a 15,968-byte buffer without another
world-application call; Players 3/4 have no pending buffer in the final sampled state. All remain loading.

Captures exposed a separate harness error: C opens the join confirmation, but does not select YES. Opt-in
`-ConfirmCampaignAdmission` now confirms before the next caller. Same-runtime control `175723` verifies that
both host and Player 2 display "Exchanging game data" and the unanswered dialog disappears. It again completes
without crashes and preserves all four connected meshes, actor sets, and live clothing storage. The loading hold
persists: Player 2 ends with 15,965 bytes pending; Players 3/4 end with no JIP buffer. Cleared final callback slots
do not establish who owned them earlier. See `docs/world-transition-hold.md` for exact evidence and the next control.
All eight archives, baseline DLL, original save, and render settings were verified restored; no DR2 children remain.

`tools/analyze_mesh_handshakes.py` compares bounded SYN headers and admission decisions without inferring packet
loss from uncaptured traffic. The first raw Steam-boundary word is retained as a transport prefix, not assumed to
be a payload length. Snapshot telemetry includes endpoint admission-query pointers/vtables and signaling ports.
The harness now checks memory during frontend captures and captures a final network state after observation.
Python regressions: 65 passed; native callback/save/input/object-isolation/clothing ABI suites passed. The report now surfaces
bounded in-process access violations and refuses a clean handshake/save pass when any were logged.

Corrected Phase 2 run `four_instance_20260928_172024` proves that the vent
transition itself is no longer the active blocker. All four admitted actors
crossed into the main safehouse, remained visible, and independently moved
after arrival; `four_player_post_transition_control_evidence.verified=true`.
The host then stopped advancing its native update counter at 51150 while
navigating from approximately (-295,-145) toward the combat area. Players 2-4
reported native connection failure reason 3 roughly five seconds later. No
loader-wait sample occurred during this failure, so it is distinct from the
pre-ProcessFlow loader stall and must not be treated as a timeout problem.

`watch_native_update_stall.py` now tails only newly appended host health
telemetry and captures all host threads after a bounded 750 ms update-counter
plateau. `-NativeUpdateStallSnapshots` arms it in the four-instance harness.
The watcher is observational except for the crash monitor's brief suspend/resume
during the snapshot, writes a JSON sidecar with the exact plateau evidence, and
refuses to overwrite an existing capture. The next control repeats the proven
route with this watcher armed to identify the blocked host stack before clients
time out; no gameplay behavior should be changed until that stack is known.

Control `four_instance_20260928_175009` established that simultaneous post-admission
frame capture is unsafe while all four D3D9 devices are settling. All four processes
reported `Device is still lost. Free all resources.` and faulted together immediately
after the final `Request-Captures`; the four dumps are structurally valid and no
display-driver reset was recorded. `-NativeUpdateStallSnapshots` therefore skips
that nonessential capture while retaining native confirmed-member traces and later
read-only campaign snapshots.

The capture-free control `four_instance_20260928_195524` admitted four members and
kept every process alive beyond the former D3D fault. It then exposed a separate
harness-density problem: Players 2 and 3 settled at approximately `(4.46,19.91)` and
`(5.19,19.50)` in a safehouse collision pocket and could not move under acknowledged
native movement, jump, attack, camera-relative, or interact input. Host and Player 4
remained independently controllable. Native vent interaction did not gather the
distant clients, so no transition or update-stall capture occurred. The installed
DLL, save, render settings, and nine stock content archives all restored exactly.

`-SeparateConfirmedJoiners` now waits until all confirmed owners have left single-player
mode, then disperses joiners in reverse admission order with bounded private input. It is
explicit, requires confirmed native admission, records the action in `admission-input.jsonl`,
and performs no actor-position writes. The next control must prove four separated owners
before repeating the vent route.

Control `four_instance_20261001_141156` completed that test with candidate SHA256
`588CC71BD446D5DB014A55E7D799CD8E47BD5F04AF7413CB6BD84D16C1326DDF`.
All four confirmed actors first reached `singlePlayer=0 gameType=1`, then P4, P3,
and P2 were separated in reverse admission order. All four owners remained visible,
movable, and replicated. The party crossed the native safehouse vent together; the
four slots moved approximately 355-359 world units and then independently moved
approximately 1.9-2.4 units after arrival. The report records
`four_player_post_transition_control_verified=true` with no missing owner.

The host then advanced through and beyond the previous failure area. Its native
update count reached 91648, versus the former 51150 stop, with a final trailing
plateau of 0 ms. The armed watcher timed out normally and did not create a stall
capture; all four processes remained responsive until the bounded observation
ended. This control shows that the old post-transition freeze is not reproduced
when admission completes before reverse-order separation. It does not yet prove
that sequence is the sole root-cause fix. Cleanup restored the baseline DLL,
original save, render settings, and all nine staged archives; no DR2 process remained.

The aggregate `four_player_campaign_verified` flag remains false in this run only
because it did not create the separate pre-transition `local-control-before.json`
and `local-control-after.json` pair. The shared transition and post-load control
sub-gate itself is formally verified. Waypoint input now also requests five native
ownership samples on every pulse, strengthening future evidence without writing
game state.

## Next Gates

### Production Steam Alpha 4 checkpoint (2026-10-04)

The first remote Alpha 3 test established that an ordinary second player could sometimes join, but Steam and DR2
treated the lobby as full immediately afterward. Invite options disappeared, additional clients failed during
`Verifying game information`, and the host logged rejected peers with no `dr2_4p_protocol` member value. The install
was independently checked against a clean Steam mirror: all 876 official files matched byte-for-byte, so modified game
content was ruled out.

The root cause was the import-patching gate: `SteamMatchmaking` and `SteamAPI_RunCallbacks` were classified as trace
hooks and skipped whenever `Trace=0`, including every normal production launch. Alpha 4 explicitly installs those two
imports when production mode is active. A local Steam launch confirmed both imports were hooked and the real
`SteamMatchmaking` interface was proxied. The remaining acceptance gate is a fresh remote test proving that the host
creates and tags a four-member lobby, retains invite availability after Player 2 joins, and admits Players 3 and 4.

The first Alpha 4 host control then proved the repaired production path: Steam created lobby
`01860000B6FD6A02` with limit four and `dr2_4p_protocol=1`. Steam's friend-facing invite controls were still absent
because vanilla requested `k_ELobbyTypeInvisible` (`3`), which Steam documents as searchable but not visible to
friends. Alpha 5 maps only that production lobby type to `k_ELobbyTypePublic` (`2`) and intercepts later type changes
with the same policy. It also changes the SetLobbyData, SetLobbyMemberLimit, and SetLobbyType wrappers from Win32
`BOOL` to the Steamworks C++ `bool` ABI; the prior mismatch produced garbage-looking nonzero return values and could
misclassify a failed call as successful. Remote friend visibility and four-peer admission remain the acceptance test.

The first Alpha 5 remote attempts exposed a circular admission gate: three real peers reached `Hook_CanListen`, but
their member-level protocol metadata remained unavailable while the host rejected the P2P traffic needed to complete
admission. Alpha 6 now requires the host lobby's protocol tag and verifies that the requesting Steam ID is already in
that lobby's member list. A missing member-level tag is treated as propagation delay only for such listed members;
peers outside the tagged lobby remain rejected. Rejection diagnostics now log an initial burst and periodic samples
instead of writing approximately ten lines per second per incompatible peer.

The active gates after `20261001_141156` are attributable combat for P1-P4,
replicated damage, genuine KO and teammate revive, a game-authored save/load,
disconnect/rejoin recovery, broader campaign scripting, production Steam peers,
and release packaging with a solo/2P/4P selector. Do not reopen the shared-transition
blocker unless a controlled replay reproduces it.

Controls `183813` and `184847` narrow the remaining hold to network-file delivery: Player 2 waits at native
LEVEL_COMPLETE, while Players 3/4 have pending requests but no world buffer. The host retains completed local
request 1 carrying receiver handle 0. The producer also routes chunks through the first connection. Opt-in
recipient/cleanup control `185603` drains all three host requests without crashes, but Players 3/4 still do not
assemble their world data and all four remain loading. Diagnostic control `190607` confirms all metadata headers
still went to Player 2 because the first candidate skipped chunks with null payload pointers. Players 3/4 rejected
all 15 payload segments while waiting for sequence 0. Corrected header-routing control `191316` has completed.
The reporter now distinguishes accepted metadata, rejected payload, and local/receiver cleanup handles; 70 Python
tests and seven native suites pass. Control `191316` validates header/payload delivery and native world apply on
all three remote clients, then exposes a post-start disconnect/transition. The next investigation is the first
disconnect after world application, not the already-resolved NFS loading hold. No four-player campaign-entry
stability pass has yet been established. All four processes survive through deliberate cleanup with no recorded
runtime access violation or debugger exception, but the network result fails after disconnect. The baseline DLL,
all eight archive hashes, render settings, and original save were verified restored; no DR2 processes remain. See
`docs/nfs-multi-client-ownership.md`. Neither network connectivity nor a cleared queue is a gameplay verdict.

- Use `-StockSafehouseContent` for save-fixture campaign controls until installed vanilla content is independently
  audited. Do not compare a save-picker host to a loaded safehouse as though they were identical runs.
- Preserve the validated clothing-capacity control and separately test natural teardown/reinitialization. Preserve lifecycle/cleanup
  ownership and the stock solo/two-player path; do not substitute heap 0, suppress the fault, share another player's
  clothing heap, or blindly increase the reservation count. See `docs/clothing-capacity-crash.md`.
- Preserve the NFS header/recipient/cleanup fix and native command-3/world-apply chain from `191316`.
- Trace the first post-world-apply disconnect and new transition against two-player control `183105`. All three
  clients consume their buffers at 19:18:49.592-.618; a listener is already shutting down at 19:18:50.009,
  before the final scripted confirmation at 19:19:15.901. Determine native close/leave ownership and transition
  cause before changing timeouts or participant rules. Enum 40 or an applied buffer alone does not establish
  a stable rendered campaign. Do not clear loading flags or manufacture a gameplay pass.
- Validate game-authored save/load behavior in the disposable local store during the first controlled campaign load.
- Move the native session into an actual campaign, activate and synchronize all four player actors, then test
  inventories, damage, AI ownership, pause/cinematics, disconnect/rejoin, and level-transition negotiation.
- Implement the user-selectable solo/two/four-player menu and preserve ordinary solo/two-player behavior.
- Validate real remote Steam sessions separately; the current identity/lobby/transport provider is local-only.

## Operating Rules

Work only in the D: DR2-Porting-Workspace. Keep DR1 and other agents' projects separate.
Use `tools/test-four-instances.ps1` for hidden/muted tests, with `-CrashMonitor`; it guards available memory,
closes only its own children, restores render settings, and records pre-cleanup exits and DLL hashes.
Process survival alone is never a network pass. `network-summary.json` reports native handshake separately
from campaign gameplay, which remains unverified. Ordinary game launches must not receive harness flags.
