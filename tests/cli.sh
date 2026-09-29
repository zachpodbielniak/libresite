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
# Seed contents must be visible from outside the checkout, and count toward
# prompt limits during preflight before a workspace or provider can be used.
cat > "$test_dir/seeds.yaml" <<'YAML'
version: 1
name: seeded-preview
site: {brief: Use the supplied artifacts}
seed_artifacts:
  - {path: content/client.org, content: 'Approved client copy'}
  - {path: assets/logo.svg, content: '<svg/>'}
  - {path: brand.yaml, content: 'color: blue'}
  - {path: legal/terms.txt, content: 'Approved legal text'}
pipeline:
  steps:
    - {id: first, type: generate}
YAML
(cd /tmp && "$binary" -r "$test_dir/seeds.yaml" --preview first) > "$test_dir/seeds-preview"
for text in 'Approved client copy' '<svg/>' 'color: blue' 'Approved legal text'
do
	rg -Fq "$text" "$test_dir/seeds-preview"
done
sed '/^seed_artifacts:/,/^pipeline:/{ /^pipeline:/!d; }' "$test_dir/seeds.yaml" > "$test_dir/unseeded.yaml"
"$binary" -r "$test_dir/unseeded.yaml" --preview first > "$test_dir/unseeded-preview"
seed_limit=$(wc -c < "$test_dir/unseeded-preview")
"$binary" -r "$test_dir/unseeded.yaml" --validate --max-prompt-bytes "$seed_limit"
if "$binary" -r "$test_dir/seeds.yaml" -o "$test_dir/seed-output" \
	--max-prompt-bytes "$seed_limit" > "$test_dir/seed-limit" 2>&1
then
	exit 1
fi
[[ ! -e "$test_dir/seed-output.libresite-work" ]]
"$binary" --help > "$test_dir/help"
rg -q 'Examples:' "$test_dir/help"
rg -q -- '--debug' "$test_dir/help"
rg -q -- '--timeout' "$test_dir/help"
for deadline in -2 86401 invalid
do
	if "$binary" -r "$root/recipes/application.yaml" --validate --timeout "$deadline" > /dev/null 2>&1
	then
		exit 1
	fi
done
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
		if [[ "$module" == plan || "$module" == pages ]]
		then
			# Component steps carry their own compact contract and the catalog.
			"$binary" --recipe "$recipe" --preview "$step" > "$test_dir/preview"
			rg -q 'RESPONSE PROTOCOL' "$test_dir/preview"
			rg -q 'TRUTHFULNESS' "$test_dir/preview"
			rg -q 'COMPONENTS' "$test_dir/preview"
			rg -q 'ACCESSIBILITY' "$test_dir/preview"
		elif [[ "$module" != validate && "$module" != scaffold && "$module" != gate && "$module" != prune ]]
		then
			"$binary" --recipe "$recipe" --preview "$step" > "$test_dir/preview"
			rg -q 'CURRENT PROJECT' "$test_dir/preview"
			# Every recipe/stage inherits the shared device contract, even
			# when its brief does not explicitly request responsive output.
			rg -q 'MOBILE AND DESKTOP DEFAULTS' "$test_dir/preview"
			# Accessibility is shared by every stage, including backend HTML
			# and custom generate stages, without an opt-in recipe field.
			rg -q 'ACCESSIBILITY AND ARIA DEFAULTS' "$test_dir/preview"
			rg -q 'Native artifact validation does not certify ARIA compliance' "$test_dir/preview"
			rg -q 'SEO DEFAULTS' "$test_dir/preview"
			rg -q 'DELIVERABLE BOUNDARY' "$test_dir/preview"
			rg -q 'robots.txt' "$test_dir/preview"
		fi
	done < "$test_dir/plan"
done
"$binary" --recipe "$root/recipes/landing.yaml" --preview plan > "$test_dir/preview"
rg -q 'DESIGN DIRECTION' "$test_dir/preview"
# One switch moves every AI step to another agent without editing the recipe.
"$binary" -r "$root/recipes/mixed-providers.yaml" --plan --provider claude-code > "$test_dir/agent-plan"
[[ $(rg -c 'claude-code' "$test_dir/agent-plan") -eq 5 ]]
if rg -q 'codex|cursor|grok-build|antigravity' "$test_dir/agent-plan"
then
	exit 1
