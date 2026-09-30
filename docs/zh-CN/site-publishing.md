# 文档网站发布

[English](../site-publishing.md)

仓库只在 `docs/` 下维护 Markdown 文档。`.github/workflows/docs.yml` 会在 GitHub Actions
中安装固定版本的 MkDocs 和 Material 主题，检查生成的 HTML 中是否包含 Mermaid 图，
执行严格构建，并将生成的网站发布到独立的 `gh-pages` 分支。
生成的 HTML 不需要在本地生成，也不应提交到源码分支。

## GitHub 一次性设置

1. 在仓库 **Settings → Actions → General** 中允许 workflow 写入仓库内容，或确保
   `GITHUB_TOKEN` 拥有 `contents: write` 权限。
2. 将 workflow 和 MkDocs 配置推送到 `master`（或 `main`）。首次运行会创建或更新
   `gh-pages` 分支。
3. 在 **Settings → Pages** 中选择 **Deploy from a branch**，分支选择 `gh-pages`，目录选择
   `/(root)`。
4. 后续对 `docs/**` 或 `mkdocs.yml` 的提交会自动发布。也可以在 **Actions → Publish
   documentation → Run workflow** 中手动运行，并选择 `master` 或 `main` 作为源分支。

网站仅在 GitHub Actions 中构建。不要把生成的 `site/` 目录提交到源码分支。发布流程会
强制更新 `gh-pages` 的生成内容，因此不要在该分支手动维护文件。
