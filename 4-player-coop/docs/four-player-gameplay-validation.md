# Four-Chuck gameplay validation

The acceptance target is four independently controlled Chucks in the same native
campaign room, with their actions replicated to the other three clients. Allocated
actors, connected lobbies, or a rendered room alone do not meet that target.

## Read-only actor evidence

`tools/snapshot_campaign_players.py --pid <host> <client1> <client2> <client3>`
reads only DR2 processes and never calls game code or writes process memory.
It validates retail-PC `cSceneObject::GetPosition` at `007A1AB0` and the human
vtable entry `00C9A738 -> 007A1AB0`. The actual PC getter reads three floats at
actor `+1C/+20/+24`; OTR's named getter `007F71A0` has the equivalent body.
PC human vtable `00C9A720` is required for every non-null slot. Render positions
at `+48` are separately supported by the PC getter at `00757E50` (vtable slot 13).

The existing validated ownership chain is game global `00DDC3F0`, scene `+2C`,
actor manager `+94`, human pointer array `+0C` with four slots. User ID is read
at actor `+3A0`, remote-enable state at `+D83C`. Hidden flags at `+18/+44` are
supplemental scene-object observations, not a substitute for inspecting captures.
The probe rejects aliased actor pointers, non-finite positions, and a changed
scene/manager at the end of the read. Samples remain non-atomic.

The first live check, during `four_instance_20260927_203938` startup, correctly
finds four allocated but hidden menu actors in each process. That is specifically
not four-player gameplay evidence.

`tools/compare_campaign_motion.py before.json after.json --pid <host> <p2> <p3> <p4> --player <0..3>`
compares one isolated movement trial. It rejects missing or aliased identities,
wrong ownership, hidden actors, non-finite coordinates, changed scenes and stale
or widely separated timestamps. It measures local-player displacement and checks
all remote positions against their owners, while rejecting movement of other
owned players beyond the allowed tolerance. A positional match is not input
causality: retain the corresponding fresh down/up acknowledgements and native
connection evidence, and inspect actual rendered captures. Do not promote its
result directly to a gameplay pass.

## Required progression

### Live movement and inventory control: 2026-09-27

Run `four_instance_20260927_215228` repeated native campaign entry with the JIP
queue candidate and completed a ten-minute observation interval. Private input
acknowledgements and positional trials establish isolated movement/convergence
for all four owners: P1 `220025`, P2 `220037`, P3 `220104`, P4 `215948`.
These trials retained the original 0.25 minimum movement, 0.15 other-owner motion,
and 0.20 peer-error tolerances. Earlier crowded-spawn/idle-motion failures remain
recorded and were not reclassified as passes. No coordinates were written.

The host dropped the Spiked Bat with native X input. Player 3 walked to it and
picked it up with E. Capture `coop_capture.2.31.png` shows it held, and
`inventory-p3-pickup.json` records item ID 168 (0xA8) in user 2's first slot in
every process, absent from the host's inventory. Other users retained their own
inventories. Inventory layout was checked against PC `0045EE80`:
game+2C -> scene+90 -> managers+30, records at +10 with stride 78,
12 prop pointers at +04 with stride 8, selected index +68.

Player 4 opened the native pause menu. The partners entered waiting/menu states.
After each visible menu was closed, P1/P2/P3 still showed "Waiting for other
player" while P4's menu was closed. **Resume failed; this is a blocker**, despite
connected meshes, no recorded native faults/teardown, and successful movement
before pausing. Also unresolved: low/upward client camera framing, only one
partner HUD marker, combat/damage/revive, transitions, four bodies in one clear
view, and real Steam sessions. Baseline files and original save were hash-verified
restored; all owned processes were closed.

PC pause status storage is already four-wide at game->state(+38)+1DC.
Set/Clear are `007A1EF0`/`007A1F10`; all-user checks also read four slots.
Next observers preserve the native PauseMenu callback `0048CC30` and QuasiPause
callback `0048CFA0` at their dispatcher calls `00509E50`/`00509E5F`. They log raw
operations, sender/owner, pause masks and negotiation state before/after, without
clearing flags, changing timeouts or suppressing waits.

