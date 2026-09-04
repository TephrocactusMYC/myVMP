# Changelog / 更新日志

All notable changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
versions follow [Semantic Versioning](https://semver.org/).

所有重要变更遵循 Keep a Changelog，版本号遵循语义化版本。

## [Unreleased]

### Changed / 变更

- Replaced pre-release automation with strict public-repository quality gates: GCC/Clang
  warning-free builds, source hygiene, ShellCheck, documentation, runtime reproducibility,
  and protected-ELF QEMU tests for all supported architectures.
- 将发布前自动化替换为严格的公开仓库质量门禁：GCC/Clang 零 warning 构建、源码规范、
  ShellCheck、文档、runtime 可复现检查及全部支持架构的保护后 ELF QEMU 测试。
- Removed accidentally tracked build/sanitizer outputs and obsolete internal release
  preparation documents; expanded ignore rules for generated artifacts.
- 删除误跟踪的 build/sanitizer 产物和过时的内部发布准备文档，并完善生成物忽略规则。

## [0.1.0] - 2026-08-22

### Added / 新增

- Function-level AArch64 and RV64 ELF virtualization with experimental ARM/A32 support.
- AArch64、RV64 函数级 ELF 虚拟化和实验性 ARM/A32 支持。
- Dynamic opcode profiles, unified operand codec, CFG-aware encrypted bytecode, handler
  variants, bounded superoperators and sibling multi-VM execution.
- 动态 opcode profile、统一 operand codec、CFG-aware 加密字节码、handler variant、
  受限 superoperator 和 sibling multi-VM。
- Transactional single/batch protection, manifests, quality reports and QEMU regression
  suites including large/far ELF placement.
- 事务式单函数/批量保护、manifest、质量报告、QEMU 回归以及大 ELF 远跳压力。
- English-first bilingual release documentation and automated analysis/report tooling.
- 英文优先的双语发布文档以及自动分析报告工具。

### Security / 安全

- Fail-closed ELF, relocation, instruction, branch-distance and multi-VM metadata validation.
- ELF、relocation、指令、分支距离和 multi-VM metadata 的失败关闭验证。
