#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: mobile.sh WEBDRIVER_URL SITES_DIR...' 'Render the sites first, such as with OPTIONS_OUTPUT=DIR build/release/test-options and' 'MOBILE_OUTPUT=DIR build/release/test-mobile (make gate does it all).' 'Opens every page of the sites in the first directory and the home page of the rest, with motion' 'allowed, at 390 wide on a 2x screen and 1440 wide on a 1x screen, and fails on: text under 12px or squeezed below 16px, text below WCAG AA contrast (also' 'with every section band tone laid over each section), tap targets under 44px outside running' 'text, photos served with under 0.8 of the pixels their box needs, photos cropped to a strip of' 'under 40 percent, and web fonts that can swap in late. Writes mobile.json in the first directory' 'and exits nonzero on any failure.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# -ge 2 ]] || { echo 'Expected WebDriver URL and at least one rendered site directory.' >&2; exit 2; }
webdriver=${1%/}
shift
dirs=()
for dir in "$@"; do dirs+=("$(realpath "$dir")"); done
session=
profile=
call () {
	local result data=${3:-'{}'}
	result=$(curl --fail --silent --show-error --max-time 120 -X "$1" -H 'Content-Type: application/json' --data "$data" "$webdriver$2")
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
# One browser per screen density, motion allowed and scroll-driven
# animations on, as on a current phone or desktop browser.
start () {
	local encoded
	profile=$(mktemp -d)
	mkdir -p "$profile/chrome"
	printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
		'user_pref("ui.prefersReducedMotion", 0);' 'user_pref("layout.css.scroll-driven-animations.enabled", true);' \
		"user_pref(\"layout.css.devPixelsPerPx\", \"$1\");" > "$profile/user.js"
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
check=$(cat <<'JS'
const done = arguments[arguments.length - 1];
const issues = [];
const label = el => `${el.tagName.toLowerCase()}${el.className && typeof el.className === "string" ? "." + el.className.trim().split(/\s+/)[0] : ""} "${(el.textContent || el.getAttribute("alt") || el.getAttribute("aria-label") || "").trim().slice(0, 30)}"`;
const say = (what, el) => issues.push(`${what}: ${label(el)}`);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const hidden = el => el.closest("[aria-hidden='true'], .visually-hidden, dialog:not([open]), [hidden], .skip-link, noscript") ||
  (el.closest("details:not([open])") && !el.closest("summary"));
const shown = el => { const s = getComputedStyle(el), r = el.getBoundingClientRect(); return s.visibility !== "hidden" && s.display !== "none" && r.width > 1 && r.height > 1; };
const own = el => [...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim().length > 1);
/* rgb() channels run to 255; mixed colours come back as color(srgb) in 0 to 1. */
const parse = c => { const m = c.match(/[\d.]+/g); if (!m) return null; const v = m.map(Number);
  return c.startsWith("color(") ? v.map((x, i) => i < 3 ? x * 255 : x) : v; };
const lum = ([r, g, b]) => [r, g, b].map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; })
  .reduce((s, v, i) => s + v * [0.2126, 0.7152, 0.0722][i], 0);
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
/* Contrast of an element's own text against the first opaque background
 * behind it; null over a picture, a gradient or see-through ink. */
const contrast = el => {
  const style = getComputedStyle(el), fg = parse(style.color);
  if (!fg || (fg.length > 3 && fg[3] < 0.9) || parseFloat(style.webkitTextStrokeWidth || "0") > 0) return null;
  let bg = null;
  for (let node = el; node; node = node.parentElement) {
    const s = getComputedStyle(node);
    if (s.backgroundImage !== "none" && node !== document.documentElement) return null;
    const c = parse(s.backgroundColor);
    if (c && (c.length < 4 || c[3] > 0.9)) { bg = c; break; }
  }
  const size = parseFloat(style.fontSize), bold = parseInt(style.fontWeight, 10) >= 700;
  const need = size >= 24 || (size >= 18.66 && bold) ? 3 : 4.5, got = ratio(fg, bg || [255, 255, 255]);
  return got < need - 0.05 ? got : null;
};
(async () => {
  await document.fonts.ready;
  /* Walk down the page as a visitor would, letting reveals and lazy
   * pictures arrive, then back to the top. */
  for (let y = 0; y <= document.documentElement.scrollHeight; y += innerHeight / 2) { scrollTo(0, y); await sleep(80); }
  await Promise.all([...document.images].map(i => i.complete ? 0 : new Promise(r => { i.onload = i.onerror = r; setTimeout(r, 4000); })));
  scrollTo(0, 0);
  await sleep(800);
  const texts = [...document.querySelectorAll("body *")].filter(el => own(el) && !hidden(el) && shown(el));
  for (const el of texts) {
    const size = parseFloat(getComputedStyle(el).fontSize);
    if (size < 11.5) say(`text ${size.toFixed(1)}px`, el);
    /* The heading fitter may shrink a long word, never below 16px. */
    if (el.style.fontSize) {
      const fitted = el.style.fontSize;
      el.style.fontSize = "";
      const natural = parseFloat(getComputedStyle(el).fontSize);
      el.style.fontSize = fitted;
      if (size < Math.min(16, natural) - 0.5) say(`squeezed to ${size.toFixed(1)}px from ${natural.toFixed(1)}px`, el);
    }
  }
  /* The rest is measured at rest: a section still waiting to scroll in
   * keeps its entrance's first frame, scaled or washed. */
  const rest = document.createElement("style");
  rest.textContent = "*, *::before, *::after { animation: none !important; transition: none !important; }";
  document.head.append(rest);
  for (const el of texts) {
    const low = contrast(el);
    if (low !== null) say(`contrast ${low.toFixed(2)}`, el);
  }
  /* Any section may be dealt a band tone; each must keep its text legible. */
  for (const block of document.querySelectorAll("main > .block:not(:first-child)")) {
    if (block.classList.contains("sec-tone")) continue;
    const was = block.className;
    for (const tone of ["tint", "ink", "accent"]) {
      block.className = `${was} sec-tone sec-tone-${tone}`;
      for (const el of block.querySelectorAll("*")) {
        if (!own(el) || hidden(el) || !shown(el)) continue;
        const low = contrast(el);
        if (low !== null) say(`contrast ${low.toFixed(2)} on a ${tone} band`, el);
      }
    }
    block.className = was;
  }
  /* Tap targets, leaving out links inside running text. */
  for (const el of document.querySelectorAll("a[href], button, summary, input:not([type=hidden]), select, textarea")) {
    if (hidden(el) || !shown(el)) continue;
    const parent = el.parentElement, words = t => (t || "").trim().length;
    if (getComputedStyle(el).display === "inline" && parent && words(parent.textContent) > words(el.textContent) + 15) continue;
    /* A link stretched over its card by an absolute ::after is as big as the card. */
    let target = el;
    /* A checkbox or radio is toggled by its label too; the label is the target. */
    if (el.matches("input[type=checkbox], input[type=radio]") && el.labels && el.labels.length) target = el.labels[0];
    const after = getComputedStyle(el, "::after");
    if (after.content !== "none" && after.position === "absolute")
      for (let up = el.parentElement; up && up !== document.body; up = up.parentElement)
        if (getComputedStyle(up).position !== "static") { target = up; break; }
    const r = target.getBoundingClientRect();
    if (r.height < 43.5 || r.width < 43.5) say(`tap target ${Math.round(r.width)}x${Math.round(r.height)}`, el);
  }
  /* Photos: enough pixels for their box at this screen's density, and
   * at least 40 percent of the picture left after cropping. */
  for (const img of document.querySelectorAll("img")) {
    const src = img.currentSrc || img.src;
    if (/\.svg(\?|#|$)/.test(src) || !img.naturalWidth || hidden(img) || !shown(img)) continue;
    const s = getComputedStyle(img), r = img.getBoundingClientRect();
    const real = await new Promise(ok => { const t = new Image(); t.onload = () => ok([t.naturalWidth, t.naturalHeight]); t.onerror = () => ok(null); t.src = src; });
    if (!real) continue;
    const na = real[0] / real[1], ra = r.width / r.height, cover = s.objectFit === "cover";
    const kept = cover ? (na > ra ? ra / na : na / ra) : 1;
    if (kept < 0.4) say(`photo cropped to ${Math.round(kept * 100)}% (${real[0]}x${real[1]} in ${Math.round(r.width)}x${Math.round(r.height)})`, img);
    const need = (cover ? Math.max(r.width, r.height * na) : r.width) * devicePixelRatio;
    const largest = Math.max(real[0], ...(img.getAttribute("srcset") || "").split(",").map(c => parseInt(c.trim().split(/\s+/)[1], 10) || 0));
    if (real[0] / need < 0.8 && real[0] < largest) say(`soft photo ${real[0]}w for ${Math.round(need)} px`, img);
  }
  /* Web fonts must not swap in after the page is drawn: optional display,
   * each family's files fetched from the head. */
  for (const link of document.querySelectorAll("link[rel=stylesheet][href*='fonts.bunny.net']")) {
    const url = new URL(link.href);
    if (url.searchParams.get("display") !== "optional") issues.push(`web fonts display=${url.searchParams.get("display")}`);
    for (const family of (url.searchParams.get("family") || "").split("|").map(f => f.split(":")[0]))
      if (!document.querySelector(`link[rel=preload][as=font][crossorigin][href*='/${family}/files/']`)) issues.push(`web font ${family} not preloaded`);
  }
  done({width: innerWidth, dpr: devicePixelRatio, issues: [...new Set(issues)].slice(0, 12), count: issues.length});
})();
JS
)
failed=0
results=()
for pass in '390 2.0' '1440 1.0'; do
	read -r width density <<< "$pass"
	start "$density"
	call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":900}" >/dev/null
	for dir in "${dirs[@]}"; do
		# Every page of the first directory's sites (breadcrumbs and contents
		# live on inner pages); the home page of the rest.
		if [[ $dir == "${dirs[0]}" ]]; then pages=("$dir"/*/*.html); else pages=("$dir"/*/index.html); fi
		for fixture in "${pages[@]}"; do
			case ${fixture##*/} in 404.html|privacy.html|terms.html) continue ;; esac
			site=$(basename "$(dirname "$fixture")")/${fixture##*/}
			call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
			result=$(call POST "/session/$session/execute/async" "$(jq -nc --arg script "$check" '{script:$script,args:[]}')" |
				jq -c --arg site "$site" '.value + {site:$site}')
			results+=("$result")
			if ! jq -e --argjson width "$width" '.width == $width and .count == 0' <<< "$result" >/dev/null; then
				failed=1
				printf '%s\n' "$result"
			fi
		done
	done
	stop
done
printf '%s\n' "${results[@]}" | jq -s . > "${dirs[0]}/mobile.json"
jq -r '"\(length) page loads, \([.[] | select(.count > 0)] | length) with problems"' "${dirs[0]}/mobile.json"
exit "$failed"
