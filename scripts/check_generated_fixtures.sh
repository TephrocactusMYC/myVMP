#!/usr/bin/env sh
set -eu

tmp_dir="${TMPDIR:-/tmp}/myvmp-generated-fixtures-$$"
trap 'rm -rf "$tmp_dir"' EXIT INT HUP TERM

mkdir -p "$tmp_dir"
MYVMP_FIXTURE_OUT="$tmp_dir" python3 scripts/generate_fixtures.py > /dev/null
diff -ru fixtures/generated "$tmp_dir"
printf '%s\n' "generated fixtures are up to date"
