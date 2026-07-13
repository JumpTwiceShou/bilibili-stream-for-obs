# Add Local Logout Action

## Scope

- Add a localized logout action to the login submenu.
- Confirm before clearing locally stored Bilibili credentials and account/room state.
- Keep logout available when saved credentials exist even if the status check cannot complete.
- Prevent logout while a plugin-managed live session remains active.
- Add regression coverage, rebuild, test, and refresh the local ZIP package.

## Exclusions

- Do not call a Bilibili server-side logout endpoint or affect other devices.
- No live streaming mutation, commit, push, deployment, or public release.

## Ordered Work

- [x] Inspect current menu, configuration, action-state, and dialog flows.
- [x] Implement logout UI, persistence clearing, safety gate, and localization.
- [x] Add regression coverage and documentation.
- [x] Run formatting, Windows build, CTest, install, and ZIP validation.
- [x] Review the scoped diff, record the result, and archive this task.

## Evidence

- Branch/HEAD: `main` at `a28f58a9716ccbf3c4c00bf677c9f7a02b409600`.
- Existing worktree changes belong to the prior hardening and menu-state fixes and must be preserved.
- Added `登录 -> 退出登录`, a confirmation dialog, success/failure feedback, and matching English/Chinese locale keys.
- Logout resets the in-memory account/room configuration and atomically saves the empty credential state; save failure restores the previous state instead of reporting a false success.
- Logout is enabled whenever saved credentials exist, including after a transient login-status request failure, and disabled while another operation is in flight.
- A plugin-managed session blocks logout until it is stopped so the plugin does not discard credentials required for synchronized stop.
- Core control-state regression coverage includes authenticated, offline-with-credentials, and busy cases.
- Locale parity passed with 56 matching keys per language.
- Clang Format 19.1.5 `--dry-run --Werror`, Windows CI build, and `ctest --preset windows-ci-x64` passed; CTest reported 1/1 tests passed.
- Install-smoke refresh and ZIP entry validation passed. Output: `build_x64/bilibili-stream-for-obs-logout-20260713-1917.zip`.
- `git diff --check` passed and no files are staged.

## Commits And Device Sync

- No commit, push, release, or device sync requested.

## Final Result

- Added a safe, localized local-logout workflow that clears this plugin's saved credentials without affecting other devices.
- Rebuilt and packaged the plugin successfully; no live Bilibili mutation was performed.
- No commit, push, deployment, device sync, or public release was performed.
