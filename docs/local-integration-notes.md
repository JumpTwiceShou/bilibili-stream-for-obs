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