Read-only diagnostic `four_instance_20260927_221105` repeated the failure. Native
`GetNumPlayers` (`00853B00` -> `008507B0`) counts four distinct occupied users in
every process. The user-list entry pointers at +54/+6C/+84/+9C each contain the
expected 64-bit identity at +08. This rules out a two-player count in this path;
do not change its quorum to two or manufacture the missing vote.

After P4 first closed its menu, masks briefly reached zero on all peers, while
P1/P2/P3 still displayed menus. Closing those menus produced masks [8,8,8,0] on
all four peers, with those three clients waiting indefinitely. P4's later Escape
was acknowledged by the script poller but produced no native pause event. The
script acknowledgement alone is not proof that gameplay consumed a key.
`pause-quorum.json` and bounded before/after callbacks retain this distinction.
The next control repeats the sequence with two native participants before any
behavioral fix. All files restored and no owned game process remained afterward.

Private mouse support is opt-in (`-PrivateMouseProbe`, runtime
`-coopprivatemouse`). It clones each keyboard/mouse Device8 vtable independently
and supplies private DirectInput state/events without desktop injection or
hardware acquisition. Unit fixtures are passing; live camera/combat use still
requires validation. The pause control leaves this new input path disabled.

### Two-player control and acknowledgement-loop candidate

Run `four_instance_20260927_222441` passed client and host pause/resume with the
mouse probe disabled. Both native masks returned to zero; captures show gameplay.
After the client resumed, private S input moved only P2 by 1.2786 units, P1 moved
0.0038, and the remote P2 position agreed within 0.0334. Files/save restored.

The four-player trace has a concrete extra-message path: at `0048CD95`, every
non-owner responds not only to the pause originator, but also to other peers'
operation-0 acknowledgements. `007AB7E0` validates the new outgoing event and sets
byte +34 when any native pause mask is already >=8. Its synchronous local callback
then reaches `007D2AA0`, opening an unintended local menu. With only two users,
there is no third-party acknowledgement to provoke this loop.

The opt-in `-PauseAcknowledgementProbe` candidate gates only that response call:
forward replies to the original owner; do not reply to another non-owner's reply.
The incoming callback still records that peer's actual vote and runs the original
quorum logic. No pause flags, player counts, timeouts, or readiness states are
written. A nested thread-local dispatch context binds the call to its real scene
and incoming event; unexpected context fails the owned harness run. Exhaustive
four-owner fixtures preserve four genuine votes. Live validation is required.

First candidate run `four_instance_20260927_223424` resumed P4 correctly, with
all four masks zero and gameplay captures. P4 then moved 1.887 units, but the
strict convergence trial failed because an unrelated peer pair was 0.217 units
apart (limit 0.20); retain that result, do not relax the tolerance. Host-initiated
pause then stalled with mask [16,0,0,0]. Later P2/P3 input in that same batch is
contaminated by the failed host cycle and is not an independent test.

The host trace shows all four genuine votes, but P3/P4 replies had byte +34 set:
they received another peer's acknowledgement before the original host request.
The next revision preserves the original request's flag on its bound reply after
the original `007AB7E0` validator returns. Only that exact event and scene in the
nested send context are eligible; rejected validation and unrelated events stay
unchanged. Native votes, statuses and resume processing are still untouched.
Fixtures cover all prior-peer subsets and mismatched/rejected scopes.

Revised live control `four_instance_20260927_224611` passed native pause cycles
for all four initiators: P1 `225254`, P2 `225308`, P3 `225320`, P4 `225330`.
Each trial validated all four real users, paused masks (16 for the initiator,
8 for each partner) and all-zero resumed masks. Captures return to gameplay.
Post-resume movement/convergence passed P1 `225430` (1.0018 units), P2 `225443`
(1.1119), P3 `225456` (1.3699), P4 `225523` (1.1781), at unchanged tolerances.
Earlier P4 trials `225350` and `225507` failed peer convergence/other-owner motion
respectively; these remain failures. Ordinary A/D inputs separated the spawn
group before the passing trials. No actor positions or pause statuses were written.

`test-campaign-pause.ps1` now requires a clean unpaused baseline, validates the
expected real pause masks for every participant, and stops on the first failed
open/resume check. Its completed result still requires capture inspection and
post-resume movement. A refusal against an already-exited harness sent no input.

