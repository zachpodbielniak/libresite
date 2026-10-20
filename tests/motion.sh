#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: motion.sh WEBDRIVER_URL OUTPUT_DIR' 'Render fixtures first with MOTION_OUTPUT=OUTPUT_DIR build/release/test-motion.' 'Opens every motion kit at 390 and 1440 twice: with reduced motion nothing may animate and the' 'marquee band is gone; without it something animates, figures end on their supplied text and no' 'page scrolls sideways. Saves a screenshot of each.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# == 2 ]] || { echo 'Expected WebDriver URL and rendered fixture directory.' >&2; exit 2; }
webdriver=${1%/}
output=$(realpath "$2")
sessions=()
profiles=()
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
	local s p
	for s in "${sessions[@]}"; do call DELETE "/session/$s" '{}' >/dev/null || true; done
	for p in "${profiles[@]}"; do rm -r -- "$p"; done
}
trap cleanup EXIT
# One browser per preference: 1 asks for reduced motion, 0 does not.
start () {
	local profile encoded
	profile=$(mktemp -d)
	profiles+=("$profile")
	mkdir -p "$profile/chrome"
	printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
		"user_pref(\"ui.prefersReducedMotion\", $1);" > "$profile/user.js"
	printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$profile/chrome/userChrome.css"
	encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
	call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId
}
# Scroll through the page so scroll-driven and in-view motion has its chance,
# then report what is animating, whether the band shows and the figures' text.
measure=$(cat <<'JS'
const done = arguments[arguments.length - 1];
const step = () => new Promise(r => setTimeout(r, 120));
(async () => {
  for (let y = 0; y <= document.documentElement.scrollHeight; y += innerHeight / 2) { scrollTo(0, y); await step(); }
  await new Promise(r => setTimeout(r, 1500));
  const band = document.querySelector(".kit-marquee");
  done({width: innerWidth, pageFits: document.documentElement.scrollWidth <= innerWidth + 1,
    animations: document.getAnimations().length,
    band: band ? getComputedStyle(band).display !== "none" : null,
    figures: [...document.querySelectorAll(".numbers__value")].map(e => e.textContent)});
})();
JS
)
failed=0
# geckodriver drives one browser at a time: one pass per preference.
for mode in reduced moving; do
	session=$(start "$([[ $mode == reduced ]] && echo 1 || echo 0)")
	sessions=("$session")
	for width in 390 1440; do
		call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":900}" >/dev/null
		for fixture in "$output"/*/index.html; do
			name=$(basename "$(dirname "$fixture")")
			call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
			result=$(call POST "/session/$session/execute/async" "$(jq -nc --arg script "$measure" '{script:$script,args:[]}')" | jq -c .value)
			printf '%s %s %s %s\n' "$name" "$width" "$mode" "$result"
			if [[ $mode == reduced ]]; then
				jq -e --argjson width "$width" '.width == $width and .pageFits and .animations == 0 and .band != true and .figures == ["1,240","18"]' <<< "$result" >/dev/null || failed=1
			else
				jq -e --argjson width "$width" '.width == $width and .pageFits and .animations > 0 and .figures == ["1,240","18"]' <<< "$result" >/dev/null || failed=1
				call POST "/session/$session/execute/sync" '{"script":"scrollTo(0, 0)","args":[]}' >/dev/null
				call GET "/session/$session/moz/screenshot/full" '{}' | jq -er .value | base64 -d > "$output/$name-$width.png"
			fi
		done
	done
	call DELETE "/session/$session" '{}' >/dev/null
	sessions=()
done
exit "$failed"
