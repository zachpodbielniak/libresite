#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/edit.sh BINARY' 'Example: bash tests/edit.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
# This directory belongs exclusively to the fixture; never clean user projects.
trap 'rm -rf -- "$test_dir"' EXIT
# The controlled provider would answer; these refusals must happen before any call.
export CLAUDE_CODE_PATH="$root/tests/fixtures/provider.sh" LS_TEST_PROVIDER=claude-code
export LS_TEST_ARGS="$test_dir/args" LS_TEST_INPUT="$test_dir/input"
expect_refusal(){
	local code=$1 needle=$2
	shift 2
	set +e
	"$binary" "$@" > "$test_dir/out" 2> "$test_dir/error"
	local status=$?
	set -e
	test "$status" -eq "$code"
	rg -q -- "$needle" "$test_dir/error"
	test ! -e "$test_dir/args"
}
"$binary" --help | rg -q -- '--edit'
"$binary" --help | rg -q -- '--change'
mkdir "$test_dir/plain"
printf '%s\n' '<h1>Not built here</h1>' > "$test_dir/plain/index.html"
expect_refusal 2 'Use --edit DIR --change TEXT' --edit "$test_dir/plain"
expect_refusal 2 'Use --edit DIR --change TEXT' --change 'Make it blue'
expect_refusal 2 'Use --edit DIR --change TEXT' --edit "$test_dir/plain" --change 'x' -r "$root/recipes/studio.yaml"
expect_refusal 2 'Use --edit DIR --change TEXT' --edit "$test_dir/plain" --change 'x' -o "$test_dir/out-site"
expect_refusal 2 '--provider must be' --edit "$test_dir/plain" --change 'x' --provider nobody
expect_refusal 2 'not a built site' --edit "$test_dir/plain" --change 'Make it blue'
expect_refusal 2 'change request' --edit "$test_dir/plain" --change '   '
# A plain directory is untouched by a refused edit.
test "$(cat "$test_dir/plain/index.html")" = '<h1>Not built here</h1>'
test ! -e "$test_dir/plain/.libresite"
printf '%s\n' 'EDIT CLI VERIFIED'
