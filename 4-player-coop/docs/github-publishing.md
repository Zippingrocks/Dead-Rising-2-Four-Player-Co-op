# GitHub publishing checkpoint

Repository: https://github.com/Zippingrocks/Dead-Rising-2-Four-Player-Co-op

Latest published commit: `6ed34ecc02c13744abf65d90c47e5c2d1128c69c` on `main`.
This continuation adds bounded original-call teardown tracing, per-instance lobby
join evidence, seven additional Python regressions (77 total), and the research
note `native-teardown-investigation.md`. Two-player control `201427` remained
connected; four-player `202025` never admitted Player 3 and therefore did not
exercise the disconnect. All test children were closed and installation/save
restoration was verified. The publication checkout was clean after push.

Workflow: https://github.com/Zippingrocks/Dead-Rising-2-Four-Player-Co-op/actions/runs/36374027075

The initial-import details below describe the earlier publication only.

Source-only checkout on D:

`D:\.codex\.codex workspaces\DR2-Porting-Workspace\repos\Dead-Rising-2-Four-Player-Co-op`

Initial published commit: `4890040c32c80e35f53dc25c559c6d6ba8ce5818` on `main`.
This is a source snapshot of the native-header-routing checkpoint, not a relocation
of the active research workspace. Do not discard either tree or bulk-sync them.
Compare later source changes explicitly, preserving other agents' edits.

The checkout contains the co-op runtime, harness, offline tests, research docs,
shared runtime source, crash-monitor source, and synthetic minidump parser. It
also adds a root README, contribution and integration guides, ignore rules, and
offline Windows CI. Three native build/test scripts accept `-VcVarsPath`; two
source files have trailing blank lines removed. Gameplay hooks were not changed.

Local validation passed: 70 Python tests, two Node dump-parser tests, seven x86
native fixtures, synthetic content backup/restore checks, runtime build, monitor
build, and a hidden WOW64 snapshot fixture. The runtime retains existing compiler
warnings. No game was launched, no installation files were modified, and no new
gameplay success is claimed by this publication.

Keep game assets, binaries, symbols, saves, dumps, captures, raw logs, and generated
builds local. This import contains none of them. Existing Case Zero, Case West,
DR1, and other agents' work remains in its own workspace. Future commits must
inspect staged filenames and exclude proprietary inputs and runtime evidence.

The live gameplay gate is unchanged: the four clients render the safehouse after
native world application, but remote clients lose the host connection. Stable
four-player campaign entry and independent-input gameplay are still outstanding.
