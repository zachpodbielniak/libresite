#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: options.sh WEBDRIVER_URL OPTIONS_DIR' 'Render the option sites first with OPTIONS_OUTPUT=OPTIONS_DIR build/release/test-options.' 'Opens every option site at 390, 768 and 1440 and fails on sideways scroll, clipped or overlapping' 'text, body text under 16px, tap targets under 44px, text over a picture without a solid backing,' 'and, on a phone with motion allowed, any animation of a property that moves layout. Writes' 'options.json and exits nonzero on any failure.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
[[ $# == 2 ]] || { echo 'Expected WebDriver URL and option site directory.' >&2; exit 2; }
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
	if [[ -n $session ]]; then call DELETE "/session/$session" '{}' >/dev/null || true; fi
	if [[ -n $profile ]]; then rm -r -- "$profile"; fi
}
trap cleanup EXIT
start () {
	local encoded
	profile=$(mktemp -d)
	mkdir -p "$profile/chrome"
	printf '%s\n' 'user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);' \
		"user_pref(\"ui.prefersReducedMotion\", $1);" > "$profile/user.js"
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
layout=$(cat <<'JS'
const done = arguments[arguments.length - 1];
const issues = [];
const say = (what, el) => issues.push(`${what}: ${el.tagName.toLowerCase()}${el.className && typeof el.className === "string" ? "." + el.className.trim().split(/\s+/)[0] : ""} "${(el.textContent || el.getAttribute("aria-label") || "").trim().slice(0, 30)}"`);
const hidden = el => el.closest("[aria-hidden='true'], .visually-hidden, dialog:not([open]), [hidden], .skip-link, .marquee, .kit-marquee, noscript") ||
  (el.closest("details:not([open])") && !el.closest("summary")) ||
  (el.getBoundingClientRect().width <= 1 && el.getBoundingClientRect().height <= 1);
const shown = el => { const s = getComputedStyle(el), r = el.getBoundingClientRect(); return s.visibility !== "hidden" && s.display !== "none" && r.width > 0 && r.height > 0; };
const own = el => [...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim());
const parse = c => { const m = c.match(/[\d.]+/g); return m ? m.map(Number) : null; };
const solid = el => { const c = parse(getComputedStyle(el).backgroundColor); return c && (c.length < 4 || c[3] >= 0.75); };
(async () => {
  await document.fonts.ready;
  await new Promise(r => setTimeout(r, 200));
  const root = document.documentElement;
  if (root.scrollWidth > innerWidth + 1) issues.push(`scrolls sideways: ${root.scrollWidth}px in ${innerWidth}px`);
  const texts = [...document.querySelectorAll("body *")].filter(el => own(el) && !hidden(el) && shown(el));
  const page = el => { const r = el.getBoundingClientRect(); return {l: r.left + scrollX, t: r.top + scrollY, r: r.right + scrollX, b: r.bottom + scrollY}; };
  for (const el of texts) {
    const box = page(el), style = getComputedStyle(el);
    /* Clipped: text running past an ancestor that hides overflow. */
    for (let up = el.parentElement; up && up !== document.body; up = up.parentElement) {
      const s = getComputedStyle(up);
      if (s.overflowX === "visible" && s.overflowY === "visible") continue;
      if (s.overflowX === "auto" || s.overflowX === "scroll") break;
      const clip = page(up);
      if (box.l < clip.l - 2 || box.r > clip.r + 2 || box.b > clip.b + 2) say("clipped", el);
      break;
    }
    if (el.scrollWidth > el.clientWidth + 2 && style.overflowX !== "visible" && style.overflowX !== "auto") say("overflows its box", el);
    /* Body text: a sentence or more in reading text. */
    if (el.matches("p, li, dd, td, blockquote, .prose, .lede") && el.textContent.trim().split(/\s+/).length >= 8 &&
      !el.closest(".kicker, figcaption, .site-footer, small, .muted, .page-aside") && parseFloat(style.fontSize) < 15.5)
      say(`body text ${parseFloat(style.fontSize)}px`, el);
  }
  /* Overlap between two pieces of text that are not nested; fixed and
   * sticky pieces sit over the page by design. */
  const pinned = el => { for (let up = el; up && up !== document.body; up = up.parentElement) {
    const p = getComputedStyle(up).position; if (p === "fixed" || p === "sticky") return true; } return false; };
  /* Inline text that wraps is compared line by line, not as one box. */
  const lines = el => [...el.getClientRects()].filter(r => r.width > 0).map(r => ({l: r.left + scrollX, t: r.top + scrollY, r: r.right + scrollX, b: r.bottom + scrollY}));
  const boxes = texts.filter(el => !pinned(el)).map(el => ({el, b: page(el), lines: lines(el)}));
  for (let i = 0; i < boxes.length; i++) for (let j = i + 1; j < boxes.length; j++) {
    const a = boxes[i], c = boxes[j];
    if (a.el.contains(c.el) || c.el.contains(a.el)) continue;
    if (Math.min(a.b.r, c.b.r) <= Math.max(a.b.l, c.b.l) || Math.min(a.b.b, c.b.b) <= Math.max(a.b.t, c.b.t)) continue;
    /* A real overlap covers a quarter of the smaller line, not the touching
     * line boxes of tight display type. */
    const hit = a.lines.some(x => c.lines.some(y => {
      const w = Math.min(x.r, y.r) - Math.max(x.l, y.l), h = Math.min(x.b, y.b) - Math.max(x.t, y.t);
      return w > 3 && h > Math.max(3, Math.min(x.b - x.t, y.b - y.t) / 4);
    }));
    if (hit) { say("overlaps " + c.el.textContent.trim().slice(0, 20), a.el); break; }
  }
  /* Tap targets. */
  for (const el of document.querySelectorAll("a.button, button, summary, input:not([type=hidden]), select, textarea, .site-nav a, .nav-toggle")) {
    if (hidden(el) || !shown(el)) continue;
    const r = el.getBoundingClientRect();
    if (r.height < 43.5 || r.width < 43.5) say(`tap target ${Math.round(r.width)}x${Math.round(r.height)}`, el);
  }
  /* Text over a picture needs a solid backing between them. */
  /* A picture faded to a wash behind text is part of the background. */
  const pictures = [...document.querySelectorAll("main img")].filter(img => shown(img) &&
    parseFloat(getComputedStyle(img).opacity) > 0.35).map(img => ({img, b: page(img)}));
  for (const el of texts) {
    if (!el.closest("main")) continue;
    const box = page(el);
    const under = pictures.find(p => !p.img.contains(el) && Math.min(box.r, p.b.r) - Math.max(box.l, p.b.l) > 4 &&
      Math.min(box.b, p.b.b) - Math.max(box.t, p.b.t) > 4);
    if (!under) continue;
    el.scrollIntoView({block: "center"});
    const r = el.getBoundingClientRect();
    const stack = document.elementsFromPoint(r.left + r.width / 2, r.top + r.height / 2);
    let backed = getComputedStyle(el).textShadow !== "none" || solid(el);
    for (const node of stack) {
      if (node === under.img) break;
      if (node !== el && (el.contains(node) || node.contains(el) || true) && solid(node) && node !== document.body && node !== root) { backed = true; break; }
    }
    if (!stack.includes(under.img)) backed = true;
    if (!backed) say("text over a picture", el);
  }
  scrollTo(0, 0);
  done({width: innerWidth, issues: [...new Set(issues)].slice(0, 12), count: issues.length});
})();
JS
)
# With motion allowed on a phone, only paint-only properties may animate.
motion=$(cat <<'JS'
const done = arguments[arguments.length - 1];
const paint = new Set(["transform", "opacity", "translate", "rotate", "scale", "filter", "clipPath", "maskPosition",
  "webkitMaskPosition", "maskPositionX", "maskPositionY", "webkitMaskPositionX", "webkitMaskPositionY", "backgroundPosition", "backgroundSize", "color", "backgroundColor", "boxShadow", "outlineOffset",
  "offset", "easing", "composite", "computedOffset"]);
(async () => {
  for (let y = 0; y <= document.documentElement.scrollHeight; y += innerHeight) { scrollTo(0, y); await new Promise(r => setTimeout(r, 80)); }
  const moving = new Set();
  for (const a of document.getAnimations()) {
    if (!a.effect || !a.effect.getKeyframes) continue;
    for (const frame of a.effect.getKeyframes()) for (const key of Object.keys(frame))
      if (!paint.has(key)) moving.add(`${a.animationName || a.transitionProperty || "animation"} animates ${key}`);
  }
  done({width: innerWidth, issues: [...moving].slice(0, 12), count: moving.size});
})();
JS
)
failed=0
results=()
check () {
	local mode=$1 script=$2 width site result
	shift 2
	for width in "$@"; do
		call POST "/session/$session/window/rect" "{\"width\":$width,\"height\":900}" >/dev/null
		for fixture in "$output"/*/index.html; do
			site=$(basename "$(dirname "$fixture")")
			call POST "/session/$session/url" "$(jq -nc --arg url "file://$fixture" '{url:$url}')" >/dev/null
			result=$(call POST "/session/$session/execute/async" "$(jq -nc --arg script "$script" '{script:$script,args:[]}')" |
				jq -c --arg site "$site" --arg mode "$mode" '.value + {site:$site, mode:$mode}')
			results+=("$result")
			if ! jq -e '.count == 0' <<< "$result" >/dev/null; then
				failed=1
				printf '%s\n' "$result"
			fi
		done
	done
}
start 1
check layout "$layout" 390 768 1440
stop
start 0
check motion "$motion" 390
stop
printf '%s\n' "${results[@]}" | jq -s . > "$output/options.json"
jq -r '"\(length) page loads, \([.[] | select(.count > 0)] | length) with problems"' "$output/options.json"
exit "$failed"
