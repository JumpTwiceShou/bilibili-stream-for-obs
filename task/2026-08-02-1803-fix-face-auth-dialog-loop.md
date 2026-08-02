# Fix Face-Auth Dialog Loop

## Scope

- Reconcile the local Git checkout with the newer private `sync/main` baseline.
- Fix the repeated close/reopen loop of the face-verification QR dialog, including `60043` handling.
- Add focused regression coverage where practical.
- Build, test, commit to `main`, and push to private `sync/main`.

## Exclusions

- No push to public `origin` and no public release.
- No OBS plugin installation or process restart unless separately authorized.
- `plugin2` is out of scope.

## Ordered Work

- [x] Fetch and assess the six remote commits that moved `sync/main`.
- [x] Fast-forward local `main` after confirming the remote changes are compatible.
- [x] Trace the exact 1.1.0 asynchronous face-auth state machine.
- [x] Implement a single-dialog, no-reentry retry flow with correct auth-code propagation.
- [x] Add and run focused regression validation.
- [ ] Review the scoped diff, commit, and push to `sync/main`.

## Acceptance Criteria

- A pending or repeatedly returned face-auth requirement cannot close/reopen the dialog indefinitely.
- `60024` and `60043` use their actual authentication code in status checks.
- Polling does not overlap and cancellation remains safe.
- Focused build/tests pass.
- `main` is pushed to `sync/main`; public `origin` remains unchanged.

## Evidence And Decisions

- Local `main` began at `a28f58a`; fetched `sync/main` is `878f8ec`, six commits ahead.
- The fetched commits contain the 1.1.0 asynchronous hardening implementation used by the installed plugin lineage.
- The installed DLL reports 1.0.0 but is a 2026-07-13 asynchronous build; semantic version metadata was not updated in that binary.
- Existing runtime diagnosis is archived in `task/archive/2026/2026-08-02-1748-face-qr-dialog-flicker-diagnosis.md`.
- Local `main` was fast-forwarded to `878f8ec` after reviewing the six fetched commits and relevant source diff.
- The 1.1.0 implementation already propagates `60024`/`60043` and blocks overlapping polls, but it closed the dialog on the status endpoint alone and then routed another face-auth response through `handleStartAttempt`, creating a new modal dialog.
- The fix keeps the dialog open while start-live is retried, updates the existing QR/auth-code session when Bilibili still requires verification, and completes the modal flow only after start-live succeeds or returns a terminal failure.
- VM102 Windows `RelWithDebInfo` build completed successfully and produced the plugin DLL plus `plugin-core-tests.exe`.
- `ctest --preset windows-ci-x64 --output-on-failure` passed 1/1 focused offline tests.
- Clang Format 19.1.5 `--dry-run --Werror` passed for all changed C++ sources and headers.
- `git diff --check` passed, and locale parity passed with 58 matching keys in `en-US` and `zh-CN`.
- A local fresh configure attempt was abandoned without deleting existing output because the shared OBS dependency cache was generated with Windows SDK 10.0.22621 while the current host selected 10.0.26100; the already configured VM102 build provided the required clean compiler/test evidence.

## Blockers

- None.

## Commits

- Pending.

## Device Sync

- Current Windows: pending.
- Windows VM102: pending.
- Ubuntu VM101: pending.

## Final Result

- Pending.
