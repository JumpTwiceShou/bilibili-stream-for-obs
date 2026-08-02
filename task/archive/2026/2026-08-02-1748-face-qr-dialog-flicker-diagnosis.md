# Face QR Dialog Flicker Diagnosis

## Scope

- Diagnose why the face-verification QR dialog repeatedly disappears and reappears after starting a Bilibili stream while already logged in.
- Ground the diagnosis in the current plugin source and, if locally available, relevant logs.

## Exclusions

- No source-code behavior changes.
- No build, deployment, release, or external service mutation.
- `plugin2` is out of scope.

## Ordered Work

- [x] Trace the start-live and face-verification API flow.
- [x] Trace QR dialog ownership, close, refresh, and polling behavior.
- [x] Identify the concrete trigger for repeated hide/show behavior.
- [x] Record evidence, likely fix boundary, and residual uncertainty.

## Acceptance Criteria

- The responsible code path and failure mechanism are identified with file/line evidence.
- Any conclusion that depends on missing runtime logs is labeled accordingly.

## Evidence And Decisions

- Repository started clean on `main` at `a28f58a`, matching `sync/main`.
- This task is diagnosis-only; no implementation is authorized in this pass.
- `src/bilibili_api.cpp:498-503` handles start-live result `60043`, but `src/bilibili_api.cpp:526-530` hard-codes `face_auth_code=60024` for the verification-status request.
- `src/ui/dialog_factory.cpp:160-166` closes the active QR dialog as soon as polling reports success and calls the verified callback.
- `src/plugin-main.cpp:273-280` immediately retries start-live; if that request again returns a face-auth QR, it creates another face-auth dialog.
- The active OBS log recorded 29 `60043` start-live responses from 15:53:01 through 15:53:57 and another 15 from 16:40:27 through 16:40:59. This confirms repeated start-live/reopen cycles during the observed flicker.
- The installed `1.0.0` DLL was built on 2026-07-13 and does not match the checked-in 2026-06-06 build artifact. Its PDB references a later, unavailable VM102 source snapshot, so exact installed-source line mapping is unavailable. Binary symbols show an asynchronous face-auth implementation, but the runtime log confirms the same close/retry/reopen state-machine loop.

## Blockers

- No blocker to the root-cause diagnosis. Exact implementation details of the installed asynchronous variant cannot be recovered from the current Git history.

## Final Result

- Confirmed a plugin state-machine defect. A `60043` challenge is being treated as verified before start-live accepts it; the dialog is closed, start-live is retried, and another dialog is created when `60043` is returned again. The checked-in implementation additionally queries the wrong fixed auth code (`60024`) for a `60043` challenge.
- A fix should keep one dialog alive, propagate the actual auth code, prevent concurrent/in-flight polls, and only close the dialog after start-live succeeds (or after a terminal error/cancel), not merely after the status endpoint reports identified.
