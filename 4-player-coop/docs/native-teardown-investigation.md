# Native disconnect investigation

September 27, 2026. Diagnostic continuation after the source-only GitHub import.
This is not a disconnect fix or a four-player campaign pass.

## First bounded trace

`four_instance_20260927_200435` used diagnostic runtime SHA256
`744241E2B2D73D005DE411F79BEEE89B4D86F2B53F3C42FC86066085AE631655`.
The source differs from the preserved header-routing candidate only by bounded
original-call teardown instrumentation. The NFS ownership/header fix is unchanged.

All four processes survived the observation and had no recorded runtime access
violation or debugger exception. They reached connected mesh handoff, but this
control did not repeat the earlier all-client world application:

- Player 2 accepted one metadata segment and 15 world payload segments.
- Players 3/4 did not receive world payloads in this control.
- No traced world-application call occurred. Do not classify this as the same
  post-application failure seen in `191316` just because both show lost-host UI.
- The host first called native client shutdown at `20:10:17.416`, from return
  address `00864D09`. Player 2 followed at `20:10:32.416` from the same address,
  then called P2P shutdown from `0086E994`.
- The final scripted admission confirmation was at `20:10:32.760`, after the host
  had already shut down. This timing does not establish the initiating cause.
- `CaptureStackBackTrace` returned no frames at these sites; the directly recorded
  caller address is available, but an empty stack must not be presented as an
  unwound backtrace.

Final Player 2 capture `coop_capture.1.20.png` shows the safehouse behind a lost-host
dialog. Surviving processes and visible geometry do not establish a stable session.
The staging manifest at `builds/teardown_control_20260927_200435/staging.json` verifies
restoration of the baseline DLL, original save, render settings, and all eight
installed archives. All owned DR2 processes were closed. Raw evidence stays local.

## PC call chain

Read-only disassembly of the preserved mapped PC image establishes:

| Site | Meaning |
| --- | --- |
| `00CB862C -> 0087C550` | Native client Shutdown vtable entry |
| `00CB86AC -> 00876B00` | Native P2P Shutdown vtable entry |
| `00864B90` | Cleanup path guarded by client byte `+3DC` |
| `00864D07` | That cleanup path calls client Shutdown; return is `00864D09` |
| `00873BE0` | Moves client matchmaking state to 4 and sets `+3DC = 1` |
| `00875082 -> 00873BE0` | Server receptor's server-down request |
| `008829BF -> 00873BE0` | Client receptor's server-down request |
| `00CBCF04 -> 00874F20` | Server topology receptor vtable entry |
| `00CB8624 -> 008827B0` | Client topology receptor vtable entry |

Both receptor switch tables select the request branch for **PC event 16** read at
event `+1C`: server table `008750B4/008750A0`, client table `00882BF8/00882BD0`.
OTR's `cTopologyEvent::eTopologyEvent` instead labels SERVER_DOWN as 12 and
MESH_DISCONNECTED as 16. Those enum numbers are not interchangeable. The PC branch
mapping, plus the structurally matched OTR server-down handler, supports the
server-down interpretation; it does not identify what originally emitted the event.

## Refined trace

The next candidate additionally observes the two request call sites and event 16
at both receptor entries. Every wrapper retains the original calling convention,
arguments, return value, and native call. Installation validates the original PC
call targets/vtable entries; a mismatch is reported instead of patched blindly.
Nothing clears the disconnect flag, suppresses native teardown, extends timeouts,
forces readiness, or changes payload routing.

Reports expose bounded `native_teardown.calls` with source line, timestamp, caller,
operation, thread, and optional stack-address hints. Missing observations are not
proof of stability, and the report never turns these traces into a gameplay pass.
Five teardown regressions cover these observations. Two additional lobby-evidence
regressions described below bring the Python suite to 77 tests.

## Two-player observer control

