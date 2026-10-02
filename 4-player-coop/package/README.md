# Dead Rising 2 Four-Player Co-op - Alpha Test

This package enables the experimental four-player campaign runtime for the Steam PC version of Dead Rising 2.
It does not contain Capcom game files. Every participant must own the game and install the exact same mod release.

## Install

1. Close every Dead Rising 2 instance.
2. Extract the entire ZIP to a normal folder.
3. Run `Install-DR2FourPlayerCoop.cmd`.
4. Start Dead Rising 2 normally through Steam.
5. Use the stock `JOIN CO-OP GAME` flow. There is no extra menu button.

The installer checks the package hashes and game folder. If another mod already owns `dinput8.dll`, it backs that
file up before installing. It records the exact installed hashes so a later update or uninstall cannot silently delete
an unrelated change.

This build searches only for lobbies advertising its exact `dr2_4p_protocol` value. Vanilla or incompatible modded
lobbies should not be returned, and incompatible peers should be rejected. Remote Steam behavior is still alpha and
must be validated by the test group.

## Uninstall

Close the game and run `Uninstall-DR2FourPlayerCoop.cmd` from the same extracted release folder. The uninstaller
removes this release and restores the `dinput8.dll` and config that existed before installation, if any. It refuses
to delete `dinput8.dll` when another program or manual update changed that file after installation.

## Test Group Rules

- All players use the same ZIP and version.
- Do not mix proxy-DLL mods during the first remote tests.
- Keep saves backed up; this is an alpha runtime.
- Record which player hosted, lobby/join order, area transitions, disconnects, crashes, and whether each player
  retained movement, camera, inventory, combat, damage, KO, and revive behavior.

Current release protocol: `1`.
