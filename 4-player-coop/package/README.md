# Package Files

The eventual release ZIP places these authored files in the Dead Rising 2 directory:

- `dinput8.dll`: built from this repository.
- `four_player_coop.ini`: enables the production four-player runtime.

No frontend archive is required and the stock menus remain unchanged. Every participant must install the same release.
The runtime searches only for Steam lobbies advertising its exact `dr2_4p_protocol` value. It publishes the same value
for each member and rejects untagged or incompatible outgoing and incoming joins.
