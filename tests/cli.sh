#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/cli.sh BINARY' 'Example: bash tests/cli.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
# Only this script's newly-created temporary directory is removed.
trap 'rm -rf -- "$test_dir"' EXIT
"$binary" --help > "$test_dir/help"
rg -q 'Examples:' "$test_dir/help"
"$binary" --license > "$test_dir/license"
cmp "$root/LICENSE" "$test_dir/license"
"$binary" --version | rg -q 'libresite 0.1.0'
for recipe in "$root"/recipes/*.yaml
do
	"$binary" --recipe "$recipe" --validate
	"$binary" --recipe "$recipe" --plan > "$test_dir/plan"
	# Resolve every catalog stage, including custom generate/prompt pairs.
	# A valid recipe alone does not establish that its prompt keys exist.
	while IFS=$'\t' read -r _ step module _ _
	do
		if [[ "$module" != validate ]]
		then
			"$binary" --recipe "$recipe" --preview "$step" > "$test_dir/preview"
			rg -q 'CURRENT PROJECT' "$test_dir/preview"
		fi
	done < "$test_dir/plan"
done
"$binary" --recipe "$root/recipes/landing.yaml" --preview design > "$test_dir/preview"
rg -q 'DESIGN DIRECTION' "$test_dir/preview"
for limit in -1 9223372036854775808 invalid
do
	if "$binary" -r "$root/recipes/flagship.yaml" --preview polish --max-prompt-bytes "$limit" > /dev/null 2>&1
	then
		exit 1
	fi
done
if "$binary" -r "$root/recipes/flagship.yaml" --preview polish --max-prompt-bytes 1 > "$test_dir/limit" 2>&1
then
	exit 1
fi
rg -q 'Prompt needs .* bytes; limit is 1' "$test_dir/limit"
"$binary" -r "$root/recipes/flagship.yaml" --preview polish --max-prompt-bytes 262144 > "$test_dir/preview"
if "$binary" --recipe "$root/recipes/landing.yaml" --preview nonexistent > /dev/null 2>&1
then
	exit 1
fi
export CODEX_PATH="$root/tests/fixtures/provider.sh"
export CLAUDE_CODE_PATH="$CODEX_PATH" CURSOR_AGENT_PATH="$CODEX_PATH" GROK_PATH="$CODEX_PATH" AGY_PATH="$CODEX_PATH"
export LS_TEST_ARGS="$test_dir/args" LS_TEST_INPUT="$test_dir/input"
for provider in codex claude-code cursor grok-build antigravity
do
	export LS_TEST_PROVIDER="$provider"
	cat > "$test_dir/recipe.yaml" <<YAML
version: 1
name: adapter-test
site: {brief: Build a tiny test site}
ai: {provider: $provider, model: fixture-model, retries: 0, timeout: 2}
pipeline:
  steps:
    - {id: site, type: frontend}
    - {id: verify, type: validate, options: {required: [index.html]}}
YAML
	"$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/$provider"
	rg -q 'Offline site' "$test_dir/$provider/index.html"
	test -f "$test_dir/$provider/.libresite/run.json"
	rg -q 'fixture-model' "$test_dir/args"
	# Unattended adapters must skip approval prompts; otherwise Codex exec
	# waits forever for sandbox-escalation on a non-TTY stdin.
	case "$provider" in
		codex)
			rg -qx -- '--dangerously-bypass-approvals-and-sandbox' "$test_dir/args"
			if rg -qx -- '--sandbox' "$test_dir/args"
			then
				exit 1
			fi
			;;
		claude-code|antigravity)
			rg -qx -- '--dangerously-skip-permissions' "$test_dir/args"
			;;
		cursor)
			rg -qx -- '--force' "$test_dir/args"
			;;
		grok-build)
			rg -qx -- '--permission-mode' "$test_dir/args"
			rg -qx -- 'bypassPermissions' "$test_dir/args"
			;;
		*)
			exit 1
			;;
	esac
	# Output conflicts must fail even with resume requested.
	if "$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/$provider" --resume > /dev/null 2>&1
	then
		exit 1
	fi
done
# Prove a failing subprocess cannot publish an empty project.
export LS_TEST_PROVIDER=codex LS_TEST_FAIL=1
sed 's/provider: antigravity/provider: codex/' "$test_dir/recipe.yaml" > "$test_dir/failure.yaml"
if "$binary" -r "$test_dir/failure.yaml" -o "$test_dir/failure" > "$test_dir/failure.log" 2>&1
then
	exit 1
fi
test ! -e "$test_dir/failure"
test -f "$test_dir/failure.libresite-work/checkpoint.json"
export LS_TEST_FAIL=0
"$binary" -r "$test_dir/failure.yaml" -o "$test_dir/failure" --resume --max-prompt-bytes 262144
# ai-glib enforces process deadlines; cancellation must not masquerade as success.
export LS_TEST_SLEEP=1
if "$binary" -r "$test_dir/failure.yaml" -o "$test_dir/timeout" > "$test_dir/timeout.log" 2>&1
then
	exit 1
fi
test ! -e "$test_dir/timeout"
# Send a real signal while the CLI's worker is waiting for its subprocess.
"$binary" -r "$test_dir/failure.yaml" -o "$test_dir/cancel" > "$test_dir/cancel.log" 2>&1 &
run_pid=$!
sleep 0.2
kill -TERM "$run_pid"
run_status=0
wait "$run_pid" || run_status=$?
test "$run_status" -eq 130
test ! -e "$test_dir/cancel"
export LS_TEST_SLEEP=0
printf '%s\n' 'CLI VERIFIED (five subprocess adapters, failures, resume, deadline, cancellation)'
