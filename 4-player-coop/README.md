# DR2 4-Player Co-op Campaign

Current status (2026-09-27 audit): **experimental harness, not playable four-player campaign co-op**.
Hidden hardware-rendered frontend loading, native lobby admission for four members, four-actor remote activation,
and a two-member native state-transfer chain have been observed. Client-side listener startup plus native link-capacity
setup before frontend JoinLobby resolved the rejected mesh handshakes in control `20260927_170127`. All four passed
the mesh-connected gate and all three clients requested campaign transfer, but three instances then crashed with an
access violation. Follow-up `171047` localized it to clothing allocation: all 13 clothing heap IDs for Players 3/4
remain -1 despite valid actor objects. An opt-in candidate now adds separate backing storage and native lifecycle
hooks, with passing offline tests. Follow-up `173745` completed with all four meshes connected, all 52 clothing
heaps live in each process, and no crashes. All four reached safehouse level state, but remained on loading screens.
Controls `174848` and `175723` repeated that crash-free result. The harness now accepts the native YES confirmation
correctly, but Player 2 receives world data after its apply call and Players 3/4 have no pending data in final snapshots.
Native-flow control `183105` now loads and renders both instances in the safehouse in the two-player configuration,
with reciprocal co-op HUD markers and no crash. Four-player native-flow control `182227` still holds before entry.
The watcher has corrected its game-active offset from +85 to instruction-validated +8D; earlier values are invalid.
Header/recipient/cleanup control `191316` now delivers and natively applies campaign world data on all three
remote clients. Final captures render the safehouse in all four instances, but all three clients have a lost-host
dialog. This is a resolved data-transfer blocker, not stable four-player gameplay. The post-start
disconnect/transition is now the step-3 blocker; step 4 is not ready. See
`docs/world-transition-hold.md`. This is not yet a campaign pass. Replicated four-player campaign gameplay,
remote Steam sessions, and the user-facing mode selector remain unverified/unimplemented. See `docs/current-state.md`.

Started 2026-09-24. Goal: 4-player co-op story campaigns for Dead Rising 2 PC (vanilla) and the Case Zero port, with a planned main-menu toggle preserving solo and two-player modes.

Four-process boot capacity and process-local Steam identity are proven on the development PC. The host plus three hidden,
muted background clients remain alive together, each with a unique mutex, Steam ID, name and three-entry local friends
list. The four processes also discover one another over a shared local session bus, with disposable per-instance
save files on D: and no harness filename I/O sent to Steam Cloud. Ordered Steam P2P packet routing has passed a four-process ring test. This is not yet
a playable four-player campaign; game-authored multiplayer save/load and replicated gameplay remain open. Synthetic
four-member lobby callbacks already pass. Static analysis also confirms vanilla's player attributes, human-actor slots,
and `SinglePlayerToMultiPlayer` remote activation path are natively four-wide.

Shipping rules match the Case Zero port (`../docs/case-zero-additive-port-roadmap.md`):
- Additive only: no permanent replacement of vanilla content.
- Code changes go through the dinput8.dll runtime (`../tools/case-zero-runtime`).
- Content changes go through overlay folders.

Development exception: `tools/test-four-instances.ps1 -StockSafehouseContent` temporarily stages eight original PC
safehouse archives because the existing install still contains converted archives incompatible with the vanilla
save fixture. Each run first preserves the installed files on D:, records both hash sets, and restores the exact
pre-test set after its children stop. It refuses `-KeepRunning`; concurrent edits are preserved and reported.
`-NetworkSnapshots` saves read-only native listener/mesh state alongside hidden captures. Neither flag changes the
user's save or the Case Zero overlay. A connected lobby or completed mesh must never be presented as gameplay proof.

## Layout

| Path | Contents |
|---|---|
| `tools/pdb_publics.py` | dumps public symbols from an MSF 7.00 PDB |
| `tools/pdb_types.py` | dumps enums and class/struct layouts from a PDB's type stream |
| `research/otr_publics.tsv` | 84,628 public symbols from the Off the Record PC PDB (`C:\Users\Bilbo\Desktop\deadrising2otr.pdb`, exact match for `deadrising2otr.exe`) |
| `research/otr_enums.txt`, `research/otr_structs.txt` | 3,815 enums and 11,255 class layouts from the same PDB |
| `research/dr2_pc_unpacked_image.bin` | mapped vanilla runtime image for repeatable offline VA/xref probing |
| `docs/findings.md` | engine findings, with evidence |
| `docs/plan.md` | phases and open questions |

OTR names are hypotheses for vanilla DR2 until matched by code shape. The preserved mapped image above is the current
analysis source; the shipped exe's `.text` is SteamStub-encrypted.