fi
"$binary" -r "$root/recipes/studio.yaml" --plan --provider codex --model gpt-test > "$test_dir/agent-plan"
rg -q $'^1\tplan\tplan\tcodex\tgpt-test$' "$test_dir/agent-plan"
for bad in bogus-provider model-only empty-model
do
	case $bad in
		bogus-provider) flags=(--provider bogus) ;;
		model-only) flags=(--model gpt-test) ;;
		empty-model) flags=(--provider codex --model '') ;;
	esac
	if "$binary" -r "$root/recipes/studio.yaml" --plan "${flags[@]}" > /dev/null 2> "$test_dir/error"
	then
		exit 1
	fi
	rg -q 'provider|model' "$test_dir/error"
done
if "$binary" --qa "$root" --provider codex > /dev/null 2>&1
then
	exit 1
fi
"$binary" --recipe "$root/recipes/application.yaml" --preview backend > "$test_dir/app-backend"
rg -q 'localhost' "$test_dir/app-backend"
rg -q 'AGENTS.md' "$test_dir/app-backend"
rg -q 'intentionally rejected' "$test_dir/app-backend"
rg -q 'AGENTS.md' "$root/recipes/application.yaml"
rg -q 'localhost' "$root/recipes/application.yaml"
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
export LS_TEST_CWD="$test_dir/cwd"
for provider in codex claude-code cursor grok-build antigravity
do
	export LS_TEST_PROVIDER="$provider" LS_TEST_REQUIRE_SCHEMA=1
	export LS_TEST_SCHEMA="$test_dir/$provider-schema.json"
	cat > "$test_dir/recipe.yaml" <<YAML
version: 1
name: adapter-test
site: {brief: Build a tiny test site}
ai: {provider: $provider, model: fixture-model, retries: 0, timeout: 2}
pipeline:
  steps:
    - {id: site, type: frontend}
    - id: verify
      type: validate
      options:
        required: [index.html]
        checks: [{path: index.html, contains: ['Offline site'], excludes: ['TODO'], min_bytes: 16}]
YAML
	"$binary" -r "$test_dir/recipe.yaml" -o "$test_dir/$provider"
	rg -q 'Offline site' "$test_dir/$provider/index.html"
	test -f "$test_dir/$provider/.libresite/run.json"
	test -f "$test_dir/$provider/.gitignore"
	test "$(git -C "$test_dir/$provider" log -1 --format=%s)" = 'chore: initial generated project'
	test -z "$(git -C "$test_dir/$provider" status --porcelain)"
	rg -q 'fixture-model' "$test_dir/args"
	# Unattended adapters must skip approval prompts; otherwise Codex exec
	# waits forever for sandbox-escalation on a non-TTY stdin.
	case "$provider" in
		codex)
			# Codex inlines system instructions in stdin; other adapters
			# may carry them in argv or temporary prompt files instead.
			rg -q 'MOBILE AND DESKTOP DEFAULTS' "$test_dir/input"
			rg -q 'ACCESSIBILITY AND ARIA DEFAULTS' "$test_dir/input"
			rg -q 'DELIVERABLE BOUNDARY' "$test_dir/input"
			rg -q 'SEO DEFAULTS' "$test_dir/input"
			rg -q 'sitemap.xml' "$test_dir/input"
			rg -q 'noindex' "$test_dir/input"
			rg -q '360x800 and 390x844' "$test_dir/input"
			rg -q '1366x768 laptop and 1440x900 desktop' "$test_dir/input"
			rg -qx -- '--ephemeral' "$test_dir/args"
			rg -qx -- '--dangerously-bypass-approvals-and-sandbox' "$test_dir/args"
			if rg -qx -- '--sandbox' "$test_dir/args"
			then
				exit 1
			fi
			;;
		claude-code)
			# Answer-only: the schema tool is the only tool; nothing to bypass.
			cmp "$root/data/file-bundle.schema.json" "$LS_TEST_SCHEMA"
			rg -qx -- '--safe-mode' "$test_dir/args"
			rg -qx -- '--strict-mcp-config' "$test_dir/args"
			rg -x -A1 -- '--tools' "$test_dir/args" | rg -qx 'StructuredOutput'
			if rg -qx -- '--dangerously-skip-permissions' "$test_dir/args"
			then
				exit 1
			fi
			;;
		antigravity)
			cmp "$root/data/file-bundle.schema.json" "$LS_TEST_SCHEMA"
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
	if [[ $provider != claude-code && $provider != antigravity ]]
	then
		test ! -e "$LS_TEST_SCHEMA"
	fi
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
"$binary" -r "$test_dir/failure.yaml" -o "$test_dir/failure" --resume --debug --timeout 10 --max-prompt-bytes 262144 > "$test_dir/debug.out" 2> "$test_dir/debug.log"
rg -q 'attempt 1/1; provider codex; timeout 10s' "$test_dir/debug.log"
rg -q 'AI SETTINGS' "$test_dir/input"
rg -q '"timeout":10' "$test_dir/input"
rg -q '"max_tokens":16384' "$test_dir/input"
# Debug can be enabled when resuming a non-debug checkpoint. Provider-native
# persistence is enabled without changing the recipe or selecting resume argv.
if rg -qx -- '--ephemeral' "$test_dir/args"
then
	exit 1
