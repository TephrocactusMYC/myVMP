# 使用指南

[English](../usage.md)

## 准备目标

myVMP 保护已链接 ELF 中的具名函数。应在 strip 前保护，保证函数不被 inline 且 symbol
size 非零。初次接入建议使用普通 scalar C/C++，把暂不支持的操作隔离到 native helper。

```c
__attribute__((noinline, used))
uint64_t verify_token(uint64_t token, uint64_t secret) {
    return ((token ^ secret) * 0x9e3779b97f4a7c15ULL) + 7;
}
```

首轮建议 `-O2 -g -fno-inline -fno-stack-protector`。最后一项用于避免被选函数中的平台
TLS canary 序列，并不是建议整个应用关闭栈保护。

## 检查、dry-run 与保护

```sh
./build/myvmp inspect-elf app.arm64 verify_token
./build/myvmp --log-dir analysis/logs \
  protect-dry-run app.arm64 verify_token 1001
./build/myvmp protect app.arm64 verify_token app.protected.arm64 1001
```

`inspect-elf` 报告 ELF、symbol、relocation、detour 和 payload 规划。dry-run 完成验证、
lifting、encoding 和规划但不写 ELF；日志可包含 IR、bytecode、profile 和入口字节。
`protect` 写新 ELF 和 `OUTPUT.myvmp.json`，绝不原地修改输入。

启用 sibling multi-VM：

```sh
./build/myvmp protect app.arm64 verify_token app.multi.arm64 \
  1001 --multi-vm 2002
```

它要求不同的非零 seed 以及至少两个可实际分区的 CFG block。

## 批量事务保护

```sh
./build/myvmp protect-many INPUT OUTPUT MASTER_SEED \
  [--multi-vm SECONDARY_MASTER] SYMBOL...
```

```sh
./build/myvmp protect-many service.rv64 service.protected.rv64 123456 \
  verify_license derive_key authorize_request
```

命令按函数顺序派生 seed，先 staging 全部转换，只有所有符号成功才发布。它生成最终
`.myvmp.json` 与 `.myvmp.batch.json`，并拒绝覆盖已有文件。超大型项目应选择关键、耗时
可控的函数，而不是默认虚拟化所有代码。

## 行为验证

```sh
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.arm64 > original.out
qemu-aarch64 -L /usr/aarch64-linux-gnu ./app.protected.arm64 > protected.out
cmp original.out protected.out

qemu-riscv64 -L /usr/riscv64-linux-gnu ./app.rv64 > original.out
qemu-riscv64 -L /usr/riscv64-linux-gnu ./app.protected.rv64 > protected.out
cmp original.out protected.out
```

应运行应用的真实测试，覆盖边界值、失败路径、native call、共享库、并发和相关 FP 特殊值。
哈希不同不能证明语义正确。

## Lifting、profile 和质量

```sh
./build/myvmp lift-arm64 --base-va 0x400770 0xd28000a0 0xd65f03c0
./build/myvmp lift-rv64 0x00500513 0x00700593 0x00b50533 0x00008067
./build/myvmp lift-arm32 0xe2800005 0xe0200001 0xe12fff1e
./build/myvmp lift-elf-symbol app.arm64 verify_token
./build/myvmp profile-manifest 0x20260822
./build/myvmp quality-report --json before.bin after.bin
```

`quality-report` 支持 `--max-same-position`、`--max-word32-exposure`、
`--max-word64-exposure`、`--max-literal32-exposure`、`--max-literal64-exposure` 和
`--max-size-ratio` 门槛；失败时退出码为 1。函数级评估应比较提取出的函数/payload 区间，
而不是被大量未保护代码主导的整个 ELF。

## 错误契约

失败时 stderr 第一行是稳定的 `error_code=...`，随后是面向人的诊断。当前类别包括
`INVALID_ARGUMENT`、`IO_ERROR`、`SYMBOL_NOT_FOUND`、`UNSUPPORTED_ARCHITECTURE`、
`UNSUPPORTED_INSTRUCTION`、`UNSAFE_PATCH`、`OUTPUT_CONFLICT` 和 `PROTECTION_ERROR`。
自动化脚本应依赖错误代码，而不要匹配可变的自然语言诊断。

## ARM32/A32 后端

```sh
arm-linux-gnueabihf-gcc -O2 -g -marm -fno-inline -fno-stack-protector app.c -o app.arm32
./build/myvmp protect app.arm32 verify_token app.protected.arm32 1001
./build/myvmp protect app.arm32 verify_token app.multi.arm32 1001 --multi-vm 2002
qemu-arm -L /usr/arm-linux-gnueabihf ./app.protected.arm32
```

该后端支持小端 ELF32 ARM-state 标量函数、动态 opcode/operand codec、CFG block
重排、原生/内部调用、直接与非 PIE 绝对远跳、manifest、批量保护和 sibling multi-VM。
Thumb、VFP/NEON、特权指令及任意编译器输出仍属于失败关闭边界。

## 常见问题

- symbol 缺失/size 为零：在 strip 前保护，检查 `readelf -Ws`。
- 指令不支持：用 `lift-elf-symbol` 定位，将操作简化或移动到 native helper。
- relocation 被拒绝：缩小函数边界或改变 codegen，不要手工删除 relocation。
- multi-VM 被拒绝：使用多 CFG block 函数和两个不同非零 seed。
- QEMU loader 失败：安装目标 sysroot 或构建正确的静态 fixture。
- runtime blob 不一致：重新生成、审查汇编 diff，再执行 `make check-<arch>-runtime`。
