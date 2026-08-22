# 发布与签名指南

[English](RELEASING.md)

本策略采用常见开源流程：可复现版本 metadata、经过 review 的 release commit、签名 Git tag、
带 checksum 的 archive 和 detached signature。

维护者优先使用硬件保护的 OpenPGP key：离线保存 primary key，使用有期限的 signing
subkey，并在托管账号和 release notes 公布 fingerprint。默认分支应要求 PR review 和发布
门禁。

## 发布步骤

1. 把 `CHANGELOG.md` 的 `Unreleased` 内容移动到带日期的 `X.Y.Z`。
2. 把 `VERSION` 精确更新为 `X.Y.Z`，必要时同步兼容性文档。
3. 在标准 Ubuntu 环境运行 `make release-check`。
4. review diff，以 DCO sign-off 提交并完成项目 review。
5. 创建并验证签名 tag：

```sh
git tag -s vX.Y.Z -m 'myVMP X.Y.Z'
git tag -v vX.Y.Z
```

6. 从签名 tag 的干净 checkout 构建和签名：

```sh
make package
version="$(cat VERSION)"
git archive --format=tar.gz --prefix="myvmp-${version}/" \
  -o "build/dist/myvmp-${version}-source.tar.gz" HEAD
cd build/dist
sha256sum myvmp-X.Y.Z-linux-*-tool.tar.gz myvmp-X.Y.Z-source.tar.gz > SHA256SUMS
gpg --armor --detach-sign SHA256SUMS
gpg --verify SHA256SUMS.asc SHA256SUMS
```

7. 上传 binary archive、对应 source archive、`SHA256SUMS`、签名、release notes 和
   key fingerprint。每个 GPL binary 同处提供精确对应源码是发布政策的一部分。
8. 在第二台机器下载验证后再公告。

不要签名托管平台生成的可变 branch archive。应签名项目构建的版本化 archive 和 checksum
manifest。未来采用 Sigstore 时要公布 identity/issuer policy，并在迁移期保留 OpenPGP。

用户通过 `gpg --verify`、`sha256sum -c` 和 `myvmp --version` 验证；打印版本必须与签名
tag 和 archive 根目录一致。