`snapshot_campaign_pause.py` now captures signature-validated masks/counts for
either control size; it rejects unsupported code and changing ownership. Zero
masks alone are deliberately not labeled a gameplay pass.

HUD follow-up: PC `0081E2F0` already iterates four users (`0081E7D3`), but calls
`007FBF00` with the single co-op widget branch for each partner. That branch uses
the same widget fields (+18/+1C/+20), explaining why the last partner overwrites
earlier labels. The existing three-health-bar override does not allocate three
campaign partner widgets. Do not fix this by changing another loop bound or by
borrowing NPC slots without accounting for real survivors.

Combat instrumentation: `snapshot_campaign_inventory.py` validates the item
getter and local health accessors before observing twelve slots per user and the
native local health source. PC `006BCF77` reads tracker `00DDE9A8` -> +08 -> +2C
for a locally owned human; `007581A0` verifies the remote flag, and the HUD division
at `0081E471` verifies maximum health at status +447C. Menu/hidden actors are
rejected. Four new fixtures cover ownership, item selection, hidden state,
unsupported signatures and non-finite health. Never write inventory or health
to simulate gameplay.

The next private-input candidate also intercepts only the opted-in silent
harness's Set/GetCursorPos, ClipCursor, ShowCursor and SetCursor imports. It
maintains process-private position/shape/display-count state without invoking
desktop cursor APIs. Other modes still forward the original calls. Fixtures
check isolation, round trips and null-pointer handling. Unpacked engine images
now require an explicit absolute `DR2_COOP_DUMP_ROOT`; future harnesses set this
to their D: run folder rather than writing an image into the game directory.

### Private Input Live Control

Run `four_instance_20260927_225646` retained four connected native players for its
600-second observation and restored all staged files. P4 pause/resume passed with
the private mouse/cursor path enabled (`pause_20260927_230726_935_p3`). A single
relative mouse command was consumed but made little visible difference; sustained
bounded pulses produced clear native host rotation (captures `0.24`/`0.25`) and
P4 vertical rotation (`3.26`/`3.27`). No camera/actor memory was written.
The PC mouse mapper at `00A59670` clamps each normalized sample; use
`invoke-harness-camera.ps1` for repeatable bounded sweeps, not one huge delta.

One P4 attack input was consumed, but all local health readings stayed at 400.
This is not damage or combat proof: facing, reach and native friendly-fire rules
were not established. Motion trials `230740` and `231246` failed the existing
strict criteria. The latter moved P4 2.138 units, with its copies within 0.10, but
P2 disagreed by up to 0.545 between peers. Preserve the failure; separate the
crowded spawn group with ordinary input and repeat before attributing a cause.

Sustained camera input also exposed a tool release race: the game's file reader
briefly denied deletion, and the helper silently ignored it. Sequence 44 remained
after a falsely completed result; the next CreateNew correctly refused to replace
it. The exact zero-button command was verified and removed. Release now retries
bounded transient deletion errors, checks exact command contents, verifies absence,
and fails instead of reporting success if cleanup is blocked. Fixtures cover three
transient failures and persistent failure. This is harness safety, not a game fix.

The mapped-image diagnostic was successfully redirected to this run's D: folder
(11,411,456 bytes). The preexisting C: image was unchanged by the run.

### Repeated Gameplay And Native Control

Run `four_instance_20260927_231329` repeated four-member world entry and a
600-second observation. Strict motion checks passed P1 `232144`, P2 `232224`,
P3 `232259`. P4 `232312` failed because P2's remote position differed by 0.220;
later P2 `232900` and P4 `232913` failed other idle-peer errors up to 0.265. These
are not blanket passes. The host bat attack was consumed and captured, but all
local health readings remained 400; contact, safehouse restrictions and damage
replication remain unproven. The wrapper verified restoration and child cleanup.

