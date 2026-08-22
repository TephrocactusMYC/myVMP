# 参与贡献 myVMP

[English](CONTRIBUTING.md)

感谢参与改进。提交贡献即表示同意按仓库的 GPL-3.0-only 许可证发布贡献内容。

## 修改前

- 大型 ISA、bytecode format、runtime ABI 或 ELF layout 变更应先创建 issue 讨论。
- 除非抽象适用于所有 backend，否则 target-specific 语义不要进入公共 IR/bytecode。
- 不要为了接受一个 fixture 而削弱失败关闭验证。
- 不复制专有实现或许可证不兼容代码；从外部论文/项目形成的算法要记录来源。

## 开发流程

```sh
make -j$(nproc)
make test
make docs-check
```

按 `docs/zh-CN/testing.md` 运行相关目标行为、runtime blob 和质量门禁。新增逻辑操作需要
decoder/lifter test、host semantics、native handler、畸形输入处理和 QEMU 前后行为对比。

使用 C++20，保持接口小而明确，优先复用公共 format/validator，并让
`git diff --check` 通过。

PR 应说明安全/正确性问题、架构影响、执行过的测试、性能/体积变化和格式兼容性影响。
用户可见变更应加入 `CHANGELOG.md` 的 `Unreleased`。

提交应通过 `git commit -s` 加入 `Signed-off-by`，确认
[DCO 1.1](https://developercertificate.org/)。这是来源声明，不是版权转让。
