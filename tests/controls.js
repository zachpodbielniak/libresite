/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The probes of tests/controls.sh: run in the page with the probe's name;
 * each resolves with what it saw. */
const done = arguments[arguments.length - 1];
const which = arguments[0];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const rgb = c => (c.match(/[\d.]+/g) || []).map(Number).map((v, i) => c.startsWith("color(") && i < 3 ? v * 255 : v);
const lum = c => c.slice(0, 3).map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; })
  .reduce((s, v, i) => s + v * [0.2126, 0.7152, 0.0722][i], 0);
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
const ground = el => { for (let n = el; n; n = n.parentElement) { const c = rgb(getComputedStyle(n).backgroundColor); if (c.length < 4 || c[3] > 0.9) return c; } return [255, 255, 255]; };
const probes = {};
probes.clear = () => { localStorage.clear(); done(true); };
/* The scheme toggle: visible, named, 44 px, and cycling system, light,
 * dark, system with the choice stored; it leaves the visitor on dark. */
probes.toggle = () => (async () => {
  const root = document.documentElement, problems = [], states = [];
  const toggle = [...document.querySelectorAll("[data-scheme-toggle]")].find(t => t.getBoundingClientRect().height > 0);
  if (!toggle) { done({problems: ["no visible scheme toggle"], states}); return; }
  const r = toggle.getBoundingClientRect();
  if (r.height < 43.5 || r.width < 43.5) problems.push(`the toggle is ${Math.round(r.width)} x ${Math.round(r.height)} px, under 44`);
  if (!toggle.getAttribute("aria-label") && !toggle.innerText.trim()) problems.push("the toggle has no name");
  const state = () => [root.dataset.scheme || "system", localStorage.getItem("ls-scheme"), lum(ground(document.body)) < 0.1 ? "dark" : "light",
    toggle.getAttribute("aria-label") || toggle.innerText.trim()];
  states.push(state());
  for (let i = 0; i < 3; i++) { toggle.click(); await sleep(60); states.push(state()); }
  /* Leave the visitor on dark for the reload. */
  toggle.click(); await sleep(30); toggle.click(); await sleep(30);
  done({problems, states});
})();
/* After a reload: the chosen scheme, the stored choice, the ground and
 * every toggle's name. */
probes.reload = () => {
  const root = document.documentElement;
  done({scheme: root.dataset.scheme || "system", stored: localStorage.getItem("ls-scheme"),
  ground: lum(ground(document.body)) < 0.1 ? "dark" : "light",
  labels: [...document.querySelectorAll("[data-scheme-toggle]")].map(t => t.getAttribute("aria-label") || t.innerText.trim())});
};
/* Every form control in both schemes: a label, a 44 px target, a focus
 * ring, a border that reads on the page and a readable label. */
probes.controls = () => (async () => {
  const problems = [];
  let count = 0;
  for (const scheme of ["light", "dark"]) {
    document.documentElement.dataset.scheme = scheme;
    await sleep(60);
    for (const control of document.querySelectorAll("form .field input, form .field select, form .field textarea")) {
      if (control.type === "hidden" || control.closest(".form__trap")) continue;
      const name = `${scheme} ${control.type || control.tagName.toLowerCase()} ${control.name}`;
      count++;
      const label = control.labels && control.labels[0];
      if (!label || !label.innerText.trim()) { problems.push(`${name}: no visible label`); continue; }
      const check = control.type === "radio" || control.type === "checkbox";
      const target = check ? label.getBoundingClientRect() : control.getBoundingClientRect();
      if (target.height < 43.5 || (check && target.width < 43.5)) problems.push(`${name}: the target is ${Math.round(target.width)} x ${Math.round(target.height)} px`);
      control.focus({focusVisible: true});
      await sleep(10);
      const ring = [control, label, control.closest(".field")].some(el => { const s = getComputedStyle(el);
        return s.outlineStyle !== "none" && parseFloat(s.outlineWidth) >= 2; });
      if (!ring) problems.push(`${name}: no focus ring`);
      control.blur();
      /* The control is drawn on the page: its edge reads at 3:1. */
      const s = getComputedStyle(control), box = check || control.type === "range" || control.type === "file" ? null : rgb(s.borderTopColor);
      if (box && control.type !== "range" && ratio(box, ground(control.parentElement)) < 2.95 && ratio(ground(control), ground(control.parentElement)) < 2.95)
        problems.push(`${name}: its border is ${ratio(box, ground(control.parentElement)).toFixed(2)}:1 on the page`);
      const text = rgb(getComputedStyle(label).color);
      if (ratio(text, ground(label)) < 4.45) problems.push(`${name}: its label is ${ratio(text, ground(label)).toFixed(2)}:1`);
    }
  }
  delete document.documentElement.dataset.scheme;
  done({problems, count});
})();
probes[which]();
