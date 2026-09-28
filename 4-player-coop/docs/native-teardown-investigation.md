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
