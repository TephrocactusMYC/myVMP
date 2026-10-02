# 文档网站发布

[English](../site-publishing.md)

仓库只在 `docs/` 下维护 Markdown 文档。`.github/workflows/docs.yml` 会在 GitHub Actions
中安装固定版本的 MkDocs 和 Material 主题，严格构建后用 Python 标准库检查生成的 HTML
是否包含 Mermaid 图容器（不依赖 runner 是否安装 `rg`），并将生成的网站发布到独立的
`gh-pages` 分支。
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

如果 Actions 报退出码 127，请展开失败 step：这表示找不到某个 shell 命令。旧版 Mermaid
检查直接调用 `rg`，但未保证 runner 安装了它；新版改为
`python scripts/check_mermaid_site.py site`，MkDocs 也改为 `python -m mkdocs`。本地尚未推送的
workflow 变更不会影响线上运行：推送源码分支后，先确认运行所对应的 commit SHA，再查看
生成的 Pages 网站。
