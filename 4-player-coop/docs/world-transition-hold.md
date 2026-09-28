# World Transition Hold

2026-09-27. The clothing allocator crash is resolved in bounded controls; four-player campaign loading is not yet
complete. Do not infer playable actors from mesh connection, clothing storage, or safehouse level enum 40.

## Trace Control

`runtime_logs/coop/four_instance_20260927_174848` uses DLL SHA256
`56CBE6E76D5CA3C41C6EAE77B9772E09FBF757B33986017E08B92AB9797F97E9`.
Checkpoint: `builds/world_transition_trace_20260927`. All four children survived the bounded observation with
connected meshes and independent live clothing heaps. No crash was observed. Native behavior is unchanged by
the four signature-checked trace call sites; they call the original function and log bounded before/after state.

- At 17:53:51.919 the host automatically calls native state-transfer startup with four users. This does NOT depend
  on the harness's fallback scheduling log appearing. Native processing can consume consensus before the worker's
  500-ms eligibility timer fires.
- Player 2 calls native world application at 17:53:53.720 with `jipState=1`; it returns with state 1.
- Players 3/4 call the same routine at approximately 17:53:51.952 with state 0 and no buffer; it returns unchanged.
- Later Player 2 has state 2 and a 15,968-byte pending buffer, but no second world-application call is logged.
  Players 3/4 have no pending JIP buffer in the sampled hold. Delivery to all three joiners is a separate concern.
- Final captures remain on loading screens; clients report `game_started=1` and `transfer_active=1`.
  The historical `game_active=0` field was misdecoded and is not evidence. There is no four-player gameplay pass.

The trace candidate accidentally labels client byte `+94` as `active`; that is the host flag, NOT game-active.
Source logging now calls it `flag94`. The original snapshot decoder was also wrong: `+85` lies inside the previous
matchmaking-stage DWORD. PC StartGame at `0088988C` writes byte `+8D`, and AsyncWaitForClientJoining at `007A1D29`
reads that same byte. Corrected snapshots validate both instructions and carry `game_active_offset: "0x8D"`.
Only those snapshots can support game-active claims. Preserved candidate/source/log artifacts remain unchanged.

## Native World Application

PC `0088A180` matches OTR `cP2PClient::ProcessJIPWorldData` structurally. It checks local node state 3, client
matchmaking stage 2, connected mesh state 3, and **JIP state 2** before calling `00873500` with serialization
flag `0x100000`. It then calls scene completion, `StartGame`, and JIP reset only when deserialization succeeds.
Never clear its guards or manufacture the pending buffer.

Its only direct PC call sites are `0088A5A6` and `0088A61E`, inside `ProcessFlow` (`0088A210`). The latter is
dispatched at `0088ACAC`. Automatic server state-transfer startup is called at `0087578E`.
The early world-apply calls plus later pending data establish a callback ordering gap in this harness path;
they do not establish a bug in ordinary vanilla two-player co-op.

Read-only loading snapshots now validate native code signatures at `007BC02B` and `007BADA0`, then follow
`[00DDC3F0]+38` to the loading manager. They record numeric sync state, active event, event scene, and the
finalizing-start substate at `+1F8`. These offsets come from PC code, not OTR offsets. A finalizing value left
from an earlier phase is not proof that the current transition is complete.

## Admission Harness Correction

The earlier script sent C and described that as accepting the call. Captures disprove that assumption:
`173745/coop_capture.0.11.png` shows the in-game incoming call; `coop_capture.0.12.png` shows the YES/NO dialog.
The unanswered dialog remained present under the loading screen. The extra Enter recorded late in `173745`
did not clear it, after the artificial flow probes had already started loading.

`test-four-instances.ps1 -ConfirmCampaignAdmission` now confirms YES after the C pulse and captured dialog,
before admitting the next caller. Each confirmation is recorded in `admission-input.jsonl`. This is opt-in
and requires the native lobby-join/admission probes; unrelated tests keep their input sequence.
Control `four_instance_20260927_175723` keeps the exact same runtime DLL and changes only this harness input
sequence. It completed its 90-second observation with all four processes alive, connected meshes, activated actor
sets, and 52 live independent clothing heaps per process. No runtime access violation or debugger fault was recorded.
Captures `coop_capture.0.13.png` and `coop_capture.1.13.png` show the native "Exchanging game data" modal;
the final host loading capture no longer has the unanswered YES/NO dialog. Confirmation was a real harness fix,
but it did not resolve the loading hold.

- The host starts native transfer at 18:02:50.493 with four users. Players 3/4 call world application at
  18:02:50.516/.519 with JIP state 0; Player 2 calls it at 18:02:52.660 with state 1.
- Final Player 2 snapshot has JIP state 2 and 15,965 bytes pending. Players 3/4 still have state 0 and no buffer.
  Final captures are loading screens, not campaign gameplay. Disregard the old `game_active` decoder here.
- Final read-only callback snapshots find all ten flow-cache entries cleared in every process. They do not show
  who owned an earlier pending callback, so this cannot establish or exclude a prior callback-registration conflict.
- All eight live archive hashes, the baseline runtime, original save hash, and render settings were verified
  unchanged after restoration. No `deadrising2` process remained. Python regressions: 50 passed.