`four_instance_20260927_201427` tested refined runtime SHA256
`D009A73471077C6D01CD23956B16947D74D022C9B6969E3D33E569748AAF16C4`.
Player 2 applied 15,938 bytes at `20:17:55.685`; the original call returned with
JIP state 0 at `.695`. Both final native meshes remained connected, both reported
game-active 1 at the corrected PC offset, and both had cleared loading sync.
Captures `coop_capture.0.11.png` and `.1.11.png` show the safehouse with reciprocal
co-op HUD markers, without a lost-host dialog. Neither process recorded a teardown
call, runtime access violation, or debugger fault in the bounded observation.

The 90-second observation completed and the staging manifest verifies restoration
of the baseline runtime, original save, render settings, and all eight archives.
Both owned processes were closed. This preserves the two-player control; it does
not establish independent movement, combat replication, or four-player stability.

## Server-down producer

Read-only PC disassembly also identifies `00861A40`, which clears four server
member records, calls its native virtual method at `+10`, and constructs an event
with vtable `00CB8DC0`, internal ID at `+18`, event kind 16 at `+1C`, and the server
object at `+20`. It dispatches via `00861B6F -> 00A52340` if the receptor at server
`+28` exists. Vtable references to this function are `00CB81F0`, `00CB9F50`, and
`00CBC868`. The latter two are distinct server tables.

The event-construction tail matches OTR `cServer::HandleQuit` at `008C3070`, whose
equivalent event number is 12. This is a candidate upstream trace site, not proof
that it initiated a particular run's disconnect. A receiver callback may be
dispatched later through the event queue; its caller alone may not be the producer.

## Four-player comparison and reporting correction

`four_instance_20260927_202025` used the same refined runtime. All four processes
survived the bounded observation without recorded runtime access violations or
debugger exceptions, but only host, Player 2, and Player 4 became native members.
Player 3 never issued its frontend JoinLobby request. Its final capture
`coop_capture.2.19.png` shows the first disposable save slot selected in the save
picker; the host's `.0.19.png` shows "Exchanging game data". There was no mesh
handoff, world-application call, or teardown call. This control never exercised
the target disconnect, so absent teardown events are not a successful result.

The original report incorrectly marked `lobby_frontend_join_verified=true` because
it required only *any* remote join request. The reporter now requires every
expected remote instance, matches the Player number to that instance, and exposes
zero-based `missing_lobby_join_instances`. Re-analysis identifies `[2]` and returns
false for frontend join, handshake, and campaign verification. The original raw
report remains preserved. Synthetic tests cover partial and complete joins for
two, three, and four instances, plus mismatched identity and malformed join rows.
Even a complete frontend join does not establish native membership or gameplay.

The staging manifest verifies baseline DLL, original save, render settings, and
all eight archive hashes restored. No test processes remain. All seven x86 native
fixtures, 77 Python tests, and both synthetic minidump tests pass.

Next: make sequential frontend admission evidence-gated so a save-picker miss
cannot silently advance to the next caller, then repeat the refined four-player
trace. Preserve the `191316` all-client world-application checkpoint and the
`201427` two-player control. Trace upstream of HandleQuit only after reproducing
the actual disconnect; do not suppress it or increase native timeouts. Keep
incomplete admission, pre-application delivery, and post-application disconnects
as distinct outcomes.

## Acknowledged admission and repeated all-client application

`four_instance_20260927_203938` used runtime
`F382F297B99EB118BAD2731301AE012D006BB2BD30635AE9FD17148DD5CFB640`.
All three clients issued their own real JoinLobby request. They each applied the
same 15,963-byte host world at `20:45:15.398` through `.404` and returned with JIP
state 0. Players 3 and 4 received PC event 16 at `.417`, inside the world-apply
call, then requested server-down and executed native client/P2P shutdown. Host
received event 16 at `20:45:30.427`; Player 2 received it at `20:45:45.436`.
Those later 15/30-second intervals are observations, not an identified timeout
cause. The initial Players 3/4 events still need a producer trace.

The harness completed its bounded observation with all processes alive, then
closed only owned processes. Baseline DLL, original save, render settings, and
all eight archives were hash-verified restored. Surviving processes and loaded
safehouse views are not a four-player gameplay pass.

