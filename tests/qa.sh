#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/qa.sh BINARY' 'Example: bash tests/qa.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
"$binary" --list-modules | rg -x gate
"$binary" --help | rg -q -- '--qa'
"$binary" --help | rg -q -- '--approve'
"$binary" -r "$root/recipes/landing.yaml" --preview design-gate > "$test_dir/gate-preview"
rg -q '^GATE$' "$test_dir/gate-preview"
rg -q 'docs/design.org' "$test_dir/gate-preview"
mkdir -p "$test_dir/bad" "$test_dir/good"
printf '%s\n' '<html><title>x</title></html>' > "$test_dir/bad/index.html"
if "$binary" --qa "$test_dir/bad" > "$test_dir/bad-report"
then
	exit 1
fi
rg -q 'missing <!doctype html' "$test_dir/bad-report"
printf '%s' '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Hi</title></head><body><h1>Hi</h1></body></html>' > "$test_dir/good/index.html"
"$binary" --qa "$test_dir/good" > "$test_dir/good-report"
rg -q 'OK structural' "$test_dir/good-report"
export CODEX_PATH="$root/tests/fixtures/provider.sh"
export LS_TEST_ARGS="$test_dir/args" LS_TEST_INPUT="$test_dir/input" LS_TEST_CWD="$test_dir/cwd"
export LS_TEST_PROVIDER=codex
cat > "$test_dir/gated.yaml" <<'YAML'
version: 1
name: gated-cli
site: {brief: Build a tiny test site}
ai: {provider: codex, retries: 0, timeout: 2}
pipeline:
  steps:
    - {id: site, type: frontend}
    - id: design-gate
      type: gate
      options: {message: Stop, review: [index.html]}
    - id: verify
      type: validate
      options:
        required: [index.html]
YAML
# An unapproved gate is a pause, not a failure: its own exit status (7).
status=0
"$binary" -r "$test_dir/gated.yaml" -o "$test_dir/blocked" > "$test_dir/blocked.log" 2>&1 || status=$?
test "$status" -eq 7
rg -q "Gate 'design-gate' needs approval" "$test_dir/blocked.log"
test -f "$test_dir/blocked.libresite-work/review/design-gate/index.html"
"$binary" -r "$test_dir/gated.yaml" -o "$test_dir/blocked" --resume --approve design-gate
test -f "$test_dir/blocked/index.html"
"$binary" --qa "$test_dir/blocked" > "$test_dir/published-qa"
rg -q 'OK structural' "$test_dir/published-qa"
printf '%s\n' 'QA VERIFIED'