## Native Flow Control

Opt-in `-NativeFlowProbe` now separates native online-update handoff from the harness's READY_FOR_PLAY (3),
START (7), transfer-start, transfer-finalize, and direct client StartGame probes. The reporter checks that none of
those harness calls fired. Mesh startup, actor activation, clothing storage, and confirmed YES admission remain.
Four original-call trace sites at `00882088`, `008820E1`, `00882162`, and `00882573` log registration through
`0087CF50`, preserving its original arguments and return behavior. Trace calls are bounded and signature checked.
Candidate DLL SHA256: `796D5F407908C5F8D0B7693361C52AC088D22E4B7044E81797CE3FCC3A15B138`.
Checkpoint: `builds/native_flow_control_20260927_182227` (its snapshot source predates the offset correction).

Four-player control `182227` completed without a crash and with all four native meshes connected. No forced
flow signal ran. The host remained at Exchanging game data, Player 2 held 15,968 pending JIP bytes at state 2,
and Players 3/4 had no pending JIP buffer. No world-apply call occurred. This narrows the hold but does not fix it.

Two-player control `183105` uses the identical DLL and natural-flow settings, with stock two-player capacity:

- At 18:34:30.349 Player 2 registers command 1 with callback `0043AD00`, user `23087CC4`, pending=0.
- At 18:34:34.129 it natively registers command 3; the host starts transfer at 18:34:34.146 with two users.
- At 18:34:34.179 Player 2 applies 15,944 world-data bytes at JIP state 2; it returns at state 0 at .190.
- Captures `coop_capture.0.11.png` and `coop_capture.1.11.png` show both actual safehouse views and reciprocal
  Player 2 / Player 1 HUD markers, with no loading modal. Both survive the bounded observation through cleanup.
- Corrected snapshots show game_active=1 at +8D, loading sync state 0, active event 12, start-level substate 5,
  JIP state 0, no pending client sync callbacks, and cleared sync flags for both confirmed server records.
- All eight archive restoration flags are true; archive hashes, baseline runtime, and original save are verified.
  This establishes loaded two-player campaign rendering, not independent controls/combat/replication correctness.

The watcher now additionally validates start-level substate at manager +1F4 (`007BABE7`), all 19 sync callback
records at client +2D0 (`0087CDE5`), and four-capable server sync rows at server +54 with stride 0x48. It checks
the server vtable and local-server capacity before reading. Snapshot errors are kept separate from valid fields.
Tests cover offset disagreement, code-signature mismatch, callback ownership, and all four distinct server rows.
Python regressions: 65 passed; native ABI suites also pass.

Controls `183813` and `184847` locate the pending barrier: Player 2 is waiting at LEVEL_COMPLETE (18) while
Players 3/4 lack JIP data. All three registered native flow command 1 correctly; none reaches command 3.
All four meshes remain connected without crashes. The added request-table watcher confirms a retained completed
host request at slot 1 with receiver handle 0, while the next outgoing request is queued and both later clients wait.
See `docs/nfs-multi-client-ownership.md` for the recipient and cleanup-index diagnosis and the guarded candidate.
Keep native flow and the proven two-player result intact; do not clear loading flags or manufacture ready signals.

PC `0087CF50` stores ten 12-byte flow-cache records at client `+258`, with callback at `+4` and user pointer at `+8`.
Disassembly shows an already-pending command returns without overwriting its callback. Therefore the earlier idea
that a harness signal directly overwrites an existing native callback is rejected. A harness signal might instead
occupy a slot before the native request, but this remains an unproven ordering hypothesis requiring earlier traces.
The later NFS controls below establish the delivery defect independently of that callback hypothesis.

## All-Client World Application

Header-routing control `four_instance_20260927_191316` resolves delivery to all three remote clients. Each accepts
sequence-0 metadata and all 15 payload segments with no intake rejections. Each natively registers command 3;
host transfer starts at 19:18:49.570 with four users. Players 2/3/4 call world application at
19:18:49.596/.592/.601 with JIP state 2 and 15,967 bytes each, returning at .617/.612/.618 with state 0.
No forced harness READY/START/world-apply calls are needed. See `docs/nfs-multi-client-ownership.md`.

The first post-application snapshots/captures expose a different failure. Players 2/4 render the safehouse behind
"Lost connection to host player. Returning to the main menu." The host and Player 3 have entered another transition.
Capture set `coop_capture.*.17.png` was manually inspected. A client listener is shutting down by 19:18:50.009;
the final scripted confirmation occurs much later at 19:19:15.901. Its cause is not yet established. Do not infer
packet loss, insufficient memory, a two-player guard, or successful gameplay from the world-apply calls alone.
Final set `coop_capture.*.20.png` shows safehouse geometry in all four instances; all three clients have the
lost-host dialog and all four final meshes are disconnected (state 7). No process exits prematurely and no
runtime/debugger exception is recorded. All eight archive hashes, baseline DLL, original save, and render settings
are verified restored; no DR2 process remains. This preserves a useful world-application fix while failing the
required connection-stability gate.

Step 3 (actual campaign load on all four) is incomplete. Step 4 (independent input and replication) is not ready.