The input-thread GPU readback was removed: capture now happens only in Present,
and capture duration is logged. In `202025`, Player 3's input poll stalled across
a release/repress while capturing, leaving it at the save picker. Sequential
input now requires fresh down/up acknowledgements from the owned PID; joins also
require the matching frontend callback and confirmed native member record.
Shared log reads use explicit ReadWrite/Delete sharing because the runtime holds
a live writer. The first acknowledgement run `203618` stopped on that sharing
error and restored cleanly; it is a harness error, not a game/network result.

A read-only actor sampler validates PC instructions and human vtables before
recording four actor identities, ownership flags, hidden flags and positions.
It rejects aliased slots, non-finite coordinates and scene changes. Network
capture checkpoints now also record these samples. See
`four-player-gameplay-validation.md`; no snapshot by itself proves replication.
81 Python regressions, all seven x86 fixtures, and the admission-control fixture
pass. The next candidate adds original-call-preserving HandleQuit producer
tracing and starts the frontend observer at a real join instead of process boot;
no native timeout, disconnect or readiness behavior is suppressed.

## Initiating quit request

Run `four_instance_20260927_204843` repeats all-client world application (15,971
bytes). Players 3/4 enter native `cServer::HandleQuit` first at `20:54:20.101`,
caller `00864A57`. Event 16
then names that same server object as its source. PC `00864A30` performs the
virtual Quit call; its only direct call is `0088A0DD` in `00889F50`. These are
initiating calls, not merely final cleanup. They still do not explain why quit
was requested.

Run `four_instance_20260927_205651` observes the native quit request at `00889F50`.
Players **2 and 3**, not 3 and 4, request reason **9**, stage 2, previous/deferred
reason 15 at `21:02:29.913-.914`. Logged caller is `00887CFA`. They have not yet
entered world application; Player 4 alone applies 15,979 bytes at `.897-.908`.
Consequently, neither a fixed rejection of slots 2/3 nor world deserialization
itself is established as the cause. Subsequent reason 10 requests finalize the
native cleanup. Raw PC reason numbers are retained without assuming OTR's enums.

The next observer keeps a thread-local, nested dispatch context around native
client HandleEvent (`00887380`, validated vtable slot `00CB861C`) and records its
category/topology kind when a quit is requested. It also preserves and observes
the readiness predicate called at `00887B5F -> 008537D0`. This predicate checks
client stage 2, server state 3 and gateway state 3. No result is overridden.

Both completed runs restored baseline DLL, save, settings and eight archives,
and closed only their owned game processes. The report now preserves bounded
raw quit requests; 82 Python tests pass. Actor samples from `204843` show four
ownership slots assigned but only each process's local actor unhidden before
disconnect. Those flags alone are not a rendering verdict or independent-input
proof. Four-player campaign remains unverified.

## Native shutdown category confirmed

Run `four_instance_20260927_210721` delivers the same 15,970-byte world to all
three clients at `21:12:58.452-.459`. Player 3 requests quit at `.477` while
handling native event category 51 (`0x33`), not topology event 16. Players 2/4
return from world application. The later topology teardown is a consequence.

PC disassembly explains the shared return address `00887CFA`: the category-51
handler at `008877D9` jumps to the common indirect call at `00887CF8`. Read-only
live bytes agree with the mapped image except for our validated observer calls.
This is not evidence of a damaged stack or a different executable layout.

The native shutdown subtype is a signed integer at event `+24`; the flag is a
byte at `+20`, and the recipient is a 64-bit ID at `+18`. These offsets differ
from OTR and must not be copied from its event layout. The PC switch identifies
subtypes 0-9 as too many events, flush to network, server rejection, link shutdown,
lost first-party session, too few players, desync assert, dead connection listener,
host missing DLC, and client missing DLC. Quit reason 9 alone does not distinguish
these upstream causes.

