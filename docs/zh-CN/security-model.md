# 安全模型与限制

[English](../security-model.md)

myVMP 用于提高 Linux ELF 中选定 user-mode 函数的静态分析和常规 patch 成本。假设分析者
能够持有并执行文件、检查内存、跟踪指令和比较样本。目标是差异化和分析摩擦，而不是面对
本地全能力对手时的绝对保密。

## 提供的能力

- 选定原生语义被 encoded/encrypted VM instruction 替代；
- source/VM 对应关系随 seed 和 block 改变；
- 安装验证过的 detour 后销毁原生函数体；
- sibling 模式沿 CFG edge 动态改变 owner；
- manifest 与确定性 seed 支持审计和复现；
- 畸形或不支持输入在最终发布前失败；
- host 与 QEMU 回归持续检查语义。

## 不提供的能力

- 抵抗反虚拟化、符号执行或动态 tracing 的数学证明；
- anti-debug、anti-dump、remote attestation 或安全密钥存储；
- 运行时 plaintext state 已被观察之后的机密性；
- 对任意 compiler output、kernel/privileged instruction 的自动支持；
- 应用本身的 memory-safety hardening；
- tamper-proof manifest 或签名发布。

分析者最终可以建模 fetch/decode/dispatch/handler。multi-VM 和多态会提高 normalization
成本，但复杂度不等于密码学保证。

## 失败关闭边界

不支持 machine/class、symbol 缺失或 size 为零、未知指令、不安全 relocation、无法表示的
branch、非法 CFG/VM metadata、payload/runtime 上限和不一致 sibling partition 都会拒绝。
ARM/A32 不支持 Thumb/multi-VM；x86-64 target 尚未实现。runtime 保存 vector state 不代表
未经回归的 SIMD/vector 语义已经支持。

## 操作建议

- 保护少量高价值函数，而不是整个超大型应用。
- 把环境相关或不支持操作放到经过 call-gate 测试的 native helper。
- qualification 使用多个 seed，release 使用受控 seed。
- 对真实输入、并发和错误路径比较完整行为。
- 如果 sidecar 暴露 profile，把它视为敏感诊断 metadata。
- 通过外部发布系统签名最终 artifact。
- 在真实硬件测量成本；QEMU ratio 只用于回归。

缺陷报告应包含 target、compiler/linker version、最小源码或可分发 ELF、symbol、完整命令
和 seed、dry-run logs、sidecar、原始/保护输出与最小 mismatch。不要公开专有二进制或
生产 secret seed。
