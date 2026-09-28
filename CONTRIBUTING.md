# Contributing to myVMP

[中文](CONTRIBUTING.zh-CN.md)

Thank you for improving myVMP. By contributing, you agree that your contribution is
licensed under GPL-3.0-only, the repository license.

## Before opening a change

- Discuss large ISA, bytecode-format, runtime-ABI or ELF-layout changes in an issue first.
- Keep target-specific semantics out of common IR/bytecode unless the abstraction applies
  to every backend.
- Never weaken fail-closed validation merely to accept another fixture.
- Do not copy proprietary implementations or code with an incompatible license. Record
  provenance for algorithms derived from external publications or projects.

## Development workflow

```sh
make -j$(nproc)
make test
make docs-check
make style-check
```

Run the relevant target behavior, runtime-blob and quality gates described in
`docs/testing.md`. New logical operations require decoder/lifter tests, host semantics,
native runtime handlers, malformed-input behavior and QEMU original/protected comparison.

Use C++20 and four-space indentation with no tabs or trailing whitespace. Public C++
headers start with `#pragma once`; text files use UTF-8/LF and end with a newline. Keep
interfaces small, prefer shared format/validation components, and preserve unrelated
working-tree changes. Before submitting C++ changes, run the strict build and relevant tests
locally. GitHub Actions is reserved for publishing the Markdown documentation site; setup
and branch requirements are documented in `docs/site-publishing.md`.

## Pull requests

Describe the threat/correctness problem, architecture impact, tests executed, performance
and size changes, and any format compatibility effect. Keep commits reviewable and add an
entry under `CHANGELOG.md` → `Unreleased` for user-visible changes.

## Developer Certificate of Origin

Commits should include a `Signed-off-by` line certifying the
[Developer Certificate of Origin 1.1](https://developercertificate.org/):

```sh
git commit -s
```

This is provenance attestation, not copyright assignment.
