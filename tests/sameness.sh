#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: sameness.sh WEBDRIVER_URL OUTPUT_DIR' 'Render the ten sites first with SAMENESS_OUTPUT=OUTPUT_DIR build/release/test-sameness.' 'Screenshots every home page at 390 and 1440, scores how alike each pair looks and combines that' 'with score.json; writes screens.json and exits nonzero when the sites are over the limits.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# == 2 ]] || { echo 'Expected WebDriver URL and rendered site directory.' >&2; exit 2; }
command -v magick >/dev/null || { echo 'ImageMagick (magick) is required.' >&2; exit 2; }
webdriver=${1%/}
output=$(realpath "$2")
[[ -f $output/score.json ]] || { echo "No score.json in $output; render the sites first." >&2; exit 2; }
# Same limits as the structural gate in tests/test-sameness.c, plus one for
# the screenshots alone: ten home pages whose shapes are this alike or more
# on average share one layout in other words.
mean_limit=$(jq -r .mean_limit "$output/score.json")
screen_limit=0.2
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
# Reduced motion stops entrance animations, so every page is captured at
# rest; the motion look itself is scored by the structural test.
printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
	'user_pref("ui.prefersReducedMotion", 1);' > "$profile/user.js"
printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$profile/chrome/userChrome.css"
encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
session=$(call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId)
settle='return document.fonts.ready.then(() => document.documentElement.scrollHeight);'
mapfile -t sites < <(find "$output" -mindepth 2 -maxdepth 2 -name index.html -printf '%h\n' | sort)
(( ${#sites[@]} >= 2 )) || { echo 'Need at least two rendered sites.' >&2; exit 2; }
shots=$output/screens
mkdir -p "$shots"
for width in 390 1440; do
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":1000}" >/dev/null
	for site in "${sites[@]}"; do
		slug=$(basename "$site")
		call POST "/session/$session/url" "$(jq -nc --arg url "file://$site/index.html" '{url:$url}')" >/dev/null
		call POST "/session/$session/execute/sync" "$(jq -nc --arg script "$settle" '{script:$script,args:[]}')" >/dev/null
		# site.js fits large headings once fonts are in; let it finish.
		sleep 0.3
		call GET "/session/$session/moz/screenshot/full" '{}' | jq -er .value | base64 -d > "$shots/$slug-$width.png"
		# The page's shape: grey levels squashed to a fixed 64 by 256 grid, so
		# pages of different lengths compare section by section.
		magick "$shots/$slug-$width.png" -colorspace Gray -resize '64x256!' "$shots/$slug-$width-shape.png"
	done
done
# Structural similarity (SSIM) of every pair's shapes, clamped to 0..1: 1
# for the same layout. ImageMagick reports SSIM in some versions and the
# distance 1 - SSIM in others, so a shape compared with itself tells which.
ssim () {
	magick compare -metric SSIM "$1" "$2" null: 2>&1 >/dev/null |
		awk '{v = $1; if (match($0, /\([^)]*\)/)) v = substr($0, RSTART + 1, RLENGTH - 2); print v}' || true
}
first=$(basename "${sites[0]}")
distance=$(awk -v self="$(ssim "$shots/$first-390-shape.png" "$shots/$first-390-shape.png")" 'BEGIN {print (self < 0.5) ? 1 : 0}')
pairs=$(
	for width in 390 1440; do
		for (( i = 0; i < ${#sites[@]}; i++ )); do
			for (( j = i + 1; j < ${#sites[@]}; j++ )); do
				a=$(basename "${sites[i]}")
				b=$(basename "${sites[j]}")
				r=$(awk -v v="$(ssim "$shots/$a-$width-shape.png" "$shots/$b-$width-shape.png")" -v distance="$distance" \
					'BEGIN {printf "%.6f\n", distance ? 1 - v : v}')
				jq -nc --arg a "$a" --arg b "$b" --argjson width "$width" --argjson r "$r" \
					'{a:$a, b:$b, width:$width, screen:([0, ([1, $r] | min)] | max)}'
			done
		done
	done | jq -sc .
)
# Each pair's overall sameness: the structural score from the test, 85
# percent, and how alike the two home pages look at both widths, 15 percent.
jq -n --argjson pairs "$pairs" --slurpfile score "$output/score.json" --argjson limit "$mean_limit" \
	--argjson screen_limit "$screen_limit" '
	($score[0].pairs) as $structural
	| [ $structural[] | . as $p
		| ([ $pairs[] | select((.a == $p.a and .b == $p.b) or (.a == $p.b and .b == $p.a)) | .screen ] | add / length) as $screen
		| {a, b, structural: .score, screen: $screen, score: (0.85 * .score + 0.15 * $screen)} ] as $combined
	| {
		structural: $score[0].score,
		screen_390: ([ $pairs[] | select(.width == 390) | .screen ] | add / length),
		screen_1440: ([ $pairs[] | select(.width == 1440) | .screen ] | add / length),
		score: ([ $combined[] | .score ] | add / length),
		worst: ([ $combined[] | .score ] | max),
		mean_limit: $limit, screen_limit: $screen_limit,
		pairs: $combined
	}' > "$output/screens.json"
jq -r '"sameness \(.score*1000|round/1000) (limit \(.mean_limit*100|round/100)), worst pair \(.worst*1000|round/1000); structural \(.structural*1000|round/1000); screens 390 \(.screen_390*1000|round/1000) 1440 \(.screen_1440*1000|round/1000) (limit \(.screen_limit))"' "$output/screens.json"
jq -e '.score <= .mean_limit and .screen_390 <= .screen_limit and .screen_1440 <= .screen_limit' "$output/screens.json" >/dev/null
