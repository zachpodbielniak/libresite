#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/platform-hooks.sh BINARY'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
export CODEX_PATH="$root/tests/fixtures/provider.sh" LS_TEST_PROVIDER=codex
export LS_TEST_ARGS="$test_dir/args" LS_TEST_INPUT="$test_dir/input"
export LS_TEST_FAIL=0 LS_TEST_SLEEP=0
read -r -a flags <<< "$(pkg-config --cflags --libs json-glib-1.0)"
gcc -std=gnu89 -Wall -Wextra -Werror "$root/tests/check-events.c" -o "$test_dir/check" "${flags[@]}"
failures=0
expect_status() {
	local expected=$1 actual=0
	shift
	"$@" > "$test_dir/stdout" 2> "$test_dir/stderr" || actual=$?
	if [[ $actual != "$expected" ]]
	then
		printf 'Expected exit %s, got %s: %s\n' "$expected" "$actual" "$*" >&2
		failures=$((failures + 1))
	fi
}
cat > "$test_dir/recipe.yaml" <<'YAML'
version: 1
name: hooks
site: {brief: Test hooks}
ai: {provider: codex, model: fixture-model, retries: 0, timeout: 1}
seed_artifacts:
  - {path: old.txt, content: obsolete}
  - {path: move.txt, content: retained}
pipeline:
  steps:
    - {id: edit, type: generate}
    - {id: hold, type: gate}
    - {id: check, type: validate, options: {required: [new.txt]}}
YAML
printf '%s\n' 'not: [valid' > "$test_dir/invalid.yaml"
expect_status 2 "$binary" -r "$test_dir/invalid.yaml" --validate
expect_status 2 "$binary" --unknown
expect_status 2 "$binary" -r "$test_dir/recipe.yaml" --timeout -2
expect_status 1 "$binary" -r "$test_dir/missing.yaml"
expect_status 0 "$binary" -r "$root/recipes/landing.yaml" --validate --events 3 3> "$test_dir/validate.jsonl"
"$test_dir/check" "$test_dir/validate.jsonl" run.started,run.finished 0 || failures=$((failures + 1))
expect_status 2 "$binary" --events -1 -r "$test_dir/recipe.yaml" --validate
expect_status 2 "$binary" --events 2 -r "$test_dir/recipe.yaml" --validate
expect_status 2 "$binary" --events 3 --events-file "$test_dir/both" -r "$test_dir/recipe.yaml" --validate 3> /dev/null
expect_status 1 "$binary" --events 99 -r "$test_dir/recipe.yaml" --validate
export LS_TEST_BUNDLE='{"files":[],"delete":["old.txt"],"rename":[{"from":"move.txt","to":"new.txt"}]}'
expect_status 7 "$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/site" --events-file "$test_dir/edit.jsonl"
"$test_dir/check" "$test_dir/edit.jsonl" run.started,step.started,step.progress,step.completed,step.started,step.progress,step.awaiting_approval,run.finished 7 || failures=$((failures + 1))
"$binary" --help | rg -q "7 gate waiting for approval" || failures=$((failures + 1))
expect_status 0 "$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/site" --resume --approve-all --events 3 3> "$test_dir/resume.jsonl"
"$test_dir/check" "$test_dir/resume.jsonl" run.started,step.started,step.progress,step.completed,step.started,step.completed,run.finished 0 || failures=$((failures + 1))
[[ ! -e "$test_dir/site/old.txt" && ! -e "$test_dir/site/move.txt" && -f "$test_dir/site/new.txt" ]] || failures=$((failures + 1))
# Unsafe operations reject the whole response, preserving checkpoint zero.
for operation in '"delete":["../escape"]' '"rename":[{"from":"move.txt","to":"../escape"}]' \
	'"rename":[{"from":"../move.txt","to":"safe"}]' \
	'"delete":[".git/config"]' '"rename":[{"from":".libresite/run.json","to":"safe"}]' \
	'"rename":[{"from":"move.txt","to":".libresite/evil"}]'
do
	export LS_TEST_BUNDLE="{\"files\":[],$operation}"
	expect_status 4 "$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/unsafe"
	rg -q '"path":"old.txt"' "$test_dir/unsafe.libresite-work/checkpoint.json" || failures=$((failures + 1))
	rm -rf -- "$test_dir/unsafe.libresite-work"
