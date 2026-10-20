#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: compositions.sh WEBDRIVER_URL OUTPUT_DIR' 'Render fixtures first with COMPOSITIONS_OUTPUT=OUTPUT_DIR build/release/test-compositions.' 'Opens every home composition at 390 and 1440, with and without site.js, and fails when a page' 'scrolls sideways or a heading splits a word across lines; saves a screenshot of each.'
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
printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
	'user_pref("ui.prefersReducedMotion", 1);' > "$profile/user.js"
printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$profile/chrome/userChrome.css"
encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
session=$(call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId)
# A word split across lines has more than one line box for its characters.
measure=$(cat <<'JS'
const split = [];
for (const heading of document.querySelectorAll("main h1, main h2")) {
  const walker = document.createTreeWalker(heading, NodeFilter.SHOW_TEXT);
  let node;
  while ((node = walker.nextNode())) {
    for (const match of node.textContent.matchAll(/\S+/g)) {
      const range = document.createRange();
      range.setStart(node, match.index);
      range.setEnd(node, match.index + match[0].length);
      const tops = new Set([...range.getClientRects()].filter(r => r.width > 0).map(r => Math.round(r.top)));
      if (tops.size > 1) split.push(match[0]);
    }
  }
}
return {width: innerWidth, pageFits: document.documentElement.scrollWidth <= innerWidth + 1, split};
JS
)
failed=0
for width in 390 1440; do
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":1000}" >/dev/null
	for fixture in "$output"/*/index.html; do
		name=$(basename "$(dirname "$fixture")")
		# Without site.js nothing shrinks a heading that is too large, so the
		# styles alone must keep every word whole.
		bare=$(dirname "$fixture")/index-noscript.html
		grep -v '<script src="site.js"' "$fixture" > "$bare"
		for page in "$fixture" "$bare"; do
			call POST "/session/$session/url" "$(jq -nc --arg url "file://$page" '{url:$url}')" >/dev/null
			sleep 0.3
			result=$(call POST "/session/$session/execute/sync" "$(jq -nc --arg script "$measure" '{script:$script,args:[]}')" | jq -c .value)
			printf '%s %s %s %s\n' "$name" "$width" "$(basename "$page" .html)" "$result"
			if ! jq -e --argjson width "$width" '.width == $width and .pageFits and (.split | length) == 0' <<< "$result" >/dev/null; then failed=1; fi
		done
		rm -- "$bare"
		call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
		sleep 0.3
		call GET "/session/$session/moz/screenshot/full" '{}' | jq -er .value | base64 -d > "$output/$name-$width.png"
	done
done
exit "$failed"