Native two-player control `four_instance_20260927_233048` uses the same private
input build without the four-player NFS/JIP/pause extensions. Its untouched idle
samples (`idle-start.json`, `idle-after-two-minutes.json`) show owner drift only
0.015/0.032, yet peer errors 0.108/0.210. Thus small above-0.20 idle errors also
occur in this two-player control. This does not explain the larger four-player
errors or justify silently relaxing thresholds. `compare_campaign_motion.py`
and the motion driver now accept explicit two-player controls, retain the same
limits, and reject visible actors outside the declared capacity.

The signature-validated `snapshot_campaign_camera.py` locates the PC main
viewport through `007A0CA0` (game +30, viewport index 14), verifies the viewport
constructor `00894870`, embedded camera at +18, manager at +D8 with a +0C
back-reference, and native camera constructors. PC camera position/view/up are
+04/+10/+1C, FOV/near/far +28/+2C/+30; these differ from OTR alignment. It reads
only and validates finite orthonormal vectors, projection and local ownership.
Live two-player reads show 43-degree FOV, near 0.1, far 1000.

`invoke-harness-waypoint.ps1` uses that observer plus individually guarded short
W/A/S/D inputs, never actor/camera writes. Targets are bounded to ten units,
input strokes to 200/300 ms, step count to 30, and three non-improving samples
stop the route. Scene/actor changes also stop it. First live trial correctly
stopped after exposing an incorrect right-vector sign. The corrected native
basis is view cross up: a +X view's D input moves +Z. Two-player live waypoints
(8.6,20.4) then (11,20.4) reached the visible bathroom passage. More distant
targets met real geometry and stopped; no area transition is claimed.

The observation-only harness duration can now be up to 1800 seconds, retaining
the existing resource monitoring and finally cleanup. This changes no engine
loading, pause, network, or disconnect timeout.

Offline trigger audit: PC `00482F10` matches OTR's cooperative trigger-radius
routine and already loops four visible actor slots at `00482FE7`. In contrast,
PC `00437300` (matched remote-player helper) returns 1 for local 0, otherwise 0;
direct callers are `004F07F2`, `007CF357`, `00829690`. Audit their semantics before
changing this single-partner helper; do not blanket-patch unrelated loops.

### September 28 Input And Door Research

Run `four_instance_20260927_234621` uses the private-input build with a
30-minute observation budget. It completed with all four processes alive until
deliberate cleanup and no recorded native assertion, JIP/pause failure or teardown
call. The wrapper verified restoration of the baseline DLL, original save,
render settings and eight archives. Host captures `0.22` and `0.31` show
four distinct physical Chuck bodies (edge actors are cropped), separate from
the mirror reflections. P4's ordinary waypoint move to (7.5,24.5) also changed
its own camera position, so the earlier identical idle client camera samples
were not sufficient evidence of a camera ownership bug.

`snapshot_campaign_input.py` validates PC `0044C780`, its record addressing,
and scene+54 use in `00492050`. Local actor+378 selects one of 16 device
records; local keyboard actors here use index 4, not their network user slot.
The PC table has 95 records of 28 bytes at interface+448. It is not OTR's
103-button enumeration. Read-only samples during held left-click show native
indices 1,7,30,31,45,72 (then 10/8 as held duration increases); E shows 73.
These are raw state observations, not labels or proof of damage. All four
local health readings remained 400 in `combat-before/after.json`.

The private input helper can retain five native samples during a press and
always releases its exact owned command after observer failure. Nested JSON
now uses depth 10; the first 23:54 observations were depth-truncated and must
not be treated as complete raw records. Observer ownership/failure/release and
nested evidence are covered in the PowerShell fixture; 109 Python tests pass.

A read-only y=0 collision slice of the stock safehouse explained navigation:
the x=9..12,z=20..22 opening is a closed alcove, not the bathroom exit. The
actual doorway is on x~3.4,z~24. Interaction requires a centered approach;
off-center E selected the nearby shower head instead. The host naturally
swapped its bat (0xA8) for that floor item (73), then the centered door displayed
Open/E and opened normally. `navigation_20260928_000951_148_p0.json` reached
(1,24) beyond the doorway. `door-open-players.json` records the host on all
four peers within about 0.11 units. This proves a normal local door interaction
and replicated movement, not yet an area transition or every-peer door state.