done
unset LS_TEST_BUNDLE
export LS_TEST_FAIL=1
expect_status 4 "$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/provider" --events-file "$test_dir/failure.jsonl"
"$test_dir/check" "$test_dir/failure.jsonl" run.started,step.started,step.progress,step.failed,run.finished 4 || failures=$((failures + 1))
sed 's/Test hooks/Changed brief/' "$test_dir/recipe.yaml" > "$test_dir/changed.yaml"
expect_status 6 "$binary" -r "$test_dir/changed.yaml" -o "$test_dir/provider" --resume
export LS_TEST_FAIL=0 LS_TEST_SLEEP=1
expect_status 5 "$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/deadline"
export LS_TEST_SLEEP=0
sed '/- {id: edit/d; /- {id: hold/d' "$test_dir/recipe.yaml" > "$test_dir/validation.yaml"
expect_status 3 "$binary" -r "$test_dir/validation.yaml" -o "$test_dir/validation"
mkdir -m700 "$test_dir/locked.libresite-work"
"$test_dir/check" --lock "$test_dir/locked.libresite-work/lock" \
	"$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/locked" --resume || failures=$((failures + 1))

# Event files are exclusive, and unwritable descriptors fail without generation.
cp "$test_dir/validate.jsonl" "$test_dir/saved-events"
expect_status 1 "$binary" -r "$test_dir/recipe.yaml" --validate --events-file "$test_dir/validate.jsonl"
cmp "$test_dir/saved-events" "$test_dir/validate.jsonl"
expect_status 1 "$binary" -r "$test_dir/recipe.yaml" --validate --events 3 3> /dev/full
expect_status 1 "$binary" -r "$test_dir/recipe.yaml" --validate --events 3 3< /dev/null
# Identical human output with and without events, except timestamps/output path.
sed 's/new.txt/old.txt/' "$test_dir/validation.yaml" > "$test_dir/native.yaml"
expect_status 0 "$binary" -r "$test_dir/native.yaml" -o "$test_dir/plain"
sed 's@/plain$@/observed@' "$test_dir/stdout" > "$test_dir/plain.out"
sed 's/^\[[^]]*\] //' "$test_dir/stderr" > "$test_dir/plain.err"
expect_status 0 "$binary" -r "$test_dir/native.yaml" -o "$test_dir/observed" --events-file "$test_dir/native.jsonl"
sed 's/^\[[^]]*\] //' "$test_dir/stderr" > "$test_dir/observed.err"
cmp "$test_dir/plain.out" "$test_dir/stdout"
cmp "$test_dir/plain.err" "$test_dir/observed.err"
"$test_dir/check" "$test_dir/native.jsonl" run.started,step.started,step.completed,run.finished 0
expect_status 1 "$binary" -r "$test_dir/native.yaml" -o "$test_dir/plain"
# A rejected provider bundle reaches the same validation status as a native run.
sed '/- {id: hold/d' "$test_dir/recipe.yaml" > "$test_dir/acceptance.yaml"
expect_status 3 "$binary" -r "$test_dir/acceptance.yaml" -o "$test_dir/acceptance"
# Observe both attempts, the retry reason, and the terminal failure.
export LS_TEST_FAIL=1
sed 's/retries: 0/retries: 1/' "$test_dir/recipe.yaml" > "$test_dir/retry.yaml"
expect_status 4 "$binary" -r "$test_dir/retry.yaml" -o "$test_dir/retry" --events-file "$test_dir/retry.jsonl"
"$test_dir/check" "$test_dir/retry.jsonl" run.started,step.started,step.progress,step.progress,step.progress,step.failed,run.finished 4
rg -q '"attempt":2' "$test_dir/retry.jsonl"
export LS_TEST_FAIL=0 LS_TEST_SLEEP=1
"$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/cancel" --timeout 0 --events 3 \
	3> "$test_dir/cancel.jsonl" > "$test_dir/cancel.out" 2> "$test_dir/cancel.err" &
run_pid=$!
for ((poll = 0; poll < 100; poll++))
do
	rg -q 'step.progress' "$test_dir/cancel.jsonl" && break
	sleep 0.1
done
kill -TERM "$run_pid"
run_status=0
wait "$run_pid" || run_status=$?
[[ $run_status == 5 ]] || failures=$((failures + 1))
"$test_dir/check" "$test_dir/cancel.jsonl" run.started,step.started,step.progress,step.failed,run.finished 5
export LS_TEST_SLEEP=0

[[ $failures == 0 ]]
printf '%s\n' 'PLATFORM HOOKS VERIFIED'
