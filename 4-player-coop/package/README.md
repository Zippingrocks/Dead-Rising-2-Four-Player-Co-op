# Dead Rising 2 Four-Player Co-op - Alpha Test

This package enables the experimental four-player campaign runtime for the Steam PC version of Dead Rising 2.
It does not contain Capcom game files. Every participant must own the game and use this exact mod release.

## Drag-And-Drop Install

1. Close every Dead Rising 2 instance.
2. In Steam, open Dead Rising 2's Properties, select Installed Files, then Browse.
3. Open the ZIP and drag `dinput8.dll` and `four_player_coop.ini` beside `deadrising2.exe`.
4. Start Dead Rising 2 normally through Steam.
5. Use the stock `JOIN CO-OP GAME` flow. There is no extra menu button.

The release manifest contains SHA-256 hashes for both mod files. Do not combine this alpha with another mod that also
uses `dinput8.dll`; Windows can load only one file with that name from the game directory.

This build searches only for lobbies advertising its exact `dr2_4p_protocol` value. Vanilla or incompatible modded
lobbies should not be returned, and incompatible peers should be rejected. Remote Steam behavior is still alpha and
must be validated by the test group.

## Remove Or Reset

Close the game and delete `dinput8.dll` and `four_player_coop.ini` from the Dead Rising 2 directory. Steam's Verify
Integrity feature can repair official game files, but it normally does not remove extra mod files, so delete these two
files first if you want the mod completely gone.

## Test Group Rules

- All players use the same ZIP and version.
- Do not mix proxy-DLL mods during the first remote tests.
- Keep saves backed up; this is an alpha runtime.
- Record which player hosted, lobby/join order, area transitions, disconnects, crashes, and whether each player
  retained movement, camera, inventory, combat, damage, KO, and revive behavior.

Current release protocol: `1`.
