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

| Event | Branch | Local CI (`core_build`) | VPS (via AzerothCore) |
|-------|--------|-------------------------|------------------------|
| push / PR | `master` | ubuntu matrix build | `trigger-vps-build` → AC `Playerbot` build, no deploy |
| push | `dev` | ubuntu matrix build | `trigger-vps-build` → AC `dev` build + test deploy |

## Secret (repository)

Add **Settings → Secrets → Actions**:

| Name | Value |
|------|--------|
| `ACORE_WORKFLOW_PAT` | PAT with `workflow` scope on your AzerothCore repo |

Optional repo **variable** `ACORE_REPO` if AzerothCore is not `{owner}/azerothcore-wotlk`.

Without this secret, module pushes still run `core_build` but do not trigger the VPS.

## GitHub branch protection

**`master`**

- Require pull request before merging (from `dev`)
- Restrict direct pushes
- **Require status check:** `Validate PR source` (from `branch-protection.yml`)

**`dev`**

- Integration branch; direct push OK for daily work
