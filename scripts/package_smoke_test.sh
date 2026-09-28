#!/usr/bin/env sh
set -eu

version=$(tr -d '\n' < VERSION)
mkdir -p build/tmp
archive="build/dist/myvmp-${version}-linux-$(uname -m)-tool.tar.gz"
test -f "$archive"
listing=$(tar -tzf "$archive")
printf '%s\n' "$listing" | grep -q "/myvmp$"
printf '%s\n' "$listing" | grep -q "/README.md$"
printf '%s\n' "$listing" | grep -q "/docs/index.md$"

temporary=$(mktemp -d build/tmp/package-smoke.XXXXXX)
trap 'rm -r "$temporary"' EXIT
tar -xzf "$archive" -C "$temporary"
tool=$(find "$temporary" -type f -name myvmp -print -quit)
test -n "$tool"
test "$("$tool" --version)" = "myVMP $version"
"$tool" --help >/dev/null 2>&1 || test $? -eq 2
printf '%s\n' 'package smoke test passed'
