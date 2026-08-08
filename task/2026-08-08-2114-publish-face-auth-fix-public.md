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
- [x] Preview the merge and prepare a normal dual-parent merge preserving both public and private histories.
- [ ] Review the merge result and focused publication diff.
- [ ] Push the merge commit to private `sync/main` and public `origin/main`.
- [ ] Verify public GitHub branch state and synchronize reachable managed devices.
- [ ] Record the result and archive this task.

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
- The user explicitly authorized public repository publication. Direct public `main` publication is used instead of the skill's default PR flow because the requested outcome is to reconcile and push the already-validated default branch.

## Validation

- Prior implementation validation: Windows `RelWithDebInfo` build passed; focused tests passed 1/1; formatting, locale parity, and diff checks passed.
- Publication-specific validation: merge preview completed; it identified only the expected duplicate 1.1.0 patch conflicts. Final tree/parent checks are pending.

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
