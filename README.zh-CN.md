# myVMP

[English README](README.md) · [英文文档](docs/index.md) · [中文文档](docs/zh-CN/index.md) · 版本 0.1.0

myVMP 是一个实验性的函数级 ELF 虚拟化保护工具，主要支持 AArch64 和 RV64，并提供
范围较小的 ARM/A32 后端。它把已链接 ELF 中选定的函数提升为架构无关 IR，编码为随
seed 变化的 VM 字节码，嵌入原生解释器和加密载荷，再向新 ELF 安装安全入口跳板。
输入文件永远不会被原地修改。

## 项目优势

- **二进制级工作流：**直接保护已链接 ELF 的具名函数，无需编译器插件。
- **每次构建动态变化：**seed 会改变 opcode 映射、operand codec、基本块布局、加密、
  立即数降级、dispatcher 常量、handler 变体和原生函数体填充。
- **真实 sibling multi-VM：**基本块由两个独立 profile 动态持有，并通过经过验证的
  bridge 跨 VM 执行，而不是静态放置第二个 dispatcher。
- **跨 ISA 架构：**AArch64 和 RV64 共享 IR、字节码、profile、manifest 和质量设施，
  同时保留独立的 lifter、runtime 与跳板实现。
- **失败关闭：**不支持的指令、危险重定位、畸形 ELF 和不安全 patch 计划会在发布前拒绝。
- **可度量：**提供可复现 manifest、字节暴露指标、多 seed 差异门禁、QEMU 行为对比和
  大型 ELF 压力测试。

myVMP 是研究项目，不宣称保护不可逆。建议只保护经过充分测试的关键函数，并结合自身
威胁模型评估安全性与性能成本。

## 构建

基本环境需要 Linux、GNU Make、C++20 编译器和 Python 3：

```sh
make -j$(nproc)
make test
```

主要产物是 `build/myvmp`、`build/myvmp_demo` 和 `build/myvmp_tests`。构建发布包：

```sh
make release
make package
```

交叉编译器和 QEMU 只在重建目标 runtime 或执行目标架构行为测试时需要。完整说明见
[编译与平台](docs/zh-CN/build-and-platforms.md)。

Windows 推荐 WSL2；Windows/macOS 均可使用官方 Ubuntu 容器工作流：

```sh
docker build -t myvmp:0.1.0 .
docker run --rm myvmp:0.1.0 build/myvmp --version
```

## 保护单个函数

工具读取保留非零长度符号的已链接 ELF。建议为目标函数添加 `noinline` 和 `used`：

```c
__attribute__((noinline, used))
uint64_t protected_calculation(uint64_t x) {
    return (x * 17u) ^ 0x55aa55aaULL;
}
```

```sh
aarch64-linux-gnu-gcc -O2 -g -fno-inline -fno-stack-protector app.c -o app.arm64
./build/myvmp inspect-elf app.arm64 protected_calculation
./build/myvmp protect-dry-run app.arm64 protected_calculation 1001
./build/myvmp protect app.arm64 protected_calculation app.protected.arm64 1001
```

输出旁会生成 `.myvmp.json` manifest。发布前必须比较完整程序行为：

```sh
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.arm64 > before.txt
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.protected.arm64 > after.txt
cmp before.txt after.txt
```

RV64 使用 `riscv64-linux-gnu-gcc` 和 `qemu-riscv64 -L /usr/riscv64-linux-gnu`。

## sibling multi-VM

至少有两个 CFG 基本块的函数可以启用两个独立 seed 的 VM：

```sh
./build/myvmp protect app.arm64 protected_calculation app.multi.arm64 \
  1001 --multi-vm 2002
```

manifest 会记录两个 profile、每个块的 owner 以及所有 bridge。零 seed、相同 seed、
单基本块和非法 owner 都会失败关闭。

## 批量保护选定函数

```sh
./build/myvmp protect-many app.rv64 app.protected.rv64 123456 \
  verify_license derive_session_key validate_transaction
```

`protect-many` 从 master seed 派生每个函数的 seed，并且只有所有函数都成功后才发布结果。
它会生成 `.myvmp.json` 和 `.myvmp.batch.json`，并拒绝覆盖已有输出。

## 分析和质量报告

```sh
./build/myvmp lift-elf-symbol app.arm64 protected_calculation
./build/myvmp quality-report --json original.bin protected.bin
python3 scripts/generate_analysis_report.py \
  --tool build/myvmp --input app.arm64 --protected app.protected.arm64 \
  --symbol protected_calculation --output analysis
```

报告包含 ELF 规划、profile 元数据、哈希和字节质量指标。这些指标用于回归，不能单独
证明抵抗反虚拟化的能力。

## 文档

- [中文文档导航](docs/zh-CN/index.md)
- [架构和保护流水线](docs/zh-CN/architecture.md)
- [混淆和 multi-VM](docs/zh-CN/obfuscation.md)
- [完整使用指南](docs/zh-CN/usage.md)
- [分析报告](docs/zh-CN/analysis-and-reports.md)
- [平台与环境](docs/zh-CN/build-and-platforms.md)
- [安全模型](docs/zh-CN/security-model.md)
- [测试](docs/zh-CN/testing.md)
- [调研与参考文献](docs/zh-CN/research.md)
- [后续路线](docs/zh-CN/roadmap.md)
- [贡献指南](CONTRIBUTING.zh-CN.md)、[安全政策](SECURITY.zh-CN.md)和
  [发布签名指南](RELEASING.zh-CN.md)

## 许可证

myVMP 使用 [GNU GPL v3.0 only](LICENSE)。贡献内容采用相同许可证，并按
[贡献指南](CONTRIBUTING.zh-CN.md)使用 DCO sign-off。
