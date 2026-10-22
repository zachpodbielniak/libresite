#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: showcase.sh WEBDRIVER_URL SITES_DIR' \
		'Check supplied photo colour at 390, 768 and 1440; explicit mono/duotone themes retain their filters;' \
		'the home headline sits on the first screen of a 900 px tall window.' \
		'Example: SHOWCASE_OUTPUT=build/showcase build/release/test-showcase' \
		'         bash tests/showcase.sh http://localhost:4444 build/showcase' \
		'Writes photo-colour.json. Exits 0 on success, 1 on a regression, 2 on invalid input.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# == 2 ]] || { echo 'Expected WebDriver URL and rendered photo sites.' >&2; exit 2; }
webdriver=${1%/}
output=$(realpath "$2")
session=
profile=$(mktemp -d)
call () {
	local result
	result=$(curl --fail --silent --show-error --max-time 60 -X "$1" -H 'Content-Type: application/json' --data "${3:-{\}}" "$webdriver$2")
	if jq -e '.value.error? != null' <<< "$result" >/dev/null; then
		jq -r '.value.message' <<< "$result" >&2
		return 1
	fi
	printf '%s\n' "$result"
}
# shellcheck disable=SC2329 # EXIT trap.
cleanup () {
	if [[ -n $session ]]; then call DELETE "/session/$session" >/dev/null || true; fi
	rm -r -- "$profile"
}
trap cleanup EXIT
mkdir -p "$profile/chrome"
printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
	'user_pref("ui.prefersReducedMotion", 1);' > "$profile/user.js"
printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$profile/chrome/userChrome.css"
encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
session=$(call POST /session "$(jq -nc --arg p "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$p}}}}')" | jq -er .value.sessionId)
measure=$(cat <<'JS'
const done=arguments[0];
(async()=>{
  if(document.readyState!=="complete")await new Promise(r=>addEventListener("load",r,{once:true}));
  await Promise.all([...document.images].map(i=>i.decode().catch(()=>{})));
  await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
  const explicit=document.body.matches(".look-photos-mono,.look-photos-duotone");
  const photos=[...document.querySelectorAll('main img[src*="assets/photos/"]')].map(i=>({src:i.getAttribute("src"),filter:getComputedStyle(i).filter,blend:getComputedStyle(i).mixBlendMode,loaded:i.complete&&i.naturalWidth>0}));
  const parse=c=>c.match(/[\d.]+/g).map(Number);
  const lum=c=>c.slice(0,3).map(v=>{v/=255;return v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4;}).reduce((s,v,i)=>s+v*[0.2126,0.7152,0.0722][i],0);
  const labels=[...document.querySelectorAll('.cover .cover__text .kicker')].map(e=>{
    const s=getComputedStyle(e),fg=parse(s.color),bg=parse(s.backgroundColor),a=lum(fg),b=lum(bg);
    return {background:s.backgroundColor,contrast:(Math.max(a,b)+0.05)/(Math.min(a,b)+0.05),opaque:bg.length<4||bg[3]>=0.99};
  });
  /* The opening's headline sits on the first screen; where its primary
   * action lands is recorded. */
  const title=document.querySelector("main h1"), opening=title&&title.closest("main > *");
  const action=opening&&[...opening.querySelectorAll(".actions a.button")].find(a=>a.getBoundingClientRect().height>0);
  const fold={screen:innerHeight,title:title?Math.round(title.getBoundingClientRect().bottom+scrollY):null,action:action?Math.round(action.getBoundingClientRect().bottom+scrollY):null};
  fold.fits=fold.title!==null&&fold.title<=innerHeight;
  done({width:innerWidth,explicit,photos,labels,fold,labelFailures:labels.filter(l=>!l.opaque||l.contrast<4.45),failures:photos.filter(i=>!i.loaded || (explicit ? !i.filter.includes("grayscale") : i.filter!=="none" || i.blend!=="normal"))});
})()
JS
)
results=()
failed=0
shopt -s nullglob
fixtures=("$output"/*/index.html)
[[ ${#fixtures[@]} -gt 0 ]] || { echo 'No rendered photo sites.' >&2; exit 2; }
for width in 390 768 1440; do
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":900}" >/dev/null
	for fixture in "${fixtures[@]}"; do
		call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
		result=$(call POST "/session/$session/execute/async" "$(jq -nc --arg s "$measure" '{script:$s,args:[]}')" | jq -c --arg site "$(basename "$(dirname "$fixture")")" '.value + {site:$site}')
		results+=("$result")
		printf '%s\n' "$result"
		jq -e --argjson w "$width" '.width == $w and (.photos|length)>0 and (.failures|length)==0 and (.labelFailures|length)==0 and .fold.fits' <<< "$result" >/dev/null || failed=1
	done
done
printf '%s\n' "${results[@]}" | jq -s . > "$output/photo-colour.json"
exit "$failed"
