# Dead Rising 2 Four-Player Co-op - Alpha Test

This package enables the experimental four-player campaign runtime for the Steam PC version of Dead Rising 2.
It does not contain Capcom game files. Every participant must own the game and use this exact mod release.

## Drag-And-Drop Install

1. Close every Dead Rising 2 instance.
2. Open the ZIP. It contains one top-level folder named `Dead Rising 2`.
3. Drag that folder into your Steam `steamapps\common` directory and approve merging it with the existing
   `Dead Rising 2` folder. No official game file is replaced.
4. Start Dead Rising 2 normally through Steam.
5. Use the stock `JOIN CO-OP GAME` flow. There is no extra menu button.

After merging, `dinput8.dll` and `four_player_coop.ini` must be beside `deadrising2.exe`.

The release manifest contains SHA-256 hashes for both mod files. Do not combine this alpha with another mod that also
uses `dinput8.dll`; Windows can load only one file with that name from the game directory.

This build searches only for lobbies advertising its exact `dr2_4p_protocol` value. Vanilla or incompatible modded
lobbies should not be returned, and incompatible peers should be rejected. Remote Steam behavior is still alpha and
must be validated by the test group.

Alpha 4 fixes a production-only defect in Alpha 3 that left the Steam matchmaking and callback hooks disabled unless
trace logging was enabled. That defect left real lobbies at the vanilla two-member limit, removed invite options once
Player 2 joined, and prevented Players 3 and 4 from being admitted. Alpha 4 now installs those hooks during ordinary
Steam launches, requests a four-member lobby, and keeps Steam's native invite path intact. The next group test should
confirm that invite options remain available after Player 2 joins and disappear only when the lobby reaches four.

Alpha 5 fixes the next live-test finding: DR2 requests an invisible Steam lobby (`type 3`), which is searchable but is
not visible to friends. In production four-player mode, Alpha 5 maps only that invisible type to a public lobby
(`type 2`) and prevents a later `SetLobbyType` call from hiding it again. Protocol filtering still keeps vanilla and
incompatible mod builds out of matchmaking. Alpha 5 also corrects the Steamworks `bool` ABI used when setting lobby
metadata, limits, and visibility.

## Remove Or Reset

Close the game and delete `dinput8.dll` and `four_player_coop.ini` from the Dead Rising 2 directory. Steam's Verify
Integrity feature can repair official game files, but it normally does not remove extra mod files, so delete these two
files first if you want the mod completely gone.

## Test Group Rules

- All players use the same ZIP and version.
- Do not mix proxy-DLL mods during the first remote tests.
- Keep saves backed up; this is an alpha runtime.
- Record which player hosted, lobby/join order, area transitions, disconnects, crashes, and whether each player
  retained movement, camera, inventory, combat, damage, KO, and revive behavior. Those gameplay systems passed the
  local four-instance test; this group is validating them across real Steam PCs.

Current release: `0.1.0-alpha.5`. Network protocol: `1`.
