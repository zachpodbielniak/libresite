#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Light and dark, and the form controls, in a real browser
# (docs/components.org, "Light and dark" and "Controls"). Composes the
# controls fixture site with the design test, serves it on 127.0.0.1 and
# checks in headless Firefox at 390 and 1440 px:
#   - the scheme toggle shows, cycles system, light, dark and back, keeps
#     the choice in localStorage, and a reload starts in the chosen scheme;
#   - every form control has a label, a 44 px target and a visible focus
#     ring, and its border reads on the page in both schemes.
# Needs firefox and geckodriver, or WEBDRIVER_URL.
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: tests/controls.sh [TEST-BINARY]' \
		'Composes the controls site with TEST-BINARY (default build/release/test-ls-design)' \
		'and checks the scheme toggle and every form control in headless Firefox.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
binary=${1:-$root/build/release/test-ls-design}
work=$(mktemp -d)
session='' driver='' server='' webdriver=${WEBDRIVER_URL:-}
fail() { printf 'controls test: %s\n' "$*" >&2; exit 1; }
call() {
	local result data=${3:-'{}'}
	result=$(curl --fail --silent --show-error --max-time 60 -X "$1" -H 'Content-Type: application/json' --data "$data" "$webdriver$2") || return 2
	if jq -e '.value.error? != null' <<< "$result" > /dev/null; then
		printf 'controls test: webdriver: %s\n' "$(jq -r '.value.message' <<< "$result")" >&2
		return 2
	fi
	printf '%s\n' "$result"
}
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
	if [[ -n $session ]]; then call DELETE "/session/$session" > /dev/null 2>&1 || true; fi
	if [[ -n $server ]]; then kill "$server" 2> /dev/null || true; fi
	if [[ -n $driver ]]; then kill "$driver" 2> /dev/null || true; fi
	rm -rf -- "$work"
}
trap cleanup EXIT
free_port() {
	python3 -I -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'
}
wait_for() {
	local _
	for _ in $(seq 50); do
		curl --silent --max-time 1 --output /dev/null "$1" && return 0
		sleep 0.2
	done
	fail "$1 did not answer"
}
# Runs the probe of tests/controls.js named $1 in the open page.
run() {
	call POST "/session/$session/execute/async" "$(jq -nc --arg script "$probe" --arg name "$1" '{script:$script,args:[$name]}')" | jq -c .value
}
open_page() {
	call POST "/session/$session/url" "$(jq -nc --arg url "http://127.0.0.1:$port/$1" '{url:$url}')" > /dev/null
}

(cd "$root" && LS_CONTROLS_OUTPUT="$work/site" "$binary" -p /design/controls) | grep -q '^ok' ||
	fail 'the controls site was not composed'
[[ -f $work/site/contact.html ]] || fail 'the controls site has no contact page'

if [[ -z $webdriver ]]; then
	gport=$(free_port)
	geckodriver --host 127.0.0.1 --port "$gport" > "$work/geckodriver.log" 2>&1 &
	driver=$!
	webdriver=http://127.0.0.1:$gport
	wait_for "$webdriver/status"
fi
webdriver=${webdriver%/}
# The system asks for light; reduced motion; no minimum window width.
mkdir -p "$work/profile/chrome"
printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
	'user_pref("ui.prefersReducedMotion", 1);' 'user_pref("layout.css.prefers-color-scheme.content-override", 1);' \
	> "$work/profile/user.js"
printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$work/profile/chrome/userChrome.css"
encoded=$(cd "$work/profile" && zip -qr - . | base64 -w0)
session=$(call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId)
port=$(free_port)
python3 -I -m http.server "$port" --bind 127.0.0.1 --directory "$work/site" > /dev/null 2>&1 &
server=$!
wait_for "http://127.0.0.1:$port/index.html"

probe=$(cat "$root/tests/controls.js")

failed=0
for size in 390x844 1440x900; do
	width=${size%x*} height=${size#*x}
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":$height}" > /dev/null
	open_page index.html
	run clear > /dev/null
	open_page index.html
	result=$(run toggle)
	printf '%s toggle: %s\n' "$size" "$(jq -c .states <<< "$result")"
	jq -r '.problems[]' <<< "$result" | sed "s/^/  $size: /"
	jq -e '.problems | length == 0' <<< "$result" > /dev/null || failed=1
	jq -e '[.states[] | .[0:3]] == [["system", null, "light"], ["light", "light", "light"], ["dark", "dark", "dark"], ["system", null, "light"]]' <<< "$result" > /dev/null ||
		{ printf '  %s: the toggle did not cycle system, light, dark, system\n' "$size"; failed=1; }
	jq -e '[.states[] | .[3]] | unique | length == 3' <<< "$result" > /dev/null ||
		{ printf '  %s: the toggle does not name each state\n' "$size"; failed=1; }
	open_page index.html
	result=$(run reload)
	printf '%s reload: %s\n' "$size" "$result"
	jq -e '.scheme == "dark" and .stored == "dark" and .ground == "dark" and (.labels | unique | length == 1)' <<< "$result" > /dev/null ||
		{ printf '  %s: a reload did not keep the chosen dark scheme\n' "$size"; failed=1; }
	run clear > /dev/null
	open_page contact.html
	result=$(run controls)
	printf '%s controls: %s checked\n' "$size" "$(jq .count <<< "$result")"
	jq -r '.problems[]' <<< "$result" | sed "s/^/  $size: /"
	jq -e '.problems | length == 0 and .count >= 20' <<< "$result" > /dev/null || failed=1
done
[[ $failed == 0 ]] || fail 'see above'
printf '%s\n' 'CONTROLS VERIFIED'
