#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: gate.sh WEBDRIVER_URL SITES_DIR' 'Render the sites first with SAMENESS_OUTPUT=SITES_DIR build/release/test-sameness (make gate does it all).' 'Opens every home page at 390 and 1440: text must reach WCAG AA contrast in the light and the dark' 'scheme, no text may be in outline, nothing may be tilted, no decorative circles or neon bands, nothing may' 'animate with reduced motion, no page may scroll sideways, and each home page must stay within the' 'page-weight budget. Writes gate.json and exits nonzero on any failure.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# == 2 ]] || { echo 'Expected WebDriver URL and rendered site directory.' >&2; exit 2; }
webdriver=${1%/}
output=$(realpath "$2")
# The page-weight budget for a home page without its remote web fonts: the
# HTML, styles, script and local pictures it loads, raw and gzipped.
weight_limit=153600
gzip_limit=40960
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
	if [[ -n $session ]]; then call DELETE "/session/$session" '{}' >/dev/null || true; fi
	if [[ -n $profile ]]; then rm -r -- "$profile"; fi
}
trap cleanup EXIT
# One browser per pass (geckodriver drives one at a time): reduced motion
# with the light scheme, then the dark scheme.
start () {
	local encoded
	profile=$(mktemp -d)
	mkdir -p "$profile/chrome"
	printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
		'user_pref("ui.prefersReducedMotion", 1);' "user_pref(\"layout.css.prefers-color-scheme.content-override\", $1);" > "$profile/user.js"
	printf '%s\n' '#main-window, #navigator-toolbox { min-width: 0 !important; }' > "$profile/chrome/userChrome.css"
	encoded=$(cd "$profile" && zip -qr - . | base64 -w0)
	session=$(call POST /session "$(jq -nc --arg profile "$encoded" '{capabilities:{alwaysMatch:{browserName:"firefox","moz:firefoxOptions":{args:["-headless"],profile:$profile}}}}')" | jq -er .value.sessionId)
}
stop () {
	call DELETE "/session/$session" '{}' >/dev/null
	session=
	rm -r -- "$profile"
	profile=
}
# Every element with its own text: its colour against the first opaque
# background behind it. Text over a picture or gradient is skipped and
# counted; large text (24px, or 18.66px bold) needs 3:1, the rest 4.5:1.
measure=$(cat <<'JS'
const done = arguments[arguments.length - 1];
/* rgb() channels run to 255; mixed colours come back as color(srgb) in 0 to 1. */
const parse = c => { const m = c.match(/[\d.]+/g); if (!m) return null; const v = m.map(Number);
  return c.startsWith("color(") ? v.map((x, i) => i < 3 ? x * 255 : x) : v; };
const lum = ([r, g, b]) => [r, g, b].map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; })
  .reduce((s, v, i) => s + v * [0.2126, 0.7152, 0.0722][i], 0);
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
(async () => {
  for (let y = 0; y <= document.documentElement.scrollHeight; y += innerHeight / 2) { scrollTo(0, y); await new Promise(r => setTimeout(r, 60)); }
  scrollTo(0, 0);
  await new Promise(r => setTimeout(r, 300));
  const low = [], stroked = []; let checked = 0, skipped = 0;
  for (const el of document.querySelectorAll("body *")) {
    if (![...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim())) continue;
    const style = getComputedStyle(el);
    if (style.visibility === "hidden" || style.display === "none" || el.closest("[aria-hidden='true'], .visually-hidden, dialog:not([open])")) continue;
    const box = el.getBoundingClientRect();
    if (box.width === 0 || box.height === 0) continue;
    /* Stroke-only text reads only when it is short and the screen wide,
     * whatever is behind it. */
    const ink = parse(style.color);
    if (parseFloat(style.webkitTextStrokeWidth || "0") > 0) {
      /* Taste: no outline lettering at all, short or long. */
      stroked.push(`${el.tagName.toLowerCase()} "${el.textContent.trim().slice(0, 30)}"`);
      skipped++; continue;
    }
    let bg = null, over = false;
    for (let node = el; node; node = node.parentElement) {
      const s = getComputedStyle(node);
      if (s.backgroundImage !== "none" && node !== document.documentElement) { over = true; break; }
      const c = parse(s.backgroundColor);
      if (c && (c.length < 4 || c[3] > 0.9)) { bg = c; break; }
    }
    if (over) { skipped++; continue; }
    bg = bg || [255, 255, 255];
    const fg = parse(style.color), size = parseFloat(style.fontSize), bold = parseInt(style.fontWeight, 10) >= 700;
    if (fg.length > 3 && fg[3] < 0.9) { skipped++; continue; }
    const need = size >= 24 || (size >= 18.66 && bold) ? 3 : 4.5, got = ratio(fg, bg);
    checked++;
    if (got < need - 0.05) low.push(`${el.tagName.toLowerCase()} "${el.textContent.trim().slice(0, 30)}" ${got.toFixed(2)}`);
  }
  /* Taste (docs/components.org, "Taste"): no tilted boxes, no decorative
   * circles or blobs, no neon bands. */
  const rotated = [], blobs = [], loud = [];
  const label = el => `${el.tagName.toLowerCase()}.${[...el.classList].slice(0, 2).join(".")}`;
  const tilt = s => {
    let angle = 0;
    const m = s.transform.match(/^matrix\(([^)]+)\)/);
    if (m) { const [a, b] = m[1].split(",").map(Number); angle = Math.atan2(b, a) * 180 / Math.PI; }
    if (s.rotate && s.rotate !== "none") angle += parseFloat(s.rotate) || 0;
    angle = Math.abs(angle) % 90;
    return angle > 0.3 && angle < 89.7;
  };
  const round = (s, w, h) => {
    const r = s.borderTopLeftRadius;
    const px = r.endsWith("%") ? parseFloat(r) / 100 * Math.min(w, h) : parseFloat(r);
    return px >= Math.min(w, h) / 2 - 1;
  };
  for (const el of document.querySelectorAll("body *")) {
    const s = getComputedStyle(el);
    if (s.display === "none" || s.visibility === "hidden") continue;
    const box = el.getBoundingClientRect();
    if (box.width >= 48 && box.height >= 48 && tilt(s)) rotated.push(label(el));
    /* A big round shape with nothing in it is decoration. */
    if (box.width >= 48 && box.height >= 48 && round(s, box.width, box.height) && !el.textContent.trim() &&
      !el.querySelector("img, svg, video, picture") && !["IMG", "SVG", "VIDEO", "PICTURE", "svg"].includes(el.tagName) &&
      (parse(s.backgroundColor)?.[3] ?? 1) > 0.05 && s.backgroundColor !== "rgba(0, 0, 0, 0)") blobs.push(label(el));
    for (const pseudo of ["::before", "::after"]) {
      const p = getComputedStyle(el, pseudo);
      const w = parseFloat(p.width), h = parseFloat(p.height);
      if (p.content === "none" || p.display === "none" || !(w >= 48 && h >= 48)) continue;
      if (round(p, w, h) && (p.backgroundColor !== "rgba(0, 0, 0, 0)" || p.borderTopWidth !== "0px")) blobs.push(label(el) + pseudo);
      if (tilt(p)) rotated.push(label(el) + pseudo);
    }
    /* Neon: a large band in a vivid, light colour. */
    if (box.width >= innerWidth / 2 && box.height >= 120) {
      const c = parse(s.backgroundColor);
      if (c && (c.length < 4 || c[3] > 0.9)) {
        const hi = Math.max(c[0], c[1], c[2]), lo = Math.min(c[0], c[1], c[2]);
        if ((hi - lo) / 255 > 0.35 && hi / 255 > 0.6) loud.push(`${label(el)} ${s.backgroundColor}`);
      }
    }
  }
  done({width: innerWidth, rotated: rotated.slice(0, 5), blobs: blobs.slice(0, 5), loud: loud.slice(0, 5), pageFits: document.documentElement.scrollWidth <= innerWidth + 1,
    /* The base reduced-motion rule shortens every transition to 0.01ms rather
     * than removing it, so a layout change can start one nobody can see; any
     * CSS animation, or anything lasting 1ms or more, counts. */
    animations: document.getAnimations().filter(a => a instanceof CSSAnimation ||
      Number(a.effect.getComputedTiming().duration) >= 1).length,
    checked, skipped, low: low.slice(0, 5), lowCount: low.length, stroked});
})();
JS
)
failed=0
results=()
for scheme in 1 0; do
	name=$([[ $scheme == 1 ]] && echo light || echo dark)
	start "$scheme"
	for width in 390 1440; do
		call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":900}" >/dev/null
		for fixture in "$output"/*/index.html; do
			site=$(basename "$(dirname "$fixture")")
			call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
			result=$(call POST "/session/$session/execute/async" "$(jq -nc --arg script "$measure" '{script:$script,args:[]}')" | jq -c --arg site "$site" --arg scheme "$name" '.value + {site:$site, scheme:$scheme}')
			printf '%s\n' "$result"
			results+=("$result")
			jq -e --argjson width "$width" '.width == $width and .pageFits and .animations == 0 and .lowCount == 0 and (.stroked | length) == 0 and (.rotated | length) == 0 and (.blobs | length) == 0 and (.loud | length) == 0 and .checked > 20' <<< "$result" >/dev/null || failed=1
		done
	done
	stop
done
# Page weight, from the files a home page loads (remote fonts excluded).
weights=$(
	for fixture in "$output"/*/index.html; do
		dir=$(dirname "$fixture")
		art=0
		while read -r src; do
			[[ -f $dir/$src ]] && art=$(( art + $(wc -c < "$dir/$src") ))
		done < <(grep -o 'src="assets/[^"]*"' "$fixture" | sed 's/^src="//; s/"$//' | sort -u)
		raw=$(( $(wc -c < "$fixture") + $(wc -c < "$dir/styles.css") + $(wc -c < "$dir/site.js") + art ))
		zipped=$(cat "$fixture" "$dir/styles.css" "$dir/site.js" | gzip -9 | wc -c)
		jq -nc --arg site "$(basename "$dir")" --argjson raw "$raw" --argjson zipped "$zipped" '{site:$site, raw:$raw, gzip:$zipped}'
	done | jq -sc .
)
printf '%s\n' "${results[@]}" | jq -s --argjson weights "$weights" --argjson raw "$weight_limit" --argjson gz "$gzip_limit" \
	'{pages: ., weights: $weights, weight_limit: $raw, gzip_limit: $gz}' > "$output/gate.json"
jq -r '"weight: largest home page \([.weights[].raw] | max) bytes (limit \(.weight_limit)), \([.weights[].gzip] | max) gzipped (limit \(.gzip_limit)); contrast: \([.pages[].checked] | add) text elements checked, \([.pages[].lowCount] | add) below AA, \([.pages[].skipped] | add) over pictures or in outline skipped, \([.pages[].stroked[]] | length) in outline; taste: \([.pages[].rotated[]] | length) tilted, \([.pages[].blobs[]] | length) decorative circles, \([.pages[].loud[]] | length) neon bands; animations with reduced motion: \([.pages[].animations] | add); pages scrolling sideways: \([.pages[] | select(.pageFits | not)] | length)"' "$output/gate.json"
jq -e '[.weights[] | select(.raw > $raw or .gzip > $gz)] | length == 0' --argjson raw "$weight_limit" --argjson gz "$gzip_limit" "$output/gate.json" >/dev/null || failed=1
exit "$failed"
