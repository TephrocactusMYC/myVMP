# Changelog / 更新日志

All notable changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
versions follow [Semantic Versioning](https://semver.org/).

所有重要变更遵循 Keep a Changelog，版本号遵循语义化版本。

## [Unreleased]

### Changed / 变更

- Added a bilingual, implementation-grounded technical paper covering the architecture,
  threat model, evaluation, attacker baseline and limitations; removed the Pages workflow's
  dependency on an assumed `rg` executable when checking Mermaid output.
- 新增中英双语技术论文稿，系统描述架构、威胁模型、评价、攻击者基线和局限；Pages 的 Mermaid
  检查不再假设 runner 已安装 `rg`。
- Extended the reference devirtualizer to reverse AArch64, ARM32 and RV64 runtime bytecode
  encoding without a sidecar, recover VM instructions and static CFG edges, and compare
  them with sidecars in real protected-ELF regressions. Handler effects and source-level
  semantic IR recovery remain future work.
- 扩展参考反虚拟化分析工具：无需 sidecar 即可撤销 AArch64、ARM32、RV64 runtime bytecode
  编码并恢复 VM 指令与静态 CFG 边；真实保护 ELF 回归会与 sidecar 对比。handler 效果和
  源码级语义 IR 恢复仍为后续工作。
- Switched the Pages site to pinned Material for MkDocs with Mermaid support and a deployment
  check that diagram containers are emitted for both English and Chinese pages.
- Pages 网站改用固定版本的 Material for MkDocs 支持 Mermaid，并在部署前检查中英文页面
  均生成图表容器。
- Replaced the slow build/test workflow with a documentation-only GitHub Pages publisher;
  MkDocs sources remain Markdown on the source branch and generated pages go to `gh-pages`.
- 将耗时的编译/测试 workflow 替换为仅发布文档的 GitHub Pages 流程；源码分支只保留
  Markdown，生成的网站推送到 `gh-pages`。
- Removed accidentally tracked build/sanitizer outputs and obsolete internal release
  preparation documents; expanded ignore rules for generated artifacts.
- 删除误跟踪的 build/sanitizer 产物和过时的内部发布准备文档，并完善生成物忽略规则。
- Added an attacker-side payload/manifest oracle, bounded same-block superoperator mining,
  and a reversible live-state model exercised by the host sibling interpreter. Bytecode
  synthesis and native runtime state bridges are not yet integrated. VM calls transfer the
  full caller state across profiles and resolve shuffled-block return continuations.
- 新增攻击者视角 payload/manifest 分析工具、有界同基本块 superoperator 挖掘，以及由 host
  sibling interpreter 实际执行验证的可逆 live-state 模型。字节码合成和原生 runtime 状态桥
  尚未集成。跨 profile 的 VM 调用会传递完整调用者状态，并正确解析乱序 block 的返回续点。

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
