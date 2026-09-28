# Repository boundaries

This is a source-only import of the co-op project on September 27, 2026. The
existing research workspace has not been moved, deleted, or uploaded. Its game
assets, private test evidence, and separate DR1 and content-port work remain local.

Included here are authored co-op runtime code, test and analysis tools,
documentation, and the shared runtime and crash-monitor source they require.
Commercial assets, executable images, symbol databases, saves, dumps, screenshots,
raw logs, generated builds, machine settings, and third-party checkouts are excluded.

The `tools/case-zero-runtime` directory retains its historical name because the
co-op build depends on it. Its presence does not distribute Case Zero content.
Historical documents can reference companion-workspace files absent from a fresh
clone; use the root README and integration-testing guide for this checkout.

The imported runtime reflects the native-header-routing checkpoint recorded in
`4-player-coop/docs/current-state.md`. Historical DLL hashes identify local test
artifacts, not guaranteed reproducible builds across compiler versions and paths.
The repository setup adds offline CI and compiler-path overrides; it does not
change the gameplay hooks or claim that four-player gameplay is complete.

The companion research workspace remains active. Publish later work by explicitly
comparing source changes, preserving independent edits, and running these tests.
Do not bulk-copy evidence into this checkout or clean the companion workspace.
Git history starts with this import, not with the earlier experimental runs.

No new distribution license has been selected for the imported code. This is an
unofficial project; no rights to Capcom's assets are granted or implied.
