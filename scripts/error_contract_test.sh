#!/usr/bin/env sh
set -eu

tool="${MYVMP_TOOL:-build/myvmp}"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

if "$tool" protect /definitely/missing.elf missing "$tmp" 1 > /dev/null 2> "$tmp"; then
    printf '%s\n' 'expected protect to fail' >&2
    exit 1
fi
grep -q '^error_code=IO_ERROR$' "$tmp"

if "$tool" lift-arm64 not-an-integer > /dev/null 2> "$tmp"; then
    printf '%s\n' 'expected lift to fail' >&2
    exit 1
fi
grep -q '^error_code=INVALID_ARGUMENT$' "$tmp"

printf '%s\n' 'CLI error contract passed'
