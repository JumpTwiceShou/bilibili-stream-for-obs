# AGENTS.md - bilibilistream plugin1

## Scope

This repository is the C++/Qt native OBS plugin for Bilibili stream control.

The outer workspace `D:\dev\repos\bilibilistream` is a compatibility wrapper only. The actual code repository is this nested path:

```text
D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master
```

## Canonical Paths

- Current Windows repo: `D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master`
- Current Windows worktrees: `D:\dev\worktrees\bilibilistream\<branch-name>`
- Windows VM102 repo: `C:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master`
- Windows VM102 worktrees: `C:\dev\worktrees\bilibilistream\<branch-name>`
- Ubuntu VM101 repo: `~/src/repos/bilibilistream/plugin1/obs-bilibili-stream-master`
- Ubuntu VM101 worktrees: `~/src/worktrees/bilibilistream/<branch-name>`
- Old path `E:\bilibilistream` is a compatibility junction only.

## Development Rules

- Do not develop directly on `main`.
- Use one branch per task and one worktree per Codex session.
- Current Windows PC is migration control/fallback only; primary development moves to Ubuntu VM101 and Windows VM102.
- Code sync uses Git. Real env values, if added later, must use Infisical.
- Do not commit `.env`, `.env.local`, tokens, private keys, certificates, generated secret caches, local bootstrap files, OBS packaged output, `.deps`, or build output unless explicitly classified.
- `plugin2` is not part of this repo migration and must not be folded into this repo.

## Remote Policy

- Public release remote: `origin https://github.com/JumpTwiceShou/bilibili-stream-for-obs.git`.
- Planned private development remote: `sync git@github.com:JumpTwiceShou/bilibili-stream-for-obs-dev.git` after explicit Phase 6 approval.
- Default development push target after Phase 6: private `sync`.
- Release/publish target: public `origin`, only after explicit user approval.
- Do not push to public `origin` for ordinary private development.

## Env Policy

- Infisical project: `dev-secrets` (`cc4ee95f-8c4f-406e-832e-f65cdeb73739`).
- Project path: `/projects/bilibilistream`.
- Shared path: `/shared/common`.
- Local env output file: `.env.local`, gitignored.
- `sync-all-projects` is remote-to-local only.
- New env keys must be documented in `.env.example` and `docs/env.md`, then written with `push-project-env` dry-run and explicit apply.

## Local Integration Notes

Read `docs/local-integration-notes.md` before changing OBS stop-stream integration logic.

## Commands

- Setup/test/build: not confirmed at Phase 3.
- Inspect existing OBS/CMake/build instructions before building.
- Keep packaged plugin output out of Git unless explicitly requested for release.

## Completion Checks

Before finishing a task, show the diff, confirm no env values are staged, and state whether work is private-only or intended for public release.

## Worktree Details
See `docs/worktree.md` for this project's exact worktree commands. Do not create a worktree or branch until the user confirms the task scope.