The next candidate records the received subtype and observes all seven validated
direct calls to PC shutdown-event constructor `008585E0`, preserving its 16-byte
stack cleanup and original return. Inlined constructors are not covered by the
producer observer but their events remain visible at the receiver. No shutdown
condition is bypassed. Run `210721` restored all baseline hashes and closed its
owned processes.

## Desync assertion is the initiating shutdown

Run `four_instance_20260927_211819`, runtime SHA256
`8A142BFBB58FD0DF7478410CAE97206B56E5D5C248BCF3DF8ACE3B46C261B8D0`,
applies 15,969 bytes on all clients. At `21:23:57.183`, Players 3/4 construct
shutdown subtype **6**, called from `0086ECCF`, and immediately receive the same
category-51 event. This is the PC **LOST CONNECTION DESYNC ASSERT** path. The
underlying failed expression is not yet identified. No timeout extension or
forced readiness would correct that mismatch.

The constructor is called by `0086EA50`, a cdecl assertion helper taking a bool
condition, expression, source filename and line number. A new observer preserves
the native call/result and logs only false conditions, up to 32 failures. All 17
direct call sites are signature-validated before patching. They include inventory,
prop identity, broadcast sequence, network sequencing, world deserialization and
reliable send failures; the subtype alone cannot choose between them.

This run completed cleanup and verified baseline DLL, save, settings and eight
archive hashes. Its report correctly does not verify gameplay. The report now
retains shutdown subtypes separately from quit reasons and preserves bounded
assertion evidence; 84 Python tests pass, along with seven x86 fixtures and the
admission fixture.

Read-only side investigation identifies PC inventory JIP serializer `004AA2D0`
(vtable reference `00C45C8C`) by comparison with OTR's named `004AB020`. Both PC
directions already iterate **four** users: loop bounds at `004AA5B9` and
`004AA8C4`, with per-user record stride `0x78` on the latter path. Do not enlarge
these existing bounds speculatively. This does not prove that inventory is the
failing subsystem or that all per-user state is correct.

## Inventory event overtakes world restoration

Run `212632` identifies assertion caller `004BC96A`, source line 5720, for an
unrecoverable inventory mismatch. PC callback `004BC870` requires a valid actor
at payload `+10` and an item at `+14`; the latter is null on the failing branch.
Native type-name table `00D5B8B0[0x16]` identifies this as `PlayerCycleItems`,
not the separate bulk `InventorySync` event. OTR callback names alone are not a
valid mapping for these event IDs.

Run `213436` traces the precise identity: **item `000000A8`, actor user 0**.
Player 3 restores it at `21:40:13.699` and broadcasts it under sender user 2.
Players 2/4 unpack that event at `.701`, while native JIP state is 2, and cannot
find the item. They assert. The same ID resolves to non-null objects on those
clients at `.708-.709` as their own world restoration proceeds. This establishes
an ordering defect, not missing inventory capacity or a permanently absent item.
All three worlds contain 15,969 bytes. This run restores all baseline hashes.

The opt-in `-coopjipbroadcastqueue` candidate intercepts the incoming gameplay
broadcast call `00865B4D -> 007B7890`, before unpacking. It preserves event-header
and payload bytes in a bounded 256-entry FIFO while a remote client's native JIP
state is receiving/ready or world application is on the stack. Original gameplay
broadcast processing replays the FIFO only after native world application returns
with JIP 0, connected client/server/mesh state, and unchanged game/scene ownership.
Local world-restoration events and host processing use their original paths.
Network control messages are handled before this call and are not held.

Invalid lengths/identity, overflow or changed ownership abort the owned harness
child with `E043C005`; they never evict events or claim success. Native disconnect
invalidates pending events. The probe is four-player-harness-only and defaults off.
No visibility, loading, world contents, ready signals or disconnect checks are
overridden. The FIFO fixture covers byte ownership, wraparound, no eviction,
length/type checks, reset and zero/max payloads.