fi
rg -q 'persistence=enabled cwd=' "$test_dir/debug.log"
rg -q 'system_bytes=[0-9]+ prompt_bytes=[0-9]+' "$test_dir/debug.log"
rg -q 'elapsed_ms=[0-9]+ result=response session=fixture-session' "$test_dir/debug.log"
rg -q 'response_bytes=[0-9]+ stop_reason=' "$test_dir/debug.log"
if rg -q 'CURRENT PROJECT|Offline site|debug:' "$test_dir/debug.out" || rg -q 'CURRENT PROJECT|Offline site' "$test_dir/debug.log"
then
	exit 1
fi
# The fixture creates no files; debug deliberately retains its private cwd.
debug_cwd=$(< "$test_dir/cwd")
test -d "$debug_cwd"
rmdir -- "$debug_cwd"
# ai-glib enforces process deadlines; cancellation must not masquerade as success.
export LS_TEST_SLEEP=1
if "$binary" --debug -r "$test_dir/failure.yaml" -o "$test_dir/timeout" > "$test_dir/timeout.log" 2>&1
then
	exit 1
fi
test ! -e "$test_dir/timeout"
rg -q 'result=failed session=.*error_domain=ai-glib-error-quark error_code=' "$test_dir/timeout.log"
debug_cwd=$(< "$test_dir/cwd")
rmdir -- "$debug_cwd"
# A longer execution-only deadline allows a previously timed-out checkpoint
# to finish, without editing its recipe or weakening output validation.
export LS_TEST_SLEEP_SECONDS=3
"$binary" --unlimited --timeout 1 -r "$test_dir/failure.yaml" -o "$test_dir/timeout" --resume > "$test_dir/recovered.log" 2>&1
test -f "$test_dir/timeout/index.html"
rg -q '"timeout":"unlimited"' "$test_dir/input"
rg -q '"max_tokens":"unlimited; provider limits apply"' "$test_dir/input"
export LS_TEST_SLEEP_SECONDS=5
rg -q 'attempt 1/1; provider codex; timeout 2s' "$test_dir/timeout.log"
# A deadline gets exactly the configured retry, with the reason visible.
sed 's/retries: 0/retries: 1/' "$test_dir/failure.yaml" > "$test_dir/retry.yaml"
if "$binary" -r "$test_dir/retry.yaml" -o "$test_dir/retry" > "$test_dir/retry.log" 2>&1
then
	exit 1
