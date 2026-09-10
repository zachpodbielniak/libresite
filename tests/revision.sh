#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/revision.sh BINARY' 'Example: bash tests/revision.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
# This directory belongs exclusively to the fixture; never clean user projects.
trap 'rm -rf -- "$test_dir"' EXIT
export CODEX_PATH="$root/tests/fixtures/provider.sh" LS_TEST_PROVIDER=codex
export LS_TEST_ARGS="$test_dir/args" LS_TEST_INPUT="$test_dir/input"
export LS_TEST_FAIL=0 LS_TEST_SLEEP=0
mkdir "$test_dir/source"
printf '%s\n' 'Original homepage' > "$test_dir/source/index.html"
printf '%s\n' 'User file stays byte-identical' > "$test_dir/source/keep.txt"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$test_dir/source/check.sh"
chmod 700 "$test_dir/source/check.sh"
# YAML emission cannot silently ignore a requested source revision.
if "$binary" -r "$root/recipes/revision.yaml" --revise "$test_dir/source" \
	--emit-yaml > "$test_dir/error" 2>&1
then
	exit 1
fi
rg -q 'Choose one' "$test_dir/error"
if "$binary" --from-form "$test_dir/unused.json" --revise "$test_dir/source" \
	> "$test_dir/error" 2>&1
then
	exit 1
fi
rg -q 'do not combine' "$test_dir/error"
# The change brief travels through the recipe, with current source in previews.
"$binary" -r "$root/recipes/revision.yaml" --revise "$test_dir/source" --validate
"$binary" -r "$root/recipes/revision.yaml" --revise "$test_dir/source" --preview revision > "$test_dir/preview"
rg -q 'SITE DATA brief is the change brief' "$test_dir/preview"
rg -q 'Original homepage' "$test_dir/preview"
rg -q 'contact form' "$test_dir/preview"
"$binary" -r "$root/recipes/revision.yaml" --revise "$test_dir/source" -o "$test_dir/revised"
rg -q 'Offline site' "$test_dir/revised/index.html"
rg -qx 'Original homepage' "$test_dir/source/index.html"
cmp "$test_dir/source/keep.txt" "$test_dir/revised/keep.txt"
cmp "$test_dir/source/check.sh" "$test_dir/revised/check.sh"
test -x "$test_dir/revised/check.sh"
test "$(git -C "$test_dir/revised" log -1 --format=%s)" = 'feat: revise existing project'
test "$(git -C "$test_dir/revised" rev-list --count HEAD)" -eq 1
test -z "$(git -C "$test_dir/revised" status --porcelain)"
# Generated outputs are directly reusable; administrative files are excluded.
"$binary" -r "$root/recipes/revision.yaml" --revise "$test_dir/revised" -o "$test_dir/third"
cmp "$test_dir/revised/keep.txt" "$test_dir/third/keep.txt"
cmp "$test_dir/revised/.gitignore" "$test_dir/third/.gitignore"
# A source named like the output workspace must never acquire lock/checkpoint files.
mkdir -m700 "$test_dir/collision.libresite-work"
cp "$test_dir/source/keep.txt" "$test_dir/collision.libresite-work/keep.txt"
if "$binary" -r "$root/recipes/revision.yaml" --revise "$test_dir/collision.libresite-work" \
	-o "$test_dir/collision" --resume > "$test_dir/error" 2>&1
then
	exit 1
fi
rg -q 'must not overlap' "$test_dir/error"
test ! -e "$test_dir/collision.libresite-work/lock"
test ! -e "$test_dir/collision.libresite-work/checkpoint.json"
# Existing destinations, nested outputs, missing sources and binary input fail
# without provider calls. The successful fixture above is the positive control.
for mode in existing nested alias missing binary
do
	project="$test_dir/source"
	output="$test_dir/rejected-$mode"
	case "$mode" in
		existing) output="$test_dir/source" ;;
		nested) output="$test_dir/source/child" ;;
		alias) ln -s "$test_dir/source" "$test_dir/alias"; output="$test_dir/alias/child" ;;
		missing) project="$test_dir/missing" ;;
		binary) printf '\0' > "$test_dir/source/image.png" ;;
	esac
	printf '%s\n' 'no provider call' > "$test_dir/input"
	if "$binary" -r "$root/recipes/revision.yaml" --revise "$project" -o "$output" > "$test_dir/error" 2>&1
	then
		exit 1
	fi
	rg -qx 'no provider call' "$test_dir/input"
	if [[ "$mode" != existing ]]
	then
		test ! -e "$output"
	fi
done
printf '%s\n' 'REVISION VERIFIED'
