# Contributing

This project is still reverse-engineering and integration research. Read the current-state document before
changing a hook; preserve proven two-player behavior and keep unverified four-player changes opt-in.

## Working Rules

- Build and keep generated evidence on D:. Do not run interactive game tests in CI.
- Validate retail PC instruction signatures and calling conventions before patching. OTR symbols are research
  clues, not interchangeable DR2 offsets or enum values.
- Distinguish process survival, native membership, world application, rendered entry, and gameplay replication.
- Never force readiness, clear loading flags, or suppress a failure to produce a passing report.
- Use disposable per-instance saves. Do not overwrite the player's save or write harness saves to Steam Cloud.
- Keep tests hidden, muted, and nonactivating. Close only processes owned by the test.
- Preserve installed-file backups and concurrent edits. Verify restoration after every integration run.

Run the offline checks in README for changes to shared behavior. Add a focused regression for each diagnosed
ownership, layout, or parser defect. Include the test run ID, runtime hash, observed result, and remaining
limitations when documenting live controls. Preserve historical evidence; add corrections instead of rewriting it.

Before committing, inspect `git diff --cached --stat` and `git diff --cached --name-only`. Do not force-add excluded
files. No game binaries, extracted content, PDBs, mapped images, saves, raw logs, screenshots, or crash dumps belong
in commits or Actions artifacts. Do not add a redistribution license for third-party material.