Each client subsequently traversed the same door through its own input: P2
`001259`, P3 `001421`, P4 `001508`. `all-four-corridor.json` records all four
actors beyond the bathroom, with each remote position near its owned position.
`corridor-inventory.json` agrees on the host's floor-item swap (selected ID 73)
in every peer. P4's return-and-pickup attempts did not acquire item 0xA8;
`p4-pickup.json` and `p4-pickup-second.json` retain null P4 inventories. No item
contact/prompt was established in those views, so neither success nor a P4-only
pickup defect is claimed. Host navigation reached the south corridor corner,
but no area-load trigger was activated. The next observation can run up to
3600 seconds; this only changes the supervised harness deadline, not game timeouts.

The next run (`four_instance_20260928_002321`) adds read-only prop observations.
PC `004105E0` bounds dynamic-prop indices to 2048 and reads manager+30; callsite
`006140C5` locates that manager at scene+90 -> +20. PC `0044EFA0` validates the
inline/heap prop name getter. `00865E36` validates prop+AC -> +40 identity, and
`0048B1D8`/`0048B1FF` use the same metadata's flags and position for distance
culling. `snapshot_campaign_props.py` checks these signatures, finite positions,
bounded string termination, and stable scene/table ownership. Live host output
finds ID 168 named SpikedBat at the held bat's expected location; 113 Python
tests pass. These metadata positions are not a blanket physics-pose verdict.

Live prop names identify the actual `door_vent` exit at approximately
(-9.194,-2.755,37.653), not the eastern breach-door corridor. This corrects the
earlier speculative exit route. The bathroom door is ID 148,
`Door_Survivor_Room8`, at (3.486,-1.118,24.496). The pivot location is not the
interaction point: successful approach was approximately (4.2,23.8), then E.

Player-four handoff subsequently passed in this run. Host S400 faced away from
the wall, then X150 dropped item 168. `bat-dropped-props.json` finds SpikedBat
in all four native prop tables near (7.6,-1.02,23.8). P4 navigated to (7.6,23.2),
used D150 to face it, then E500 to pick it up. `p4-bat-pickup.json` agrees on
P4 selected item 168 in every peer, with host items 167/166/164 and no 168.
Capture `coop_capture.3.22.png` visibly shows the bat in P4's hands. No inventory,
actor or prop memory was written. This resolves the earlier ambiguous P4 pickup
attempts; it does not establish combat damage.

### First Vent Transition And Content Isolation

Run `four_instance_20260928_002321` reached the exit through ordinary movement
on all four owners. `all-four-at-vent.json` retains the four native actor tables;
P4 still carried the transferred bat. The host's normal E press at 00:52:01
triggered the vent fade. This is a real area-transition attempt, not a pass:
all four processes exited with C0000005 at about 00:52:07.

Each runtime trace identifies `geo_sawbladeShape D3DVertexDesc 0`, failed
CreateVertexDeclaration, and null dereference at PC `00AB3143`. Host virtual
usage was about 1240 MiB of 2047 MiB, so this was not identified as an address
space exhaustion. The installed global `streamedassets.big` still contained
older port experiments; safehouse-only isolation did not remove them.

The live archive SHA256 was
`eaf6ccc548993b5414ddd59d7decc93b9c33b368262d5eebda198d54254239b3`.
Its `SawBlade.big` compressed entry SHA256 was
`b1662adba48676b6dc82c70b25f2f978be6d53d89d4bfb3223b92a524d720d7c`;
strict zlib decompression fails with `incorrect data check`. Raw inflation
(diagnostic only, not acceptance) gives 138544 bytes. The preserved PC archive
SHA256 is `f8dbf120ecf59a6f5d4eb2c0f7b8fb180f0269aba50ca5e2a4b51fcca9f569e7`;
its entry SHA256 is
`276b35358440b943142156424d6d9fc127d3850a43e0e5fa79138083904b7c0a`
and strict inflation passes at 138516 bytes. This is a concrete content
confound; it must be removed before attributing the crash to four-player code.

`-StockStreamedAssetsContent` now optionally includes this entire original
global archive in the same pre-copy backup/hash manifest and conflict-aware
restoration as the eight safehouse archives. It cannot be combined with
KeepRunning. Safehouse-only, global-only, and combined nine-file fixture tests
pass, including preservation of concurrent edits. The port experiments are
restored afterward, not deleted or repaired as part of this co-op test.

