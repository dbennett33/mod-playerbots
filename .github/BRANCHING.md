# Branching and CI

## Branches

| Branch | Paired AzerothCore branch | Role |
|--------|---------------------------|------|
| `master` | `Playerbot` | Live module line |
| `dev` | `dev` | Test module line |

Work flows **dev → PR → master**. Do not push directly to `master`.

Create `dev` once (from current `master`):

```bash
git checkout master
git pull
git checkout -b dev
git push -u origin dev
```

## CI

Module pushes do **not** rebuild AzerothCore on GitHub-hosted Ubuntu/Windows/macOS.
They only trigger your fork’s **`vps-build`** (Debian 12 VM / VPS).

| Event | Branch | Action |
|-------|--------|--------|
| push | `master` | `trigger-vps-build` → AC `Playerbot` `vps-build` (stage live, no auto-deploy) |
| push | `dev` | `trigger-vps-build` → AC `dev` `vps-build` + auto test deploy |
| manual | any | `core_build` / `windows_build` / `macos_build` via **Actions → Run workflow** if you want a smoke compile |

`vps-build` always checks out this module’s matching branch (`master` ↔ `Playerbot`, `dev` ↔ `dev`).

## Secret (required)

**Settings → Secrets → Actions** on `mod-playerbots`:

| Name | Value |
|------|--------|
| `ACORE_WORKFLOW_PAT` | Fine-grained or classic PAT that can run workflows on your AzerothCore fork (`workflow` scope / Actions write) |

Optional repo **variable** `ACORE_REPO` if AzerothCore is not `{owner}/azerothcore-wotlk`.

Without `ACORE_WORKFLOW_PAT`, module pushes fail at the trigger step (by design).

## GitHub branch protection

**`master`**

- Require pull request before merging (from `dev`)
- Restrict direct pushes
- **Require status check:** `Validate PR source` (from `branch-protection.yml`)

**`dev`**

- Integration branch; direct push OK for daily work
