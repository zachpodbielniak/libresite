#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: brand-layout.sh WEBDRIVER_URL OUTPUT_DIR' 'Render fixtures first with BRAND_LAYOUT_OUTPUT=OUTPUT_DIR build/release/test-brand-layout.' 'Checks all header/frame combinations at 390 and 1440; saves screenshots and exits nonzero on any failure.'
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
const brand = document.querySelector('.site-header .brand');
const walker = document.createTreeWalker(brand, NodeFilter.SHOW_TEXT);
let node; const broken = [];
while ((node = walker.nextNode())) {
  for (const match of node.textContent.matchAll(/\S+/g)) {
    const lines = new Set();
    for (let i = match.index; i < match.index + match[0].length; i++) {
      const range = document.createRange(); range.setStart(node, i); range.setEnd(node, i + 1);
      lines.add(Math.round(range.getBoundingClientRect().top));
    }
    if (lines.size > 1) broken.push(match[0]);
  }
}
const box = brand.getBoundingClientRect();
return {width: innerWidth, name: brand.textContent.trim(), broken,
  font: parseFloat(getComputedStyle(brand).fontSize),
  fits: box.left >= 0 && box.right <= innerWidth + 1 && brand.scrollWidth <= brand.clientWidth + 1,
  pageFits: document.documentElement.scrollWidth <= innerWidth + 1};
JS
)
failed=0
for width in 1440 390; do
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":1000}" >/dev/null
	for fixture in "$output"/*/index.html; do
		variant=$(basename "$(dirname "$fixture")")
		call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
		sleep 0.2
		result=$(call POST "/session/$session/execute/sync" "$(jq -nc --arg script "$measure" '{script:$script,args:[]}')" | jq -c .value)
		printf '%s %s %s\n' "$variant" "$width" "$result"
		if ! jq -e --argjson width "$width" '.width == $width and .broken == [] and .fits and .pageFits and .font >= 16 and .name == "Example\u0027s Plumbing & Heating"' <<< "$result" >/dev/null; then failed=1; fi
		if [[ $variant == rail-stacked || $variant == split-stacked ]]; then
			call GET "/session/$session/screenshot" '{}' | jq -er .value | base64 -d > "$output/$variant-$width.png"
		fi
	done
done
exit "$failed"
