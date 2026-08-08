# Publish Face-Auth Fix To Public Repository

## Scope

- Reconcile the private development `main` history with the public repository's existing `main` without rewriting history.
- Publish the verified face-auth single-dialog fix and associated project/task metadata to public `origin/main`.
- Keep the private development remote and reachable managed devices synchronized with the resulting merge commit.

## Exclusions

- No force-push, rebase, tag movement, GitHub Release creation, or release-asset upload.
- No new plugin build or installation; the implementation was already validated before private publication.
- `plugin2` remains out of scope.

## Ordered Work

- [x] Verify project initialization marker, GitHub authentication, remotes, branch, and clean worktree.
- [x] Fetch and assess private/public default branches and tag state.
- [x] Preview and create a normal dual-parent merge preserving both public and private histories.
- [x] Review the merge result and focused publication diff.
- [x] Push the merge commit to private `sync/main` and public `origin/main`.
- [x] Verify public GitHub branch state and synchronize reachable managed devices.
- [x] Record the result and archive this task.

## Acceptance Criteria

- Public `origin/main` contains the face-auth fix without force-push or history loss.
- Existing public tag `1.1.0` remains unchanged at `f9e8657`.
- Private `sync/main`, current Windows, VM102, and VM101 converge on the publication merge commit with clean worktrees.
- No GitHub Release or binary asset is created or modified.

## Evidence And Decisions

- Start/private HEAD: `05873a8`; public `origin/main`: `f9e8657`; merge base: `b35ddfe`.
- Public `f9e8657` and private `d9467f5` are patch-equivalent 1.1.0 hardening commits, but the histories diverged because private project metadata preceded its equivalent commit.
- The private branch has 12 unique commits relative to public history, including implementation commit `1dea6c4` for the face-auth fix.
- A normal merge is required for a non-destructive public push. Force-push and tag changes are explicitly excluded.
- `git merge-tree` preview confirmed duplicate-patch conflicts in the 1.1.0 files. Because public `f9e8657` is already patch-equivalent to private `d9467f5` and the current private tree contains later fixes, the publication merge will use Git's `ours` merge strategy to retain the fully validated current tree while recording both histories as parents.
- `git merge --no-commit --no-ff -s ours origin/main` prepared the merge with `MERGE_HEAD=f9e8657` and no worktree conflict or source-tree change.
- Merge commit `8b16782` has first parent `05873a8` and second parent `f9e8657`; its first-parent file delta contains only this publication task file, confirming no validated source was altered by reconciliation.
- Private `sync/main` fast-forwarded from `05873a8` to `8b16782`; public `origin/main` fast-forwarded from `f9e8657` to `8b16782`. No tag refspec was pushed.
- The user explicitly authorized public repository publication. Direct public `main` publication is used instead of the skill's default PR flow because the requested outcome is to reconcile and push the already-validated default branch.

## Validation

- Prior implementation validation: Windows `RelWithDebInfo` build passed; focused tests passed 1/1; formatting, locale parity, and diff checks passed.
- Publication-specific validation: merge preview identified only the expected duplicate 1.1.0 patch conflicts; dual-parent/first-parent-tree checks passed; both remote branches and the GitHub API resolved public `main` to `8b16782` after push.
- The public `1.1.0` tag remained at `f9e8657`; the existing non-draft, non-prerelease GitHub Release remained present and unchanged. No branch-push Actions workflow was triggered for `8b16782`.

## Blockers

- None.

## Commits

- `8b16782` - `Merge public 1.1.0 history before publishing fixes`

## Device Sync

- Current Windows: publication merge verified at `8b16782`; private and public remote-tracking branches matched before task archival.
- Windows VM102: clean worktree verified, then fast-forwarded private `origin/main` from `05873a8` to `8b16782`.
- Ubuntu VM101: clean worktree verified, then fast-forwarded private `origin/main` from `05873a8` to `8b16782`.
- The task-archive follow-up commit will be pushed to both remotes and fast-forwarded to both managed devices as the final publication bookkeeping step.

## Final Result

- Published the previously validated face-auth single-dialog fix to public `JumpTwiceShou/bilibili-stream-for-obs` without rewriting history.
- Reconciled the patch-equivalent but divergent private/public 1.1.0 histories with dual-parent merge commit `8b16782`; the current validated private source tree was retained exactly.
- Private and public `main` branches, plus both reachable managed devices, were verified at the publication merge. Existing public tag and Release state were not modified, and no binary assets were published.
