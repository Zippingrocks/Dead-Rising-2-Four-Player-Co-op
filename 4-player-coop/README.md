# DR2 4-Player Co-op Campaign

Source publication: [Dead-Rising-2-Four-Player-Co-op](https://github.com/Zippingrocks/Dead-Rising-2-Four-Player-Co-op).
See [local publishing notes](docs/github-publishing.md) before syncing this active research workspace.

Current status (2026-10-01): **experimental local gameplay, not a finished four-player mod**.
Native four-member campaign entry now repeats successfully with all actor/clothing slots active and native meshes
connected. Run `215228` passed isolated movement/convergence checks for all four owners; the host dropped a bat,
Player 3 picked it up, and all four inventories agreed. This goes beyond lobby/allocated-actor evidence.

Pause control `224611` passed all four initiators with the corrected acknowledgement/reply-order handling, then
passed separate post-resume movement/convergence trials for all four owners. A two-player control passed both
initiators. Private camera response and four physical Chucks in a room have since been observed, along with a normal
bathroom door interaction and replicated host movement through it. Attack inputs reach native button records;
damage is not yet established. Partner HUD widgets, combat, shared transitions, death/revive, production Steam
sessions and the user-facing solo/two/four selector still require work. See `docs/current-state.md` and
`docs/four-player-gameplay-validation.md` for precise passes, failures and acceptance gates. Do not equate
loopback identities or a rendered room with release-ready multiplayer.

Run `four_instance_20261001_141156` subsequently passed a native shared transition and separate post-load control
for all four owners. Run `four_instance_20261001_175129` passed the independent visual-identity control: Players
1/2 remained unchanged, Player 3 wore a full TIR suit, and Player 4 wore the yellow TIR jacket. The watched run
remained stable until intentional shutdown and restored every temporarily staged archive. Formal attributable
combat, KO/revive, complete HUD behavior, production Steam sessions, and release packaging remain open.

Started 2026-09-24. Goal: 4-player co-op story campaigns for Dead Rising 2 PC (vanilla) and the Case Zero port, with a planned main-menu toggle preserving solo and two-player modes.

Four-process boot capacity and process-local Steam identity are proven on the development PC. The host plus three hidden,
muted background clients remain alive together, each with a unique mutex, Steam ID, name and three-entry local friends
list. The four processes also discover one another over a shared local session bus, with disposable per-instance
save files on D: and no harness filename I/O sent to Steam Cloud. Ordered Steam P2P packet routing has passed a four-process ring test. This is not yet
a release-ready four-player campaign; full multiplayer save/load and comprehensive replicated gameplay remain open. Synthetic
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
`-StockStreamedAssetsContent` additionally isolates the original global streamed-asset archive, after a vent
transition exposed an older modified SawBlade entry with a failed compression checksum. Use both content flags
for this install's vanilla gameplay/transition controls; neither permanently removes the port experiments.
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