The previous 15-second debugger cleanup limit interrupted dump finalization;
the host dump has no usable stream directory and is not valid diagnostic
evidence. Runtime text still preserves the fault. Monitor completion now has
a 180-second per-monitor grace period and reports incomplete evidence on
timeout. This is not a native game timeout change and does not promise every
possible crash can produce a complete dump. Baseline DLL, original save,
render settings, and all eight archives from the failed run restored exactly.

`validate_minidump.py --run-root <run>` checks thread/module/full-memory stream
structure and payload bounds without reading gigabytes of game memory. It
rejects all four interrupted dumps from this run. Future harness cleanup writes
`dump-validation.json` and explicitly marks structurally incomplete evidence.
This is not proof of useful exception context or correct fault attribution.
The Python regression suite now contains 118 passing tests.

### Four-Player Campaign Re-Admission And Local Control

Run `four_instance_20260928_135823` completed after the frontend harness stopped
treating missing menu-key release traces as fatal during background screen
transitions. The command files were still removed before continuing; this change
only affects scripted frontend navigation. The stricter gameplay input helper
still requires exact down/up acknowledgements.

The run dismissed the client save warning for all three clients and recorded
host native admission confirmations for callers 1, 2, and 3 in
`admission-input.jsonl`. The final outcome is `observation-completed`; no process
exited before cleanup. Restoration verified the baseline DLL, original save,
render settings, and all nine temporary stock archives. Peak private memory was
about 2.5 GiB across the four hidden instances.

Final network evidence shows endpoint state 6 for peers `0110000170000001`,
`0110000170000002`, and `0110000170000003`; all four confirmed slots are present.
Native mesh connection, native handshake, lobby frontend join, lobby logic join,
four-player clothing capacity, and four-player actor activation are all observed.
The clothing manager reports `reserved_players=4`, `mode=2`, and live heap sets
for players 0-3.

The post-admission actor snapshot at 21:04:54 UTC shows all four actor slots
visible and unhidden in every process, with the expected local owner marked by
`remote_enabled=0` in its own process and remote owners enabled in peers. A host
capture from the same run shows partner HUD presence for Player 4, but this is
not yet complete HUD validation for all partners.

After admission, four private harness W-key pulses were sent at 14:08:54-14:08:57
local time, one to each instance. Each pulse recorded exact down/up acknowledgement
from that process. The post-input snapshot at 21:09:09 UTC shows positions changed
and replicated across peers: players 1-3 moved materially from the admission
cluster, while the host also accepted local input. This establishes local
four-player campaign control evidence in the safehouse.

The automatic report still marks `four_player_campaign_verified=false` because it
does not yet fold the manual post-admission movement snapshot into that aggregate
field. Treat the individual evidence above as the current gameplay result. Combat,
damage, revive/down states, complete partner HUDs, another shared area transition,
production Steam sessions, and the user-facing solo/2P/4P selector remain open.

### Acceptance Gates

1. All three frontend clients request their own lobby join; host native membership
   confirms the matching identities. Each input press and release is acknowledged
   by that exact process's log, not inferred from writing a command file.
2. Native mesh handoff, world delivery/application, and completed loading on all
   four clients, with no teardown, runtime faults, or lost-host dialog.
3. All four campaign human actors are visible in the same room. Confirm local
   ownership for each instance and validate the view against actor positions.
4. Move one player at a time with private test input. Record before/after positions
   in all four processes. The intended player must move; other owned players must
   not mirror the input; remote copies must converge. Repeat for every player.
5. Check pickup/drop, attacks and damage, inventory ownership, partner HUDs, pause,
   death/revive, and a shared level transition. Record failures individually.
6. Repeat a bounded soak and retain solo/two-player controls. Real Steam sessions
   and the user-selectable two/four-player mode need separate validation; loopback
   identities are not evidence of production Steam matchmaking support.

Do not force visible flags, teleport actors, clear loading state, suppress native
disconnects, or extend engine timeouts to manufacture success. Use disposable D:
save copies, hidden/muted windows, owned-process cleanup, and verified restoration.
