# Analysis and Reports

[中文](zh-CN/analysis-and-reports.md)

## Report layers

A useful review combines four kinds of evidence:

1. **Eligibility:** ELF/symbol range, relocations and detour plan.
2. **Semantic:** lifted IR plus original/protected behavior.
3. **Transformation:** profile, block ownership, bridges and encrypted payload metadata.
4. **Quality/cost:** byte exposure, diversity, size and runtime ratios.

No one metric establishes security. In particular, high entropy may merely indicate
compression/encryption, and whole-file similarity is dominated by unprotected code.

## Built-in commands

```sh
./build/myvmp inspect-elf INPUT SYMBOL
./build/myvmp --log-dir DIR protect-dry-run INPUT SYMBOL SEED
./build/myvmp lift-elf-symbol INPUT SYMBOL
./build/myvmp profile-manifest SEED
./build/myvmp quality-report --json BEFORE AFTER
```

`protect` sidecars are JSON and include architecture, VM mode, codecs, profiles, physical
blocks, profile owner IDs, successors and bridges. `protect-many` also produces an
aggregate batch manifest.

The JSON emitted by `quality-report --json` has the stable top-level identifier
`"schema": "myvmp-quality-v1"`. Consumers should check this value before interpreting
metrics; additive fields may be introduced in a future schema version. The report exits
with status 1 when any requested threshold fails, so CI can treat it as a quality gate.

## Automated report bundle

The repository includes `scripts/generate_analysis_report.py`:

```sh
python3 scripts/generate_analysis_report.py \
  --tool build/myvmp \
  --input app.arm64 \
  --protected app.protected.arm64 \
  --symbol protected_calculation \
  --output build/reports/app
```

It writes:

```text
build/reports/app/
  report.md              human-readable summary
  report.json            machine-readable index and metrics
  inspect.txt            ELF/symbol/patch plan
  lift.txt               lifted IR, when a symbol is supplied
  quality.json           before/after byte metrics, when requested
  protected.manifest.json copied sidecar, when present
```

Use `--before-bytes` and `--after-bytes` to compare extracted function/payload byte ranges.
Without them the script generates the other evidence but skips quality scanning. Add
`--whole-elf-quality` only when a potentially expensive whole-file comparison is
intentional; unchanged application code usually dominates that result.

## Interpreting quality metrics

| Metric | Preferred direction | Meaning |
|---|---:|---|
| `same_position_ratio` | lower | unchanged bytes at the same offset |
| `longest_same_position_run` | lower | largest fixed byte island |
| aligned 32/64-bit exposure | lower | source words found in output |
| literal 32/64-bit exposure | lower | arbitrary source windows found in output |
| pairwise ciphertext distance | higher | seed-to-seed payload diversity |
| unique payloads/manifests | equal sample count | catches fixed profiles or collisions |
| owner balance | away from 0 | both sibling VMs perform meaningful work |
| bridge count | non-zero | proves actual cross-profile CFG transitions |
| runtime/size ratio | bounded | operational cost |

Thresholds should be calibrated on representative protected symbols. Whole-file size
ratio and similarity are usually misleading for a small protected function.

## Project quality suites

```sh
python3 scripts/bytecode_seed_sweep.py 16
python3 scripts/multi_vm_quality.py 8
python3 scripts/riscv64_multi_vm_quality.py 8
python3 scripts/arm64_coverage_report.py
python3 scripts/arm64_liftability_report.py
python3 scripts/riscv64_liftability_report.py
```

The Makefile exposes stable wrappers for these scripts. Prefer the Make targets in CI
because they build prerequisites and check generated runtime blobs.

## Recommended release evidence

Archive the original hash, protected hash, tool hash/version, full command, seeds through
a secret build record, sidecars, report bundle, behavior-test results, target compiler
version and runtime/size measurements. Do not publish production seeds if they are treated
as build secrets. A seed is diversity input, not a cryptographic root of trust.
