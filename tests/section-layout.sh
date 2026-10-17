#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: section-layout.sh WEBDRIVER_URL OUTPUT_DIR' 'Render fixtures first with SECTION_LAYOUT_OUTPUT=OUTPUT_DIR build/release/test-section-layout.' 'Checks section headings and introductions at 390 and 1440; saves screenshots and exits nonzero on any failure.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# == 2 ]] || { echo 'Expected WebDriver URL and rendered fixture directory.' >&2; exit 2; }
webdriver=${1%/}
output=$(realpath "$2")
session=
profile=
call () {
	local result data=${3:-'{}'}
	result=$(curl --fail --silent --show-error --max-time 60 -X "$1" -H 'Content-Type: application/json' --data "$data" "$webdriver$2")
	if jq -e '.value.error? != null' <<< "$result" >/dev/null; then
		jq -r '.value.message' <<< "$result" >&2
		return 1
	fi
	printf '%s\n' "$result"
}
# shellcheck disable=SC2329 # Invoked by the EXIT trap.
cleanup () {
	if [[ -n $session ]]; then call DELETE "/session/$session" '{}' >/dev/null; fi
	if [[ -n $profile ]]; then rm -r -- "$profile"; fi
}
trap cleanup EXIT
profile=$(mktemp -d)
mkdir -p "$profile/chrome"
printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' > "$profile/user.js"
printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$profile/chrome/userChrome.css"
encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
session=$(call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId)
measure=$(cat <<'JS'
const rows = [...document.querySelectorAll(".features .section-head")].map(head => {
  const title = head.querySelector("h2"), intro = head.querySelector(".prose");
  return {titleWidth:title.getBoundingClientRect().width, introWidth:intro.getBoundingClientRect().width,
    lines:title.getBoundingClientRect().height/parseFloat(getComputedStyle(title).lineHeight),
    font:parseFloat(getComputedStyle(title).fontSize)};
});
return {width:innerWidth, rows, pageFits:document.documentElement.scrollWidth <= innerWidth + 1};
JS
)
failed=0
for width in 1440 390; do
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":1000}" >/dev/null
	for fixture in "$output"/*/index.html; do
		variant=$(basename "$(dirname "$fixture")")
		url="file://$fixture"
		if [[ -n ${SECTION_LAYOUT_URL:-} ]]; then url="${SECTION_LAYOUT_URL%/}/$variant/index.html"; fi
		call POST "/session/$session/url" "$(jq -nc --arg url "$url" '{url:$url}')" >/dev/null
		sleep 0.2
		result=$(call POST "/session/$session/execute/sync" "$(jq -nc --arg script "$measure" '{script:$script,args:[]}')" | jq -c .value)
		printf '%s %s %s\n' "$variant" "$width" "$result"
		if ! jq -e --argjson width "$width" '.width == $width and .pageFits and (.rows|length)==3 and all(.rows[]; .titleWidth >= 250 and .introWidth >= 250 and .lines <= 4.1 and .font >= 20)' <<< "$result" >/dev/null; then failed=1; fi
		if [[ $variant == rail-side ]]; then
			call GET "/session/$session/screenshot" '{}' | jq -er .value | base64 -d > "$output/$variant-$width.png"
		fi
	done
done
exit "$failed"
