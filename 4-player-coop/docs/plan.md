# Plan

## Current gate (2026-09-25 corrected audit)

1. Preserve the stock-layout four-member checkpoint `202242`; reject the old "stable transport" result.
2. Use a genuine native cLocalServer and local client. Never reuse a cRemoteServer or seed fabricated confirmed slots.
3. Four-member native handshake/confirmation is proven. Correcting the campaign's separate one-remote-link limit
   at `0x0087C8CA` was essential; transport array expansion was unnecessary with the correct topology.
4. Require fresh periodic endpoint evidence for every expected peer; process survival alone does not pass this gate.
5. Wrapped local save I/O passed in all four instances (`203937`). Next diagnose the hidden frontend stall, then
   validate game-authored saves and controlled campaign entry using the disposable D: store.

Earlier "host roster gate passed" notes below refer to synthetic population, not genuine joined player membership.

## Phase 0: research (in progress)

1. Recover vanilla's `eUserPlayers` layout: user, none, all and virtual values. Match OTR functions to vanilla by code shape; start with the 17 callers of `IsValidUser` at `0x007A1B20`.
2. Find where story co-op caps at 2 (native count, HUD, actor storage and four-record remote activation confirmed):
   - session/lobby size;
   - P2P peer-group size;
   - join participant construction before `cP2PClient::SinglePlayerToMultiPlayer` (host allocation and completion are
     now confirmed dynamically sized; the native server retains four `0x48` client records, so map client connect,
     populate their peer IDs and drive confirmation);
   - join handshake;
   - HUD slots.
   Compare against the TIR path (`cGameModeGameShow`), which already runs 4 players.
3. ~~Confirm the online backend and multi-instance blockers.~~ Done (findings §6-8): Steamworks lobbies, callbacks,
   loopback P2P, four synthetic identities and private save namespaces are implemented. The real host now allocates four
   records. Next: connect three native clients, route its legacy socket traffic and validate DR2's own packet stream.
4. List the per-player arrays that exist only for 2 in the story path, but for 4 in TIR. The main
   `cGameScene` player-attribute array is confirmed four-wide in vanilla (`4 * 0x68`) versus two-wide in OTR
   (`2 * 0x88`). Human actor pointers, `AddHumanActor`, and `SinglePlayerToMultiPlayer` activation are also four-wide;
   continue with inventory, pause/cinematic negotiation, HUD identity and join-transfer serialization.

### Multi-instance gates

The harness advances in explicit safety gates:

1. **Boot capacity (passed):** four minimized processes, unique mutexes/logs, low render settings and measured CPU/RAM. No campaign load.
2. **Identity and storage I/O isolation (passed; game-authored saves pending):** each process has a stable local
   Steam ID, persona name, three-entry local friends list and private D: save store. In-process disposable round trips
   passed in `203937`; harness filename I/O never falls back to Steam Cloud. Actual campaign save/load remains untested.
3. **Loopback transport (discovery, packet routing, synthetic lobby callbacks and native host allocation passed):** all
   four processes register and heartbeat on a shared local session bus. Steam P2P queues and four-member lobby callbacks
   work, and the real host retains four client records. Native `cP2PClient::Connect` constructs a `cRemoteServer` on clients;
   the host constructs a genuine `cLocalServer` and joins its local client. Two native records are confirmed without
   synthetic writes. Four-member native membership is now proven with stock layouts and sequential joins. The former endpoint state writes, fabricated
   roster, incorrect outgoing rerouting, and wrong Steam callback overload have been removed. See the latest findings
   for each run's actual result. Do not use timed key input as evidence of title or lobby arrival.
4. **Story test:** host plus three clients enter a controlled room only after the first three gates pass.

`tools/test-four-instances.ps1` implements boot/network probes and restores the player's render settings.
It checks memory headroom before launches and during observation, records pre-cleanup exit codes, and can attach the
existing crash debugger with `-CrashMonitor`. `network-summary.json` separates process survival, fresh endpoint state,
confirmed peer identities, and native handshake evidence. It never certifies campaign gameplay.

All harness instances, including instance 0, are background-only by default. Both the launcher and injected runtime
force their windows hidden, reject focus requests and suppress Win32 error dialogs. `-coopsilent` intercepts DR2's
IXAudio2 mastering voice and its separate Bink volume setter inside each child; it does not change persisted Windows
mixer state. Multi-instance tests may run alongside other desktop work only with this background profile active.

## Phase 1: mod-only matchmaking (implemented; live Steam validation pending)

- Installing `four_player_coop.ini` enables four-player mode without changing DR2's menus.
- Searches require the exact Steam lobby metadata `dr2_4p_protocol=1`.
- Hosts advertise that protocol and a four-member limit. Every modded member publishes the protocol on its lobby-member
  record; outgoing joins and the host's native incoming admission reject missing or different protocol values.
- Increment the protocol value whenever a release changes network-visible behavior incompatibly.

## Phase 2: 4 players in vanilla story co-op

- Raise the session and peer caps from 2 to 4.
- Keep native transport layouts. The stock reliable layer has three remote endpoints plus a separate listener;
  the correctly constructed host needs no fourth remote slot. Preserve experimental expansion only as a control.
- Re-run the hidden four-instance gate and require listener state 3 plus three simultaneous host endpoints in state 6,
  with all three clients remaining in state 6 for the full test duration.
- Spawn and synchronise players 3 and 4 as Chuck, using the co-op outfit path.
- Extend the co-op HUD to 3 partners.
- Make pause, cinematic, level-switch and save negotiation wait for all players.

## Phase 3: Case Zero co-op

- The same runtime switches, in Case Zero mode.
- Handle partner spawn points in Still Creek and cutscene ownership.

## Decisions (user, 2026-09-24)

- **Testing:** a multi-instance harness runs 4 DR2 instances on one PC over loopback, for unattended tests. Phase 0 step 3 now also covers what blocks a second instance:
  - Steam's single-instance guard;
  - GFWL/Steam sign-in;
  - the game's mutex/window checks.
- **Mode selection (superseded 2026-10-01):** there is no menu button. Installing the mod enables its isolated
  four-player matchmaking pool; removing or disabling its dedicated ini restores unmodified DR2 behavior.
