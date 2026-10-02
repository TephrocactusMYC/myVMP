# 文档导航

[English](../index.md)

本文档集描述当前已经实现的行为，而不是愿景设计。准备保护二进制时先读使用指南；接入
发布流程前应阅读架构和安全模型。

## 用户文档

- [使用指南](usage.md)：单函数、multi-VM、批量保护和 CLI。
- [编译与平台](build-and-platforms.md)：宿主环境、交叉工具链、支持目标和发布包。
- [分析报告](analysis-and-reports.md)：dry-run、manifest、质量指标和自动报告。
- [公开实测快照](readme-benchmarks.md)：README 使用的跨架构质量、体积和 QEMU 结果。
- [测试](testing.md)：单元测试、QEMU、大 ELF、差异度和发布门禁。
- [文档网站发布](site-publishing.md)：在独立 `gh-pages` 分支自动部署 MkDocs 网站。

## 设计文档

- [技术论文](paper.md)：基于实际实现、按研究论文组织的威胁模型、方法、评价和局限。
- [架构](architecture.md)：模块、数据流、ELF 改写和 runtime ABI。
- [混淆](obfuscation.md)：动态 profile、operand codec、块加密、多态和 sibling multi-VM。
- [安全模型](security-model.md)：威胁模型、保证、失败关闭边界和限制。
- [调研与参考文献](research.md)：开源项目、论文、标准及形成的设计思路。
- [路线图](roadmap.md)：发布之后的简要优先级。
- [Android 原生保护远景](android-vision.md)：未来 NDK `.so` 目标的可行性、集成路线、
  复用分析和开发决策门；目前仅调研。

## 项目治理

- [更新日志](https://github.com/TephrocactusMYC/CactiVM/blob/master/CHANGELOG.md)
- [贡献指南](https://github.com/TephrocactusMYC/CactiVM/blob/master/CONTRIBUTING.zh-CN.md)
- [安全政策](https://github.com/TephrocactusMYC/CactiVM/blob/master/SECURITY.zh-CN.md)
- [GPL-3.0-only 许可证](https://github.com/TephrocactusMYC/CactiVM/blob/master/LICENSE)

```text
src/{arm64,riscv64}  ISA 解码和提升
src/ir               架构无关语义
src/bytecode         VM ISA、profile 和 codec
src/vm               host VM 与 sibling 模型
runtime + src/runtime 原生解释器及 payload builder
src/elf + src/patch   ELF 解析、规划、跳板和写入
src/protect           端到端保护器
scripts + tests       生成、质量、集成和回归
```

旧的阶段性计划已收敛到这套文档，避免发布用户混淆“曾经计划”和“当前行为”。原始材料
仍保留在 Git 历史中。
