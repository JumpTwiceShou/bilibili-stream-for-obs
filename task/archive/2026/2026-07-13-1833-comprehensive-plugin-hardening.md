# Comprehensive OBS Plugin Hardening

## Scope

- Implement every actionable finding from the 2026-07-13 plugin audit.
- Redact authentication and streaming secrets from logs and user-facing errors.
- Move mutable state to the OBS module configuration path, protect persisted cookies on Windows, migrate legacy state, and stop persisting CSRF/RTMP secrets.
- Move Bilibili API work off the OBS/Qt UI thread with cancellation-safe result delivery.
- Gate automatic native-stop synchronization to plugin-managed Bilibili sessions.
- Centralize form encoding, validate remote JSON without throwing, and improve HTTP cancellation/response handling.
- Fix plugin/menu/worker lifecycle and CMake/template/install hygiene.
- Add focused offline regression coverage for pure parsing, redaction, encoding, and state decisions where practical.

## Exclusions

- No live Bilibili account mutation, real stream start/stop, deployment, push, or public release.
- `plugin2` and the outer compatibility workspace remain out of scope.
- Do not install missing system toolchains without explicit authorization.

## Ordered Work

- [x] Read applicable shared/project instructions and the archived audit.
- [x] Confirm branch, HEAD, upstream, live remote HEAD, and existing worktree state.
- [x] Implement configuration security and migration.
- [x] Implement log redaction, form encoding, and defensive response parsing.
- [x] Refactor API calls and polling off the UI thread with safe cancellation.
- [x] Add managed-session gating and lifecycle cleanup.
- [x] Clean CMake/template/install rules and add focused tests.
- [x] Run available validation and document toolchain limitations.
- [x] Review scoped and staged diffs, record the result, and archive this task.

## Acceptance Criteria

- No authentication token, CSRF value, QR credential URL, or RTMP stream key is logged or persisted in ordinary JSON configuration.
- Windows cookie persistence uses user-bound OS protection; other platforms retain a clearly isolated compatibility fallback until a native secret-store dependency is adopted.
- Plugin loading, menu actions, QR/face polling, room updates, and native-stop synchronization do not perform network I/O on the UI thread.
- Native OBS stop only closes a session marked as managed by this plugin.
- Form bodies encode every value and malformed partition data cannot throw through Qt.
- Plugin load failure and unload cancel work safely and remove owned UI.
- CMake install rules remain relocatable and generated plugin metadata is actually used.
- Focused offline tests cover the new pure logic, or any unavailable validation is recorded explicitly.

## Evidence

- Start branch: `main`.
- Start/local/upstream/live remote HEAD: `a28f58a9716ccbf3c4c00bf677c9f7a02b409600`.
- Existing untracked file belongs to the completed prior audit: `task/archive/2026/2026-07-13-1804-plugin-code-audit.md`.
- Starting source worktree is clean.
- Windows CI configure completed with bundled CMake, MSVC 19.44, Windows SDK 10.0.26100, OBS 31.1.1, Qt 6, and CURL 8.12.1 dependencies.
- `cmake --build --preset windows-ci-x64 --parallel` completed with compiler warnings treated as errors and produced the plugin DLL plus `plugin-core-tests.exe`.
- `ctest --preset windows-ci-x64 --output-on-failure` passed 1/1 focused offline tests.
- Clang Format 19.1.5 `--dry-run --Werror` passed for every changed/new C++ source and header.
- `cmake --install build_x64 --config RelWithDebInfo --prefix build_x64/install-smoke` installed only below the requested prefix with the DLL, PDB, sanitized data config, and both locale files.
- Locale parity validation passed with 48 matching keys in `en-US` and `zh-CN`.
- `git diff --check`, JSON parsing, sensitive-field scans, unexpanded-template scan of the compiled DLL, and the final unstaged/staged review passed; no files are staged.

## Decisions

- Preserve QR-login persistence on Windows using DPAPI rather than forcing login on every OBS restart.
- Keep a per-user configuration fallback on non-Windows platforms so existing macOS/Linux behavior does not regress; mark this residual security limitation in code/docs.
- Reuse one plugin-owned background task queue and marshal all completion handling back to the Qt thread.
- Treat the persisted managed-session marker as ownership state, separate from the latest observed Bilibili live state.
- Route menus and static dialogs through OBS locale resources and retain Bilibili/API dynamic messages as returned by the API layer.

## Blockers

- None. The VS Build Tools installation includes a working bundled CMake/MSVC/Clang Format toolchain; zsh/gersemi are still not installed locally, but the repaired wrapper/action path and CMake generation were inspected, and the Windows build does not require those tools.

## Commits And Device Sync

- No commit, push, release, or device sync requested.

## Final Result

- All eight audit finding groups were addressed within plugin1: secret-safe configuration/logging, background request execution, managed-session stop ownership, encoded forms, defensive JSON conversion, lifecycle/build cleanup, offline tests, and active localization resources.
- Windows compilation, focused tests, formatting, install relocation, locale parity, metadata, JSON, and diff checks pass.
- Live Bilibili login/start/stop/update calls were intentionally not exercised; API compatibility remains dependent on Bilibili's private interfaces.
- Changes remain local and unstaged on `main`. No commit, push, public release, deployment, or device sync was performed.
- The configured `origin` is the private development repository (`JumpTwiceShou/bilibili-stream-for-obs-dev`); public release scope was not entered.
