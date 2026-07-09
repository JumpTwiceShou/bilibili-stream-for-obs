# Worktree Workflow - bilibilistream plugin1

Status: documented. This file does not create a worktree by itself.

## Repository Roots

Current Windows PC:

```text
repo: D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master
worktrees: D:\dev\worktrees\bilibilistream\<branch-path>
```

Windows VM102:

```text
repo: C:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master
worktrees: C:\dev\worktrees\bilibilistream\<branch-path>
```

Ubuntu VM101:

```text
repo: ~/src/repos/bilibilistream/plugin1/obs-bilibili-stream-master
worktrees: ~/src/worktrees/bilibilistream/<branch-path>
```

Old compatibility paths may exist as Windows junctions. Do not start new work from those old paths.

## Rules

- Do not develop directly on protected branches: main.
- Use one branch per task.
- Use one worktree per branch.
- Use one Codex session per worktree.
- Do not share the same branch or worktree between Codex sessions.
- Create or remove worktrees only after the user confirms the task branch.
- Keep dependency folders local to each machine and project; do not copy `node_modules`, `.venv`, build outputs, or generated secret caches between machines.
- The outer bilibilistream folder is a workspace container only; plugin2 is excluded from migration.

## Branch Names

Use these branch families unless the user gives a specific branch name:

```text
feat/<name>
fix/windows-<name>
chore/investigate-<name>
```

Use a filesystem-safe worktree folder name by replacing `/` with `-`:

```text
branch: feat/migration-cleanup
path:   feat-migration-cleanup
```

## Current Windows Commands

Create a new branch and worktree from the planned base ref:

```powershell
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree list
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" branch --list feat/migration-cleanup
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree add "D:\dev\worktrees\bilibilistream\feat-migration-cleanup" -b feat/migration-cleanup "main"
Set-Location "D:\dev\worktrees\bilibilistream\feat-migration-cleanup"
```

If the branch already exists and is not checked out anywhere else:

```powershell
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree add "D:\dev\worktrees\bilibilistream\feat-migration-cleanup" feat/migration-cleanup
```

## Windows VM102 Commands

Run after the repository has been cloned or synced on Windows VM102:

```powershell
git -C "C:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree list
git -C "C:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree add "C:\dev\worktrees\bilibilistream\fix-windows-validation" -b fix/windows-validation "main"
Set-Location "C:\dev\worktrees\bilibilistream\fix-windows-validation"
```

## Ubuntu VM101 Commands

Run in a login shell, or initialize PATH/fnm first if automation uses non-login SSH:

```bash
git -C "~/src/repos/bilibilistream/plugin1/obs-bilibili-stream-master" worktree list
git -C "~/src/repos/bilibilistream/plugin1/obs-bilibili-stream-master" worktree add "~/src/worktrees/bilibilistream/feat-migration-cleanup" -b feat/migration-cleanup "main"
cd "~/src/worktrees/bilibilistream/feat-migration-cleanup"
```

## Cleanup

Only remove a worktree after merge or explicit user confirmation:

```powershell
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree list
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree remove "D:\dev\worktrees\bilibilistream\feat-migration-cleanup"
git -C "D:\dev\repos\bilibilistream\plugin1\obs-bilibili-stream-master" worktree prune
```

## Before Finishing Work

- Run the project status and relevant checks from the worktree.
- Confirm no env files, tokens, private keys, certificates, bootstrap files, or generated secret caches are staged.
- Push only to the approved private development remote after Phase 6 approval.
- Push to public `origin` only for explicit release/publish work.
