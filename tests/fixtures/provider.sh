#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Controlled protocol fixture. Never invokes an actual provider or network.
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Protocol test fixture. Example: provider.sh'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
printf '%s\n' "$@" > "${LS_TEST_ARGS:?}"
previous=
for argument in "$@"
do
	if [[ $previous == --json-schema && -n ${LS_TEST_SCHEMA:-} ]]
	then
		printf '%s' "$argument" > "$LS_TEST_SCHEMA"
	fi
	previous=$argument
done
if [[ -n ${LS_TEST_CWD:-} ]]
then
	pwd > "$LS_TEST_CWD"
fi
# Drain stdin for every adapter. Some ai-glib versions pass prompts in argv,
# others pipe them; exiting early races long writes and causes EPIPE.
cat > "${LS_TEST_INPUT:?}"
if [[ ${LS_TEST_FAIL:-0} == 1 ]]
then
	printf '%s\n' 'fixture failure' >&2
	exit 7
fi
if [[ ${LS_TEST_SLEEP:-0} == 1 ]]
then
	sleep "${LS_TEST_SLEEP_SECONDS:-5}"
fi
# Escape the file bundle for a JSON string, without requiring another language.
bundle='{"files":[{"path":"index.html","content":"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>Fixture</title></head><main><h1>Offline site</h1></main></html>"}]}'
bundle=${LS_TEST_BUNDLE:-$bundle}
if [[ ${LS_TEST_REQUIRE_SCHEMA:-0} == 1 && ( $LS_TEST_PROVIDER == claude-code || $LS_TEST_PROVIDER == antigravity ) && ! -s ${LS_TEST_SCHEMA:?} ]]
then
	bundle="Prepared your files. $bundle"
fi
escaped=${bundle//\\/\\\\}
escaped=${escaped//\"/\\\"}
if [[ ${LS_TEST_PROVIDER} == codex ]]
then
	printf '%s\n' '{"type":"thread.started","thread_id":"fixture-session"}'
	printf '{"type":"item.completed","item":{"id":"a","type":"agent_message","text":"%s"}}\n' "$escaped"
	printf '%s\n' '{"type":"turn.completed","usage":{"input_tokens":1,"output_tokens":1}}'
else
	printf '{"type":"result","subtype":"success","is_error":false,"status":"SUCCESS","result":"%s","text":"%s","response":"%s"}\n' "$escaped" "$escaped" "$escaped"
fi
