# Local Integration Notes

This note summarizes the useful outer workspace `PROJECT_NOTES.md` content that was approved for migration into the plugin1 repository.

## Repository Boundary

- This repo is plugin1: the C++/Qt native OBS plugin.
- The outer workspace `D:\dev\repos\bilibilistream` exists for old path compatibility only.
- plugin2 is a separate Python OBS script reference and is not part of this repository migration.
- Do not copy plugin2, packaged output, `dist`, or release artifacts into this repo unless explicitly requested.

## Relevant plugin1 files

- `src/plugin-main.cpp`: OBS plugin entry points and frontend event integration.
- `src/bilibili_api.cpp` / `src/bilibili_api.hpp`: login, room state, start-live, stop-live, and Bilibili API wrappers.
- `src/core/config_manager.cpp`: local plugin configuration.

## Behavior To Preserve

The integration goal is to make plugin1 handle OBS native stop-stream behavior safely:

1. Listen for OBS frontend streaming stop events.
2. Track whether the current Bilibili session was successfully started and is still managed by this plugin.
3. Ignore OBS stop events when no plugin-managed Bilibili session exists.
4. For a managed session, query the current Bilibili live status off the UI thread.
5. If Bilibili is still live, call the stop-live API. If the status query fails, fail safe by attempting stop only for that managed session.
6. Refresh local plugin menu/status state so the UI returns to a start-live state.

## plugin2 Reference

plugin2 demonstrated the behavior through a Python OBS script pattern:

- register an OBS frontend event callback during script load;
- listen for `OBS_FRONTEND_EVENT_STREAMING_STOPPED`;
- call a live-status check before sending stop-live;
- use the stop-live button path only when the room is still live.

plugin1 adds a managed-session ownership gate to this reference behavior so an unrelated OBS stop event cannot close a Bilibili session started elsewhere.

This repo should use plugin2 only as behavioral reference, not as copied source.

## Native QR verification (1.3.3)

Face verification uses the native QR dialog. Codes 60024/60028 render the server-returned QR; 60043 restores the earlier face-middle entry with the encoded account ID. The legacy status request preserves the original code and polls every 1.5 seconds. The main label stays stable while waiting. One explicit success response permits an automatic start attempt. If start-live still requires verification, the same positive status cannot repeatedly trigger start requests; a new pending-to-success transition rearms automatic start. **Verification completed — retry start** provides a fallback for stale/inconsistent status. Status failures continue polling without guessing success. Waiting expires after ten minutes. Cancelled dialogs ignore late responses, and an in-flight start request must finish before closing. The embedded SDK/WebView2 flow was removed in 1.3.3 after a blank-window regression. QR display does not establish successful verification; actual start success and valid streaming credentials are still required. See docs/face-auth-contract.md for compatibility and validation limits.

## Plugin start and route dialog

The route picker explicitly confirms a managed Bilibili start and distinguishes OBS output state. The manual 一键推流 button uses the selected route and already acquired credentials without another Bilibili start request; invalid/stopped sessions and active OBS output are rejected. Close dismisses only the window. Opening the route window does not automatically start OBS output; the user explicitly clicks the button. Native OBS start/stop synchronization settings remain independent.
