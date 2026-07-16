# AGENTS.md - bilibilistream plugin1

<!-- homelab-project-initialization: complete -->

## Startup

Read `~/.codex/SHARED_AGENT_RULES.md`, `docs/local-integration-notes.md` before stream-control integration changes, and the active file under `task/` when the work requires one.

## Scope

This nested repository is the C++/Qt OBS plugin for Bilibili stream control. The outer workspace is compatibility context only; `plugin2` is excluded.

## Canonical Paths

- Current Windows: `D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master`.
- Windows VM102: `C:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master`.
- Ubuntu VM101: `~/src/repos/bilibilistream/plugin1/obs-bilibili-stream-master`.

Direct work on `main` is allowed. Inspect existing OBS/CMake instructions and run only focused build or integration checks required by the change.

## Remote And Env

- Private development remote: `sync` → `JumpTwiceShou/bilibili-stream-for-obs-dev`.
- Public release remote: `origin` → `JumpTwiceShou/bilibili-stream-for-obs`.
- Ordinary development pushes to `sync`; public release requires explicit approval.
- Env paths: `/shared/common` and `/projects/bilibilistream`; local output is ignored `.env.local`.
- Do not commit env values, credentials, OBS packaged output, `.deps`, or build output unless explicitly classified for release.

## Completion

Check the scoped diff and staged files, run only necessary validation, state private versus public-release scope, update the task, and archive it when complete.
