/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The site gate's page probe (scripts/site-gate.sh, docs/site-gate.org).
 * Runs in the page with {home}; resolves with {width, height, findings},
 * each finding {check, detail}. The first-screen checks (photo, headline,
 * action) run on the home page only; the rest on every page. */
const done = arguments[arguments.length - 1];
const opts = arguments[0] || {};
const sleep = ms => new Promise(r => setTimeout(r, ms));
/* rgb() channels run to 255; mixed colours come back as color(srgb) in 0 to 1. */
const parse = c => { const m = c && c.match(/[\d.]+/g); if (!m) return null; const v = m.map(Number);
  return c.startsWith("color(") ? v.map((x, i) => i < 3 ? x * 255 : x) : v; };
const opaque = c => c && (c.length < 4 || c[3] > 0.9);
const lum = ([r, g, b]) => [r, g, b].map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; })
  .reduce((s, v, i) => s + v * [0.2126, 0.7152, 0.0722][i], 0);
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
const words = s => (s.match(/[\p{L}\p{N}][\p{L}\p{N}'’.@-]*/gu) || []).length;
const short = s => { s = s.replace(/\s+/g, " ").trim(); return s.length > 40 ? s.slice(0, 39) + "…" : s; };
const label = el => `${el.tagName.toLowerCase()} "${short(el.innerText || el.getAttribute("aria-label") || el.getAttribute("alt") || "")}"`;
const hiddenBy = el => el.closest("[aria-hidden='true'], [hidden], .visually-hidden, .sr-only, dialog:not([open]), noscript, template") ||
  (el.closest("details:not([open])") && !el.closest("summary"));
const boxed = el => { const s = getComputedStyle(el), r = el.getBoundingClientRect();
  return s.display !== "none" && s.visibility !== "hidden" && parseFloat(s.opacity) > 0.05 && r.width > 1 && r.height > 1; };
const shown = el => boxed(el) && !hiddenBy(el);
const own = el => [...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim());
/* The first opaque background behind an element, or null when a picture or
 * gradient is behind it first. */
const backdrop = el => {
  for (let node = el; node; node = node.parentElement) {
    const s = getComputedStyle(node);
    if (s.backgroundImage !== "none" && node !== document.documentElement) return null;
    const c = parse(s.backgroundColor);
    if (opaque(c)) return c;
  }
  return [255, 255, 255];
};
const onScreen = r => r.bottom > 0 && r.top < innerHeight && r.right > 0 && r.left < innerWidth;
const overlap = (a, b) => Math.max(0, Math.min(a.right, b.right) - Math.max(a.left, b.left)) *
  Math.max(0, Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top));
/* Sticky elements that stay put while the page moves. A single scroll can
 * miss a bar that only sticks for a few pixels, so each one is lined up
 * with the top of the screen and the page is moved a little. */
const held = async () => {
  const sticky = [...document.querySelectorAll("body *")].filter(el => getComputedStyle(el).position === "sticky");
  const y = scrollY;
  const found = new Map();
  try {
    for (const el of sticky) {
      if (!shown(el)) continue;
      /* A bar inside another pinned bar is the same bar. Match pinned(). */
      let nested = false;
      for (let up = el.parentElement; up && up !== document.body; up = up.parentElement) {
        const p = getComputedStyle(up).position;
        if (p === "fixed" || p === "sticky") { nested = true; break; }
      }
      if (nested) continue;
      const origin = el.getBoundingClientRect().top + scrollY;
      scrollTo(0, Math.max(0, origin));
      await sleep(40);
      const before = el.getBoundingClientRect().top;
      const y1 = scrollY;
      scrollBy(0, 8);
      await sleep(40);
      if (scrollY - y1 <= 1 || Math.abs(el.getBoundingClientRect().top - before) >= 1) continue;
      /* Height is taken while it is stuck. Measuring after the scroll is
       * restored misses a bar that lives further down the page. */
      const r = el.getBoundingClientRect();
      const h = Math.min(r.bottom, innerHeight) - Math.max(r.top, 0);
      if (r.width >= innerWidth / 2 && h > 0) found.set(el, h);
    }
  } finally {
    scrollTo(0, y);
    await sleep(40);
  }
  return found;
};
/* Bars pinned over the page: fixed elements, and held sticky ones, at
 * least half the screen wide, not inside another pinned one. */
const pinned = kept => [...document.querySelectorAll("body *")].filter(el => {
  const s = getComputedStyle(el);
  if (s.position !== "fixed" && s.position !== "sticky") return false;
  for (let up = el.parentElement; up && up !== document.body; up = up.parentElement) {
    const p = getComputedStyle(up).position;
    if (p === "fixed" || p === "sticky") return false;
  }
  const r = el.getBoundingClientRect();
  if (!shown(el) || !onScreen(r) || r.width < innerWidth / 2) return false;
  return s.position === "fixed" || kept.has(el);
});
const raster = url => !!url && !/^data:image\/svg/i.test(url) && !/\.svgz?([?#]|$)/i.test(url);

(async () => {
  const findings = [];
  const say = (check, detail) => findings.push({check, detail});
  await Promise.race([document.fonts.ready, sleep(3000)]);
  await sleep(150);
  /* A smooth scroll would finish after the measurements below. */
  document.documentElement.style.scrollBehavior = "auto";
  scrollTo(0, 0);
  await sleep(50);
  const W = innerWidth, H = innerHeight, phone = W < 600;
  const root = document.documentElement;
  const bars = new Map();
  const keepBars = kept => { for (const el of pinned(kept)) {
    const r = el.getBoundingClientRect(), h = Math.min(r.bottom, H) - Math.max(r.top, 0);
    if (!bars.has(el) || bars.get(el) < h) bars.set(el, h);
  } };
  const noteHeld = async () => {
    const found = await held();
    for (const [el, h] of found) if (!bars.has(el) || bars.get(el) < h) bars.set(el, h);
    keepBars(new Set(found.keys()));
  };
  await noteHeld();
  const pinnedNow = [...bars.keys()].map(el => el.getBoundingClientRect());

  if (opts.home) {
    /* photo: a raster picture covering at least 10 percent of the first screen. */
    const pictures = [];
    for (const el of document.querySelectorAll("body *")) {
      if (!shown(el)) continue;
      const r = el.getBoundingClientRect();
      if (!onScreen(r)) continue;
      let url = null;
      if (el.tagName === "IMG" && el.complete && el.naturalWidth > 0) url = el.currentSrc || el.src;
      else if (el.tagName === "VIDEO" && el.poster) url = el.poster;
      else {
        const m = getComputedStyle(el).backgroundImage.match(/url\(["']?([^"')]+)["']?\)/);
        if (m) url = m[1];
      }
      if (!url) continue;
      const seen = overlap(r, {left: 0, top: 0, right: W, bottom: H}) / (W * H);
      pictures.push({url, seen, el});
    }
    const best = pictures.filter(p => raster(p.url)).sort((a, b) => b.seen - a.seen)[0];
    if (!best || best.seen < 0.1) {
      const drawn = pictures.filter(p => !raster(p.url)).length;
      say("photo", best ? `the largest photo covers ${Math.round(best.seen * 100)}% of the first screen, under 10%` :
        `no photo on the first screen${drawn ? ` (${drawn} drawing${drawn > 1 ? "s" : ""})` : ""}`);
    }

    /* headline: the first visible H1, on the first screen, clear of every
     * pinned bar, and at least 28 px on a phone. */
    const h1 = [...document.querySelectorAll("h1")].find(shown);
    if (!h1) say("headline", "no visible H1");
    else {
      const r = h1.getBoundingClientRect(), size = parseFloat(getComputedStyle(h1).fontSize);
      if (phone && size < 27.5) say("headline", `the H1 "${short(h1.innerText)}" is ${Math.round(size)} px, under 28`);
      if (r.top < 0 || r.bottom > H + 1) say("headline", `the H1 "${short(h1.innerText)}" ends ${Math.round(r.bottom)} px down a ${H} px screen`);
      const cover = [...bars.keys()].find((el, i) => !el.contains(h1) && overlap(pinnedNow[i], r) > 4);
      if (cover) say("headline", `the H1 "${short(h1.innerText)}" is under the fixed ${cover.tagName.toLowerCase()}${cover.className ? "." + String(cover.className).trim().split(/\s+/)[0] : ""}`);
    }

    /* action: filled buttons on the first screen, counted by where they go. */
    const actions = new Map();
    for (const el of document.querySelectorAll("a[href], button, input[type=submit], input[type=button]")) {
      if (!shown(el)) continue;
      const r = el.getBoundingClientRect();
      if (!onScreen(r) || r.height < 24) continue;
      const s = getComputedStyle(el), fill = parse(s.backgroundColor);
      const behind = backdrop(el.parentElement) || [255, 255, 255];
      const filled = (opaque(fill) && ratio(fill, behind) >= 1.5) || /gradient\(/.test(s.backgroundImage);
      if (!filled) continue;
      const text = short(el.innerText || el.value || el.getAttribute("aria-label") || "");
      if (!text) continue;
      /* Same destination counts once. A control with no destination, and a
       * href the browser cannot parse, is its own action. A bad href must
       * not reject the whole probe. */
      let key;
      const href = el.getAttribute("href");
      if (href === null) {
        key = el.form ? "form:" + el.form.action : "button:" + text;
        if (!el.form && actions.has(key)) key = key + "#" + actions.size;
      } else if (/^tel:/i.test(href)) key = "tel:" + href.replace(/\D/g, "");
      else if (/^mailto:/i.test(href)) key = href.toLowerCase();
      else {
        try {
          const u = new URL(href, location.href);
          key = u.origin + u.pathname.replace(/\/$/, "/index.html") + u.hash;
        } catch (e) {
          key = "url:" + href;
        }
      }
      if (!actions.has(key)) actions.set(key, text);
    }
    if (actions.size === 0) say("action", "no primary action on the first screen");
    else if (actions.size > 1) say("action", `${actions.size} primary actions on the first screen: ${[...actions.values()].map(t => `"${t}"`).join(", ")}`);
  }

  /* copy: banned filler and demonstration lines. */
  const text = document.title + "\n" + document.body.innerText;
  for (const re of [/example business/i, /for demonstration/i, /\bends here\b/i,
    /\bone of the (?:\d+|[a-z]+) services\b/i, /\bask us about an? [a-z]/i]) {
    const m = text.match(re);
    if (m) { const line = text.slice(text.lastIndexOf("\n", m.index) + 1).split("\n")[0]; say("copy", `"${short(line)}"`); }
  }

  /* Scroll through so content that waits for the screen settles, and the
   * sticky bars show their held height. */
  for (let y = 0; y <= root.scrollHeight; y += H / 2) { scrollTo(0, y); await sleep(40); }
  scrollTo(0, Math.min(H, root.scrollHeight - H));
  await sleep(80);
  await noteHeld();
  scrollTo(0, 0);
  await sleep(80);

  /* bar: no pinned bar over 12 percent of a phone's screen. */
  if (phone) for (const [el, h] of bars) if (h > H * 0.12)
    say("bar", `a fixed ${el.tagName.toLowerCase()}${el.className ? "." + String(el.className).trim().split(/\s+/)[0] : ""} covers ${Math.round(h / H * 100)}% of the screen (${Math.round(h)} of ${H} px)`);

  /* empty: a section with no words but its heading and short links, or a
   * topic section without its fact. A link of five words or more (a card)
   * and a phone or email link are content. */
  const sections = [...document.querySelectorAll("main section, main article, body > section")]
    .filter(sec => shown(sec) && !sec.querySelector("section, article"));
  for (const sec of sections) {
    const head = sec.querySelector("h1, h2, h3, h4, h5, h6");
    const name = head ? `"${short(head.innerText)}"` : label(sec);
    let count = 0;
    const walker = document.createTreeWalker(sec, NodeFilter.SHOW_TEXT);
    for (let n = walker.nextNode(); n; n = walker.nextNode()) {
      const up = n.parentElement;
      /* Text in a closed fold is one tap away: it counts when the fold shows. */
      const fold = up && up.closest("details:not([open])");
      if (!up || !(fold && sec.contains(fold) ? boxed(fold) && !hiddenBy(fold.parentElement) : shown(up)) || (head && head.contains(n))) continue;
      const link = up.closest("a, button");
      if (link && sec.contains(link) && !/^(tel|mailto):/i.test(link.getAttribute("href") || "") &&
        words(link.innerText) < 5) continue;
      count += words(n.textContent);
    }
    const media = [...sec.querySelectorAll("img, picture, video, iframe, form, table, svg")].some(m => {
      const r = m.getBoundingClientRect(); return shown(m) && r.width >= 64 && r.height >= 64; });
    const body = sec.textContent;
    const topic = head ? head.innerText : "";
    if (count === 0 && !media) say("empty", `the section ${name} has only a heading and links`);
    else if (/\bhours\b|\bopening times\b|\bwhen we(?: are|'re) open\b/i.test(topic) &&
      !/\b\d{1,2}(?::\d{2})?\s*(?:am|pm|a\.m\.|p\.m\.)|\b\d{1,2}:\d{2}\b|24\/7|24 hours|\bclosed\b/i.test(body))
      say("empty", `the section ${name} gives no hours`);
    else if (/\breviews?\b|\btestimonials?\b|what (?:our )?(?:customers|clients|patients|people|neighbou?rs) say/i.test(topic) &&
      !sec.querySelector("blockquote, q, [itemprop=review]") &&
      ![...sec.querySelectorAll("a[href^='http']")].some(a => {
        try { return new URL(a.href).host !== location.host; } catch (e) { return false; }
      }))
      say("empty", `the section ${name} has no review and no link to reviews`);
    else if (/\bhow\b.*\b(?:works?|runs?|goes)\b|\bprocess\b|\bstep by step\b/i.test(topic) &&
      sec.querySelectorAll("li").length < 2 && sec.querySelectorAll("h3, h4, h5").length < 2)
      say("empty", `the section ${name} lists no steps`);
  }

  /* sideways, clipped and contrast: the checks of tests/gate.sh and
   * tests/options.sh, on every page. */
  if (root.scrollWidth > W + 1) {
    const wide = [...document.querySelectorAll("body *")].find(el => shown(el) && el.getBoundingClientRect().right > W + 1 &&
      !(el.parentElement && el.parentElement.getBoundingClientRect().right > W + 1));
    say("sideways", `the page is ${root.scrollWidth} px wide in a ${W} px screen${wide ? `, from ${label(wide)}` : ""}`);
  }
  const texts = [...document.querySelectorAll("body *")].filter(el => own(el) && shown(el) &&
    !el.closest(".marquee, .kit-marquee, .skip-link, .skip"));
  const page = el => { const r = el.getBoundingClientRect(); return {l: r.left + scrollX, t: r.top + scrollY, r: r.right + scrollX, b: r.bottom + scrollY}; };
  const clipped = [], low = [];
  for (const el of texts) {
    const box = page(el), s = getComputedStyle(el);
    /* Clipped: text running past an ancestor that hides overflow. */
    let cut = false;
    for (let up = el.parentElement; up && up !== document.body; up = up.parentElement) {
      const u = getComputedStyle(up);
      if (u.overflowX === "visible" && u.overflowY === "visible") continue;
      if (u.overflowX === "auto" || u.overflowX === "scroll") break;
      const clip = page(up);
      cut = box.l < clip.l - 2 || box.r > clip.r + 2 || box.b > clip.b + 2;
      break;
    }
    if (cut || (el.scrollWidth > el.clientWidth + 2 && s.overflowX !== "visible" && s.overflowX !== "auto")) clipped.push(label(el));
    /* Contrast: WCAG AA against the first opaque background; text over a
     * picture, in outline or translucent is not measured here. */
    if (parseFloat(s.webkitTextStrokeWidth || "0") > 0) continue;
    const fg = parse(s.color), bg = backdrop(el);
    if (!bg || !fg || (fg.length > 3 && fg[3] < 0.9)) continue;
    const size = parseFloat(s.fontSize), bold = parseInt(s.fontWeight, 10) >= 700;
    const need = size >= 24 || (size >= 18.66 && bold) ? 3 : 4.5, got = ratio(fg, bg);
    if (got < need - 0.05) low.push(`${label(el)} ${got.toFixed(2)}:1`);
  }
  if (clipped.length) say("clipped", `${clipped.length} cut off, such as ${clipped.slice(0, 2).join(", ")}`);
  if (low.length) say("contrast", `${low.length} below AA, such as ${low.slice(0, 2).join(", ")}`);
  done({width: W, height: H, findings});
})();
