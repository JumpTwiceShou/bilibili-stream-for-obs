# Fix Menu Enable State After Login

## Scope

- Fix the account-refresh completion order that leaves authenticated menu actions disabled.
- Add focused regression coverage for the menu enable-state policy.
- Rebuild, test, and refresh the local install-smoke package.

## Exclusions

- No live Bilibili API mutation, commit, push, deployment, or public release.
- Preserve all existing comprehensive-hardening changes in the worktree.

## Ordered Work

- [x] Reproduce and identify the state-ordering defect from the screenshot and source.
- [x] Apply the state-ordering fix and regression test.
- [x] Run formatting, Windows build, CTest, and install-smoke validation.
- [x] Review the scoped diff, record the result, and archive this task.

## Evidence

- Branch/HEAD: `main` at `a28f58a9716ccbf3c4c00bf677c9f7a02b409600`.
- Existing worktree changes are the prior comprehensive-hardening implementation and are preserved.
- Root cause: `finishAccountRefresh()` enables controls before assigning the refreshed authenticated config, so the action policy reads stale `login_status` and `room_id` values.
- Successful refresh now assigns the new config before recomputing action states; request-failure handling still releases the busy state before returning.
- Added pure regression coverage for authenticated/idle and busy menu-control policies.
- Clang Format 19.1.5 `--dry-run --Werror` passed for the scoped sources.
- `cmake --build --preset windows-ci-x64 --parallel` passed and rebuilt the plugin plus core test executable.
- `ctest --preset windows-ci-x64 --output-on-failure` passed 1/1 tests.
- Install-smoke refresh passed and a verified ZIP was created at `build_x64/bilibili-stream-for-obs-fixed-20260713-1913.zip`.
- `git diff --check` passed and no files are staged.

## Commits And Device Sync

- No commit, push, release, or device sync requested.

## Final Result

- Fixed the stale-menu-state regression shown by the user: after a successful account refresh, start/stop, room update, and open-room actions are computed from the current authenticated configuration.
- The rebuilt DLL, install-smoke directory, and ZIP contain the fix.
- No live Bilibili API operation, commit, push, deployment, or public release was performed.
