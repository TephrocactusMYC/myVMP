# Documentation Website Publishing

[中文](zh-CN/site-publishing.md)

The repository keeps documentation source as Markdown under `docs/`. The GitHub Actions
workflow in `.github/workflows/docs.yml` installs pinned MkDocs and Material for MkDocs
versions, performs a strict build, verifies Mermaid containers with a Python
standard-library checker, and publishes the generated site to the separate `gh-pages`
branch. The check does not assume that `rg` is installed on the runner. Generated
HTML is not maintained or committed on the source branch.

## One-time GitHub setup

1. In repository **Settings → Actions → General**, allow the workflow to have read and
   write repository contents (or otherwise grant `GITHUB_TOKEN` the `contents: write`
   permission).
2. Push this workflow and the MkDocs configuration to `master` (or `main`). The first run
   creates/updates `gh-pages`.
3. In **Settings → Pages**, select **Deploy from a branch**, choose `gh-pages`, and select
   `/(root)`.
4. Later commits affecting `docs/**` or `mkdocs.yml` publish automatically. A manual run is
   also available from **Actions → Publish documentation → Run workflow**; select `master`
   or `main` as the source branch for that run.

The site is built in GitHub Actions only. Do not add the generated `site/` directory to the
source branch; it is a transient build product. The deployment workflow force-updates the
generated contents of `gh-pages`, so hand-edits on that branch will be replaced.

If Actions reports exit code 127, expand the failing step: it means a shell command was
not found. The former Mermaid check called `rg` without installing it; the current check
uses `python scripts/check_mermaid_site.py site`, and both MkDocs invocations use
`python -m mkdocs`. A local, unpushed workflow edit cannot affect a GitHub run: push the
source branch, confirm the new commit SHA in the run, then inspect the generated Pages site.
