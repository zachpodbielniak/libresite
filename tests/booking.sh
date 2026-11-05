#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# The booking page in a real browser (docs/components.org, "Book online").
# Composes the preset booking site with the design test and checks it in
# headless Firefox:
#   - with a small fake booking host (tests/fixtures/booking.py), the
#     calendar shows; a booked time the host has just given away (409)
#     asks for another, and the next time books (201);
#   - with the host stopped, an empty list of times, or no scripts, the
#     same page shows the booking request form instead;
#   - the request form's controls have labels, 44 px targets and error
#     text tied by aria-describedby, in light and dark, at 390 and 1440.
# Needs firefox and geckodriver, or WEBDRIVER_URL.
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: tests/booking.sh [TEST-BINARY]' \
		'Composes the booking site with TEST-BINARY (default build/release/test-ls-design)' \
		'and checks the calendar and the booking request form in headless Firefox.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
binary=${1:-$root/build/release/test-ls-design}
work=$(mktemp -d)
sessions=() driver='' server='' webdriver=${WEBDRIVER_URL:-}
fail() { printf 'booking test: %s\n' "$*" >&2; exit 1; }
call() {
	local result data=${3:-'{}'}
	result=$(curl --fail-with-body --silent --show-error --max-time 60 -X "$1" -H 'Content-Type: application/json' --data "$data" "$webdriver$2") || {
		printf 'booking test: webdriver: %s\n' "$result" >&2
		return 2
	}
	if jq -e '.value.error? != null' <<< "$result" > /dev/null; then
		printf 'booking test: webdriver: %s\n' "$(jq -r '.value.message' <<< "$result")" >&2
		return 2
	fi
	printf '%s\n' "$result"
}
stop_server() {
	if [[ -n $server ]]; then
		kill "$server" 2> /dev/null || true
		wait "$server" 2> /dev/null || true
		server=''
	fi
}
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup() {
	local id
	for id in "${sessions[@]}"; do call DELETE "/session/$id" > /dev/null 2>&1 || true; done
	stop_server
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
# A headless Firefox session whose profile asks for light; $1 = 0 turns
# scripts off. Prints the session id.
open_session() {
	local profile=$work/profile-$1 encoded
	mkdir -p "$profile"
	printf '%s\n' 'user_pref("ui.prefersReducedMotion", 1);' 'user_pref("layout.css.prefers-color-scheme.content-override", 1);' \
		> "$profile/user.js"
	if [[ $1 == 0 ]]; then printf '%s\n' 'user_pref("javascript.enabled", false);' >> "$profile/user.js"; fi
	encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
	call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId
}
# Serves the site; "host" answers the booking route, "empty" answers no
# times, "static" is a plain file server with no booking route.
serve() {
	stop_server
	port=$(free_port)
	case $1 in
	static) python3 -I -m http.server "$port" --bind 127.0.0.1 --directory "$work/site" > /dev/null 2>&1 & ;;
	empty) python3 -I "$root/tests/fixtures/booking.py" "$work/site" "$port" "$work/posts" --empty & ;;
	*) python3 -I "$root/tests/fixtures/booking.py" "$work/site" "$port" "$work/posts" & ;;
	esac
	server=$!
	wait_for "http://127.0.0.1:$port/index.html"
}
open_page() {
	call POST "/session/$session/url" "$(jq -nc --arg url "http://127.0.0.1:$port/$1" '{url:$url}')" > /dev/null
}
run() {
	call POST "/session/$session/execute/async" "$(jq -nc --arg script "$probe" --arg name "$1" '{script:$script,args:[$name]}')" | jq -c .value
}
# Whether the element at CSS selector $1 is displayed, with no page script:
# the WebDriver commands work with scripts turned off.
displayed() {
	local element
	element=$(call POST "/session/$session/element" "$(jq -nc --arg css "$1" '{using:"css selector",value:$css}')" |
		jq -er '.value | to_entries[0].value') || { printf 'missing'; return; }
	call GET "/session/$session/element/$element/displayed" | jq -r .value
}

(cd "$root" && LS_BOOKING_OUTPUT="$work/site" "$binary" -p /design/booking-request) > "$work/compose.log" 2>&1 ||
	{ cat "$work/compose.log"; fail 'the booking site was not composed'; }
[[ -f $work/site/book.html ]] || fail 'the booking site has no booking page'

if [[ -z $webdriver ]]; then
	gport=$(free_port)
	geckodriver --host 127.0.0.1 --port "$gport" > "$work/geckodriver.log" 2>&1 &
	driver=$!
	webdriver=http://127.0.0.1:$gport
	wait_for "$webdriver/status"
fi
webdriver=${webdriver%/}
probe=$(cat "$root/tests/booking.js")
failed=0
session=$(open_session 1)
sessions+=("$session")

# The host answers: the calendar books, 409 then 201.
serve host
call POST "/session/$session/window/rect" '{"width":390,"height":844}' > /dev/null
open_page book.html
result=$(run calendar)
printf 'host up: %s\n' "$result"
jq -e '.app and (.request | not)' <<< "$result" > /dev/null || { printf '  the calendar did not replace the request form\n'; failed=1; }
jq -e '.taken | test("just taken")' <<< "$result" > /dev/null || { printf '  a 409 did not ask for another time\n'; failed=1; }
jq -e '.booked | test("confirmed")' <<< "$result" > /dev/null || { printf '  a 201 did not confirm the booking\n'; failed=1; }
[[ -f $work/posts && $(wc -l < "$work/posts") -eq 2 ]] || { printf '  the host did not receive two booking requests\n'; failed=1; }

# The host stopped, and a host with no times: the request form.
for mode in static empty; do
	serve "$mode"
	for size in 390x844 1440x900; do
		width=${size%x*} height=${size#*x}
		call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":$height}" > /dev/null
		open_page book.html
		result=$(run request)
		printf '%s %s: %s\n' "$mode" "$size" "$(jq -c 'del(.problems)' <<< "$result")"
		jq -r '.problems[]' <<< "$result" | sed "s/^/  $mode $size: /"
		jq -e '.request and (.app | not) and (.problems | length == 0) and .count >= 8' <<< "$result" > /dev/null || failed=1
	done
done

# The probe itself: with the request form's targets shrunk it must say
# so, or a pass above proves nothing.
printf '%s\n' '.booking-calendar__request .choice, .booking-calendar__request .field input { min-height: 0 !important; height: 1rem !important; padding: 0 !important; }' \
	'.booking-calendar__request .field--check > label { min-height: 0 !important; padding: 0 !important; }' >> "$work/site/styles.css"
open_page book.html
result=$(run request)
jq -e '[.problems[] | select(test("the target is"))] | length > 0' <<< "$result" > /dev/null || fail 'the probe missed shrunken targets'
printf 'broken styles: %s problems found, as expected\n' "$(jq '.problems | length' <<< "$result")"

# No scripts at all: the same page shows the request form. geckodriver
# holds one session at a time.
serve static
call DELETE "/session/$session" > /dev/null
sessions=()
session=$(open_session 0)
sessions+=("$session")
open_page book.html
request=$(displayed '[data-booking-request] form')
app=$(displayed '.booking-calendar__app')
printf 'no scripts: request form displayed %s, calendar displayed %s\n' "$request" "$app"
[[ $request == true && $app == false ]] || failed=1

[[ $failed == 0 ]] || fail 'see above'
printf '%s\n' 'BOOKING VERIFIED'
