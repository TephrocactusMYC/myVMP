# Published benchmark snapshot

[中文](zh-CN/readme-benchmarks.md) · [Machine-readable JSON](examples/quality-benchmarks.json)

This is the evidence behind the compact tables in the README. It is a checked-in
snapshot from 2026-09-04, generated with the repository fixtures and the normal `myvmp`
CLI on Ubuntu 22.04. Every protected output listed here was executed under QEMU and its
stdout compared byte-for-byte with the original.

## What was measured

The quality comparison uses the original native function range as its input and the
concatenated, encrypted VM block payload ranges recorded by the output manifest as its
protected sample. This answers whether recognizable native bytes and aligned words leak
into the encrypted bytecode. It deliberately does **not** compare entire ELF files:
headers, libraries and all unselected functions are expected to stay unchanged.

The separate ELF-size cases show packaging cost. They include ARM32, AArch64 and RV64;
one-function and three-function selection; and a 135,275,392-byte AArch64 PIE that forces
the range-safe far-page detour. Exact values and environment versions are in the JSON.

## Interpreting the result

Zero positional and literal exposure, higher entropy and a passing regression threshold
are useful evidence that the codec/encryption pipeline is operating. They are not a
cryptographic proof and do not imply that virtualization cannot be analyzed. See the
[security model](security-model.md) before making deployment decisions.

To produce a report for your own binary, use
[`generate_analysis_report.py`](analysis-and-reports.md). Results depend on toolchain,
input, selected symbols, seeds and VM mode; published numbers are examples, not promises
for every ELF.
