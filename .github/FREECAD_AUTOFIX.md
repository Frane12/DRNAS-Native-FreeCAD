# DRNAS FreeCAD — event-driven AI build repair

## Status and activation

The on-failure workflow is installed on the default branch (`main`):

- `.github/workflows/freecad-instant-autofix.yml` watches for a **completed failure** of `DRNAS REAL upstream FreeCADGui Full ARM64 Build` from `feature/full-freecad-android`.
- `scripts/freecad-ai-autofix.py` reads the failed GitHub Actions log and attempts **one** bounded code patch.
- `.github/workflows/upstream-freecadgui-full-android.yml` is present on `main` as required for a `workflow_dispatch` of the actual build on the feature branch.

**One-time setup required:** In this GitHub repository, visit **Settings → Secrets and variables → Actions → New repository secret** and add `OPENAI_API_KEY`. This requires OpenAI API credentials and API billing; a ChatGPT subscription or linked ChatGPT Gmail/GitHub account does not supply this secret. Never commit the key into any file.

The system reacts to the **GitHub build-failed event**, independently of email notifications and whether the owner's phone is online. GitHub runners can queue; timing is near-event, not guaranteed instantaneous.

## Safety guarantees

- Only owner repository `Frane12/DRNAS-Native-FreeCAD` and branch `feature/full-freecad-android`.
- Skip stale failed runs, skip if another commit or newer build exists.
- Trusted repair script runs from `main`; the failing branch is never executed with the API key in the environment.
- Limited to existing Android FreeCAD build helper scripts. Cannot alter workflows, secrets, or unrelated repositories.
- Reject invalid diffs, unsupported paths, large patches, shell syntax errors and competing branch updates.
- At most **2 consecutive automatic repair commits**, then manual review is required.
- A repair commit uses the GitHub-provided token and explicitly dispatches the build again (a `GITHUB_TOKEN` push alone does **not** trigger push workflows).
- Original build workflow continues to preserve its compiler cache and artifacts, including on failed builds.
- No automatic release publishing.

If `OPENAI_API_KEY` is missing or a safe patch cannot be found, the auto-fixer fails visibly in the Actions page instead of claiming success or infinitely rerunning builds.

## Useful links

- [FreeCAD build runs](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/upstream-freecadgui-full-android.yml)
- [Automatic fixer runs](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecad-instant-autofix.yml)
