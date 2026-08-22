# 安全政策

[English](SECURITY.md)

## 支持版本

在 1.0 之前，只为最新 tagged minor release 和默认分支提供安全修复。0.1.x 是研究预览，
必须结合 `docs/zh-CN/security-model.md` 中的限制评估。

## 报告漏洞

可利用的 parser/writer 缺陷、保护输出代码执行错误、不安全发布或 secret 泄露不要提交公开
issue。请使用托管平台的 private security advisory。公开仓库前，应在仓库设置中配置有人
维护的安全联系人；如果希望使用邮件，请用真实地址替换英文政策中的相关段落。

报告应包含版本/commit、host/target 架构、toolchain、最小复现、命令/seed、预期影响和
artifact 是否可分享。不要发送生产 secret 或第三方专有二进制。

项目力求 7 天内确认完整报告、14 天内评估严重性并协调修复/公告。这是社区项目的尽力目标，
不是 SLA。关于逆向抵抗能力的研究争论不视为漏洞，除非与明确保证冲突；memory corruption、
不安全 ELF 发布和语义错误属于安全问题。
