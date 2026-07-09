# Environment - bilibilistream plugin1

## Current State

No required project-specific env keys were confirmed at Phase 3.

## Infisical

- Project: `dev-secrets`
- Project ID: `cc4ee95f-8c4f-406e-832e-f65cdeb73739`
- Shared path: `/shared/common`
- Project path: `/projects/bilibilistream`
- Environment: `dev`
- Local output file: `.env.local`

## Rules

- `.env.local` is local-only and must stay gitignored.
- `.env.example` stores variable names only.
- `sync-all-projects` is remote-to-local only.
- Adding env keys requires updating `.env.example` and this file, then `push-project-env` dry-run before explicit apply.
