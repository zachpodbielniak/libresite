#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# The site gate (docs/site-gate.org): opens a published site in headless
# Firefox at 390 and 1440 px and fails it on what a visitor sees first.
set -euo pipefail

usage() {
	cat <<'USAGE'
Usage: scripts/site-gate.sh [--json FILE] [--webdriver URL] SITE-DIR...

Serve each published SITE-DIR on 127.0.0.1, open every page in headless
Firefox at 390x844 and 1440x900, and check what a visitor sees:

  photo     a photo (not a drawing) on the home page's first screen
  headline  the H1 is on the first screen, not under a fixed bar,
            and at least 28 px on a phone
  action    exactly one primary (filled) action on the first screen
  bar       no fixed bar over 12 percent of the phone's screen height
  copy      no banned filler or demonstration copy
  empty     no section without its content
  sideways  no page scrolls sideways
  clipped   no text cut off by its box
  contrast  text reaches WCAG AA contrast

Prints PASS or FAIL per site and one line per failed check. Exits 0 when
every site passes, 1 when any check fails, 2 on a usage or browser error.
Starts its own geckodriver unless --webdriver or WEBDRIVER_URL names one.

Examples:
  scripts/site-gate.sh ./my-site
  scripts/site-gate.sh --json gate.json sites/*/

Options:
  --json FILE      Also write every finding as JSON to FILE
  --webdriver URL  Use a running WebDriver server, such as geckodriver
  -h, --help       Show this help
  --license        Print the license notice
USAGE
}

json='' webdriver=${WEBDRIVER_URL:-}
sites=()
while [[ $# -gt 0 ]]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		--license) echo "libresite site-gate.sh is licensed AGPL-3.0-or-later."; exit 0 ;;
		--json) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; json=$2; shift 2 ;;
		--webdriver) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; webdriver=$2; shift 2 ;;
		--) shift; sites+=("$@"); break ;;
		-*) echo "site-gate.sh: unknown option $1" >&2; usage >&2; exit 2 ;;
		*) sites+=("$1"); shift ;;
	esac
done
[[ ${#sites[@]} -gt 0 ]] || { usage >&2; exit 2; }
for site in "${sites[@]}"; do
	[[ -f $site/index.html ]] || { echo "site-gate.sh: $site has no index.html" >&2; exit 2; }
done
tools=(curl jq python3 zip)
[[ -n $webdriver ]] || tools+=(firefox geckodriver)
for tool in "${tools[@]}"; do
	command -v "$tool" > /dev/null || { echo "site-gate.sh: $tool is required" >&2; exit 2; }
done
probe=$(cat "$(dirname "$0")/site-gate.js")

work=$(mktemp -d)
session='' driver='' server=''
call() {
	local result data=${3:-'{}'}
	result=$(curl --fail --silent --show-error --max-time 60 -X "$1" -H 'Content-Type: application/json' --data "$data" "$webdriver$2") || return 2
	if jq -e '.value.error? != null' <<< "$result" > /dev/null; then
		echo "site-gate.sh: webdriver: $(jq -r '.value.message' <<< "$result")" >&2
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
# Waits up to ten seconds for a local server to answer.
wait_for() {
	local _
	for _ in $(seq 50); do
		curl --silent --max-time 1 --output /dev/null "$1" && return 0
		sleep 0.2
	done
	echo "site-gate.sh: $1 did not answer" >&2
	exit 2
}

if [[ -z $webdriver ]]; then
	port=$(free_port)
	geckodriver --host 127.0.0.1 --port "$port" > "$work/geckodriver.log" 2>&1 &
	driver=$!
	webdriver=http://127.0.0.1:$port
	wait_for "$webdriver/status"
fi
webdriver=${webdriver%/}
# Reduced motion, so entrances do not hide text; eager images, so the first
# screen is complete when measured; no minimum window width for 390.
mkdir -p "$work/profile/chrome"
printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
	'user_pref("ui.prefersReducedMotion", 1);' 'user_pref("dom.image-lazy-loading.enabled", false);' \
	'user_pref("layout.css.prefers-color-scheme.content-override", 1);' > "$work/profile/user.js"
printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$work/profile/chrome/userChrome.css"
encoded=$(cd "$work/profile" && zip -qr - . | base64 -w0)
session=$(call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId) || exit 2

# Sizes the window so the page itself, not the window, is WIDTH x HEIGHT.
size_window() {
	local inner
	call POST "/session/$session/window/rect" "{\"width\":$1,\"height\":$2}" > /dev/null || exit 2
	inner=$(call POST "/session/$session/execute/sync" '{"script":"return [innerWidth, innerHeight]","args":[]}' | jq -r '.value | join(" ")') || exit 2
	read -r iw ih <<< "$inner"
	call POST "/session/$session/window/rect" "{\"width\":$(($1 * 2 - iw)),\"height\":$(($2 * 2 - ih))}" > /dev/null || exit 2
}

# Check order for the report.
order='["photo","headline","action","bar","copy","empty","sideways","clipped","contrast"]'
failed=0
: > "$work/results.ndjson"
for site in "${sites[@]}"; do
	dir=$(realpath "$site")
	name=$(basename "$dir")
	started=$SECONDS
	port=$(free_port)
	python3 -I -m http.server "$port" --bind 127.0.0.1 --directory "$dir" > /dev/null 2>&1 &
	server=$!
	wait_for "http://127.0.0.1:$port/index.html"
	mapfile -t pages < <(cd "$dir" && find . -mindepth 1 -name '.*' -prune -o -name '*.html' -print | sed 's|^\./||' | sort | awk '$0 == "index.html" { print; next } { rest[++n] = $0 } END { for (i = 1; i <= n; i++) print rest[i] }')
	: > "$work/findings.ndjson"
	for size in 390x844 1440x900; do
		width=${size%x*} height=${size#*x}
		size_window "$width" "$height"
		for page in "${pages[@]}"; do
			call POST "/session/$session/url" "$(jq -nc --arg url "http://127.0.0.1:$port/$page" '{url:$url}')" > /dev/null || exit 2
			result=$(call POST "/session/$session/execute/async" "$(jq -nc --arg script "$probe" --argjson home "$([[ $page == index.html ]] && echo true || echo false)" '{script:$script,args:[{home:$home}]}')") || exit 2
			got=$(jq -r '"\(.value.width)x\(.value.height)"' <<< "$result")
			[[ $got == "$size" ]] || { echo "site-gate.sh: the page is $got, not $size" >&2; exit 2; }
			jq -c --arg page "$page" --argjson width "$width" '.value.findings[] | {check, detail, page: $page, width: $width}' <<< "$result" >> "$work/findings.ndjson"
		done
	done
	kill "$server" 2> /dev/null || true
	wait "$server" 2> /dev/null || true
	server=
	jq -sc --arg site "$name" --arg path "$dir" --argjson pages "${#pages[@]}" --argjson seconds "$((SECONDS - started))" --argjson order "$order" '
		{site: $site, path: $path, pages: $pages, seconds: $seconds, findings: .,
		 failed: ([.[].check] | unique | sort_by(. as $c | $order | index($c)))}' "$work/findings.ndjson" > "$work/site.json"
	cat "$work/site.json" >> "$work/results.ndjson"
	# One line per failed check: where, and what, merging the widths that
	# found the same thing.
	if jq -e '.failed | length == 0' "$work/site.json" > /dev/null; then
		jq -r '"PASS \(.site) (\(.pages) pages at 390 and 1440, \(.seconds) s)"' "$work/site.json"
	else
		failed=1
		jq -r '"FAIL \(.site)", (.findings as $all | .failed[] as $check |
			[$all[] | select(.check == $check)] | group_by([.page, .detail]) |
			map("\(.[0].page) at \([.[].width | tostring] | unique | sort_by(tonumber) | join(" and ")): \(.[0].detail)") as $where |
			"  \($check): \($where[:3] | join("; "))\(if ($where | length) > 3 then "; and \(($where | length) - 3) more" else "" end)")' "$work/site.json"
	fi
done
if [[ -n $json ]]; then jq -s . "$work/results.ndjson" > "$json"; fi
exit "$failed"