fi
rg -q 'attempt 1 failed: .*did not complete within 2000 ms.*; retrying' "$test_dir/retry.log"
rg -q 'attempt 2/2; provider codex; timeout 2s' "$test_dir/retry.log"
test "$(rg -c '; provider codex;' "$test_dir/retry.log")" -eq 2
test ! -e "$test_dir/retry"
# Send a real signal while the CLI's worker is waiting for its subprocess.
"$binary" --timeout 0 -r "$test_dir/failure.yaml" -o "$test_dir/cancel" > "$test_dir/cancel.log" 2>&1 &
run_pid=$!
sleep 0.2
kill -TERM "$run_pid"
run_status=0
wait "$run_pid" || run_status=$?
test "$run_status" -eq 5
test ! -e "$test_dir/cancel"
# Exercise the actual main-context heartbeat while the worker is blocked.
# The checkpoint must stay byte-identical during a wait and after cancellation.
sed 's/timeout: 2/timeout: 60/' "$test_dir/failure.yaml" > "$test_dir/wait.yaml"
export LS_TEST_SLEEP_SECONDS=60
"$binary" -r "$test_dir/wait.yaml" -o "$test_dir/wait" > "$test_dir/wait.log" 2>&1 &
run_pid=$!
for ((poll = 0; poll < 100; poll++))
do
	[[ -f "$test_dir/wait.libresite-work/checkpoint.json" ]] && break
	sleep 0.1
done
cp "$test_dir/wait.libresite-work/checkpoint.json" "$test_dir/checkpoint-before"
for ((poll = 0; poll < 350; poll++))
do
	rg -q 'still waiting' "$test_dir/wait.log" && break
	sleep 0.1
done
kill -TERM "$run_pid"
run_status=0
wait "$run_pid" || run_status=$?
test "$run_status" -eq 5
rg -q 'still waiting .*attempt 1/1; provider codex; timeout 60s; accepted artifacts unchanged' "$test_dir/wait.log"
# Cancellation changes accounting, while accepted files and next step stay put.
for checkpoint in "$test_dir/checkpoint-before" "$test_dir/wait.libresite-work/checkpoint.json"
do
	rg -q '"files":\[\]' "$checkpoint"
	rg -q '"next":0[,}]' "$checkpoint"
done
rg -q '"usage_complete":true' "$test_dir/wait.libresite-work/checkpoint.json"
rg -q '"success":false' "$test_dir/wait.libresite-work/checkpoint.json"
rg -q '"attempts":1[,}]' "$test_dir/wait.libresite-work/checkpoint.json"
test ! -e "$test_dir/wait"
export LS_TEST_SLEEP=0
printf '%s\n' 'CLI VERIFIED (five subprocess adapters, failures, resume, deadline, cancellation)'
# Platform submissions emit only YAML, with no account or output directory.
cat > "$test_dir/form.json" <<'JSON'
{"name":"form-site","site":{"pages":[{"path":"/","title":"Home"}],"offers":[],"contact":{},"brand":{"name":"Studio"},"copy_lock":["null","A & B"],"languages":{"default":"en","supported":["en"]}}}
JSON
"$binary" --from-form "$test_dir/form.json" > "$test_dir/form.yaml"
"$binary" -r "$test_dir/form.yaml" --validate
"$binary" -r "$test_dir/form.yaml" --emit-yaml > "$test_dir/form-again.yaml"
cmp "$test_dir/form.yaml" "$test_dir/form-again.yaml"
"$binary" -r "$test_dir/form.yaml" --preview frontend > "$test_dir/form-prompt"
rg -q 'STRUCTURED SITE CONTRACT' "$test_dir/form-prompt"
rg -q 'copy_lock' "$test_dir/form-prompt"
if "$binary" --from-form "$test_dir/form.json" -o "$test_dir/unwanted" > "$test_dir/rejected.yaml" 2>/dev/null
then
	exit 1
fi
test ! -s "$test_dir/rejected.yaml"
test ! -e "$test_dir/unwanted"
printf '%s\n' 'STRUCTURED FORM VERIFIED'

bash "$root/tests/platform-hooks.sh" "$binary"