Control `four_instance_20260927_214248` completed with this candidate. All three
clients applied the 15,970-byte host world at `21:48:27`, replaying 30/34/35
retained events respectively. All four human slots became unhidden with the
correct local/remote ownership in every process. Final snapshots retain native
connected meshes, active games and completed JIP/loading; the 90-second observation
interval recorded no faults, teardown, native quit, shutdown or desync assertion.
All baseline hashes were restored and no DR2 process remained. Candidate SHA256:
`D69909459A881E8231229CCF54FBC86D12C82D990739F4928EE73B60B8C264C2`.

The host Friends overlay was closed with one private, acknowledged Escape press.
Final captures show safehouse gameplay and multiple Chucks, but independent
movement/replication and complete four-player gameplay have not yet been proved.
This is the first stable four-client campaign-entry control, not a release.

## Phase 2 retry link shutdown

Run `four_instance_20260928_160936` admitted all three clients and reached four
visible human actors in every process. `combat-phase2-baseline.json` was captured
at 16:17:13 with each owner at 400/400 health and local kill counters 4/0/0/0.
That checkpoint is structurally valid, but the run is not combat evidence because
the native session subsequently shut down before reaching enemies.

Player 3 was first to receive category-51 connection failure subtype 3 at
16:18:43.893, then requested quit reason 9 at `00887CFA`. Player 2 followed at
16:18:45.199, the host at 16:18:45.493, and Player 4 at 16:18:49.956. Immediately
before the failure, Player 3 still reported all three endpoints in state 6; no
desync assertion, runtime access violation, or debugger fault was recorded. The
host camera command began after the first client failure and therefore cannot be
the initiating event. Subtype 3 identifies the native link-shutdown path, but the
current bounded trace does not identify which lower-level condition shut the link.

Only the four owned DR2 processes were terminated after the failure was confirmed.
The wrapper recorded `process-exited`, retained the failed evidence, restored the
baseline DLL, original save, render settings, and all nine staged archives, and
verified that no DR2 process remained. Do not combine this run's baseline with a
later run or count its brief four-actor state as a Phase 2 pass.

## Phase 2 pre-flow loader stall

Run `four_instance_20260928_162320` distinguishes a later transition failure from
the earlier unexplained link shutdown. All four players reached connected mesh
state 3, retained four visible actors with owners 0-3, and captured a coherent
`combat-phase2-baseline.json` at 400/400 health. The party then moved through the
bathroom and staged at the vent using only acknowledged private input.

The host vent interaction began at 16:39:14.657. Native updates advanced from
53009 to 53037 and then stopped; the final count remained unchanged for 266,411 ms.
No `native transition: ProcessFlow` callback occurred after that interaction.
At 16:39:31.175-.372, all three clients changed the host endpoint from state 6
to state 8, reliable state from 1 to 3, and topology-listener state from 1 to 4.
The three endpoint changes occurred within 197 ms. The host continued to report
its client endpoints in state 6, which is consistent with the clients timing out
an unserviced host rather than the host initiating native teardown.

The host game thread (33492, also the recorded native-update thread) was captured
inside the same pre-flow wait path identified by return addresses `00A3A1C9`,
`009A692E`, and `009E7D5D`. `00A3A1B0` is the one-millisecond sleep wrapper called
by `009A68E0`; `009E7D58` repeatedly invokes that maintenance/sleep path while an
asynchronous load remains incomplete. The other changed host thread contained
world/physics work and data pointers, not a loader function, so it is not a patch
target on this evidence.

`analyze_transition_stall.py` requires all of these independent facts before it
returns `pre-processflow-loader-stall`: a completed host E trigger, a trailing
native-update plateau of at least ten seconds, no post-trigger ProcessFlow, all
three client timeout triplets within two seconds, and the captured host wait-stack
signature. Missing evidence fails closed. `report_harness.py` includes the result
under `transition_stall_analysis` without converting it into a gameplay pass.

The actionable boundary is now before ProcessFlow and before network teardown.
The next candidate should observe asynchronous load job completion and the vent's
two-interaction sequence. Do not extend network timeouts or force completion flags;
that would conceal the loader defect and could expose partially loaded world state.
