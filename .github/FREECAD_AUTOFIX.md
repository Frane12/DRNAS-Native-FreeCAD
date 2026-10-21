# DRNAS FreeCAD — event-driven AI build repair

## Status and activation

The on-failure workflow is installed on the default branch (`main`):

- `.github/workflows/freecad-instant-autofix.yml` watches for a **completed failure** of `DRNAS REAL upstream FreeCADGui Full ARM64 Build` from `feature/full-freecad-android`.
- `scripts/freecad-ai-autofix.py` reads the failed GitHub Actions log and attempts **one** bounded code patch.
- `.github/workflows/upstream-freecadgui-full-android.yml` is present on `main` as required for a `workflow_dispatch` of the actual build on the feature branch.

**Authentication verified on 21 October 2026:** GitHub Actions run `39614770228` confirmed that the `OPENAI_API_KEY` repository secret exists and OpenAI accepted it for `GET /v1/models` (HTTP 200). This does **not** yet prove Responses model access or available API billing. Keep the secret in **Settings → Secrets and variables → Actions**; never commit the key into a file.

The system reacts to the **GitHub build-failed event**, independently of email notifications and whether the owner's phone is online. GitHub runners can queue; timing is near-event, not guaranteed instantaneous.

## GUI APK integration agent

After a successful FreeCADGui ARM64 source build, `.github/workflows/freecadgui-apk-on-upstream-success.yml` dispatches the feature-branch `DRNAS FreeCADGui QtActivity APK Integration` workflow, where the verified dependency caches are accessible. It packages real FreeCADGui/Base/App, Coin3D, GL4ES and all verified Android dependencies.

On a failed APK packaging run, `.github/workflows/freecad-apk-instant-autofix.yml` invokes `scripts/freecad-apk-ai-autofix.py` (trusted from main) with the same secret. It first verifies the failed run's exact integration commit and allows a **maximum of two** consecutively generated changes to a small approved list of existing APK-related files. Missing or stale native build artifacts are **not** treated as fixable APK code bugs.

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
- [Automatic source-build fixer runs](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecad-instant-autofix.yml)
- [Automatic APK fixer runs](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecad-apk-instant-autofix.yml)
- [Native FreeCADGui ARM64 APK workflow](https://github.com/Frane12/DRNAS-Native-FreeCAD/actions/workflows/freecadgui-qtactivity-native-apk.yml)
