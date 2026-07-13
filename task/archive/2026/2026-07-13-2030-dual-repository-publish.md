# Publish Hardening Update To Private And Public Repositories

## Scope

- Prepare a user-approved public update note before any commit or push.
- Publish the complete product changes to the private development repository.
- Publish only public product/source changes to the public repository, excluding private agent/task metadata.
- Preserve the existing public history and avoid force pushes.

## Exclusions

- Do not push, create a GitHub Release, upload binaries, or open a PR before the user approves the public update note and scope.
- Do not expose private task records or private-only repository workflow metadata in the public repository.

## Ordered Work

- [x] Read the GitHub publish skill and verify `gh` availability/authentication.
- [x] Inspect local changes, repository visibility, remote heads, and public/private history relationship.
- [x] Draft the public update note and safe dual-repository push strategy.
- [x] Obtain user approval for the public note and initial push scope.
- [x] Record the user's expanded request to release version 1.1.0 with a public GitHub Release and Windows ZIP.
- [x] Create intentional product and private-metadata commits, rerun focused checks, and push without force.
- [x] Verify both remote heads, public Release assets, and archive this task.

## Evidence

- `gh 2.96.0` is authenticated as `JumpTwiceShou` with repository scope and SSH git operations.
- Current `origin` is the private repository `JumpTwiceShou/bilibili-stream-for-obs-dev`; its `main` equals local HEAD `a28f58a9716ccbf3c4c00bf677c9f7a02b409600`.
- Public repository `JumpTwiceShou/bilibili-stream-for-obs` is public and its `main` is `b35ddfedd33e5b4bc8b61604e35d78cd2c2bc618` (`Release 1.0.0`).
- Public `main` is an ancestor of private `main`; private-only commits add migration/agent/task metadata and must not be copied into the public history.
- Working tree product changes and task records were all created during the current audit/hardening/fix workflow; no pre-existing user edits were present.

## Proposed Strategy

- Product commit: source, build configuration, tests, localization, changelog, and public README changes; exclude `docs/local-integration-notes.md` and `task/**` from the public publish.
- Private metadata commit: integration-note and archived task records, pushed only to the private repository.
- Push private `main` normally to the configured private `origin`.
- Apply the product commit on top of public `b35ddfe` and push public `main` normally, with no force push.
- User expanded scope after approval: bump to version `1.1.0`, create public tag/release `1.1.0` (matching the existing tag convention), and upload `bilibili-stream-for-obs-1.1.0-windows-x64.zip` plus `CHECKSUMS.txt`.

## Publication Evidence

- Private product commit: `d9467f59d0f7cbdace6a09e4d025f6cba3ccae02` (`Harden plugin security and stream control`).
- Private metadata commit: `323629daaa0ccf651fcdc9718d8af69d9d1b0f5a` (`chore: archive plugin hardening tasks`).
- Private `main` was pushed normally from `a28f58a` to `323629d`; private tag `1.1.0` points to product commit `d9467f5`.
- Public product commit: `f9e8657e1a8058f850ecd6d90f6d8ac1900d1102`, applied directly on public `1.0.0` base `b35ddfe` with no private task/agent metadata.
- Public `main` and tag `1.1.0` both point to `f9e8657`; no force push was used.
- Public Release: `https://github.com/JumpTwiceShou/bilibili-stream-for-obs/releases/tag/1.1.0`.
- Uploaded ZIP digest: `sha256:2e068c4d55f702cbc24ec5c18153be60e52b6eb1a988bf8ebd09d9e8e3741582`; `CHECKSUMS.txt` was uploaded alongside it.
- Release is published, non-draft, and non-prerelease.
- Version `1.1.0` was confirmed in generated plugin support metadata and Windows resource metadata before packaging.
- Windows build, CTest 1/1, Clang Format, locale parity, install layout, ZIP entries, checksum, and public-scope checks passed.

## Commits And Device Sync

- Private and public source repositories were updated as explicitly requested.
- A public GitHub Release and assets were published as explicitly requested.
- No HomeLab/device sync or deployment was performed.

## Final Result

- Version `1.1.0` is published to both repositories with separated public/private histories.
- The public repository contains only product source, tests, public documentation, release notes, and localization changes.
- The private repository additionally contains integration and archived task records.
- The public Windows x64 ZIP and checksum are available from the published `1.1.0` Release.
