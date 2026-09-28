# Retrom Butterscotch fork maintenance rules

This fork builds the GameMaker: Studio browser runner consumed by
`retrom-project/retrom-runtime`. It remains independent of Retrom application APIs,
databases, review workflows, credentials, and private games.

## Repository identity

- `main` is an unmodified, fast-forward-only mirror of `upstream/main`.
- `retrom/ge8294c9070a4` is the Retrom maintenance baseline and default
  branch. Changes and release tags originate there, never from `main`.
- `upstream` must point to
  `https://github.com/ButterscotchRunner/Butterscotch.git`.
- `retrom-fork.json` is the machine-readable baseline and release contract.
  Never replace its fixed upstream commit with a floating branch.
- Updating `main` may only fast-forward it to `upstream/main`. A new fixed
  baseline requires a reviewed `sync/upstream-g<12-hex-commit>` branch and a
  new `retrom/g<12-hex-commit>` maintenance branch.

## Branches and commits

- Use short-lived `fix/*`, `feat/*`, `build/*`, or
  `sync/upstream-<baseline>` branches created from `retrom/ge8294c9070a4`.
- Branch names use lowercase ASCII and hyphens. Do not create `temp`, `clean`,
  `final`, `runtime-clean`, parallel maintenance branches, or branches named
  after an agent or user.
- Keep downstream changes as small reviewable commits and squash or rebase PRs
  onto the maintenance branch; release ancestry must not contain merge commits
  after the fixed upstream baseline.
- Never force-push, move immutable tags, or delete another contributor's work.

## Runtime boundary

- This fork owns the core Web build and its host bridge. It must not import
  `retrom-runtime` or Retrom source code, HTTP routes, database types, or UI.
- A stable Web release must support lifecycle ready/exit signals, pause/resume,
  standard browser gamepads, and a bounded checkpoint that restores directly
  in a fresh runtime instance without requiring the game's own load menu.
- Checkpoint ABI `butterscotch-checkpoint-v2` includes bounded serialization of
  GameMaker map/list/queue/stack/priority/grid pools and preserves pool IDs.
  Do not reintroduce a blanket data-structure blocker or label a different wire
  format as v2; buffers, motion-planning grids, structs and persistent-room
  snapshots, particle pools, spatial audio emitters, vertex resources and game
  speed overrides remain unavailable until they have exact round-trip tests.
- A core-owned exit must be observable by the host exactly once. Once exited,
  checkpoint creation must fail and all input must be released.
- Tests use only the repository's redistributable, non-commercial fixtures.
  Never download or commit private/commercial games, credentials, or saves.
- Do not describe the persistent game save directory as an instant checkpoint.
  A candidate missing direct checkpoint restore may be used for local prototype
  work, but must not receive a stable release tag.

## Quality and releases

- Before pushing, run `python3 .github/rpg-runtime/verify-source.py`,
  `python3 tests/test_retrom_web_host.py`, and `.github/rpg-runtime/test-checkpoint.sh`.
- Web changes must also run
  `.github/rpg-runtime/build-web.sh <empty-output-directory>` and
  `.github/rpg-runtime/verify-release.py` with a valid candidate identity.
- PRs to `retrom/ge8294c9070a4` must pass
  `.github/workflows/rpg-runtime-quality.yml`.
- Release tags are `retrom-core-ge8294c9070a4-rN`, with optional `-rc.N` only
  for integration candidates. Increment `rN` for any source, build, asset, or
  adapter-contract change on this baseline.
- Existing `rpg-runtime-*` tags are immutable historical records. Never create
  another tag in that retired namespace.
- Tags are annotated and immutable. The tag workflow is the only supported way
  to build and publish the Web assets and `rpg-runtime-release.json`; never
  publish aliases such as `latest` or `stable`.
- Repository, tag, tag commit, asset filename, and adapter ABI define release
  identity. Observed SHA-256 values are cache-integrity diagnostics only.

Do not add Retrom host-product logic, games, credentials, or private test data.
