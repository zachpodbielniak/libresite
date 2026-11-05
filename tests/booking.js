/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* The probes of tests/booking.sh: run in the booking page with the probe's
 * name; each resolves with what it saw. */
const done = arguments[arguments.length - 1];
const which = arguments[0];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const rgb = c => (c.match(/[\d.]+/g) || []).map(Number).map((v, i) => c.startsWith("color(") && i < 3 ? v * 255 : v);
const lum = c => c.slice(0, 3).map(v => { v /= 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; })
  .reduce((s, v, i) => s + v * [0.2126, 0.7152, 0.0722][i], 0);
const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
const ground = el => { for (let n = el; n; n = n.parentElement) { const c = rgb(getComputedStyle(n).backgroundColor); if (c.length < 4 || c[3] > 0.9) return c; } return [255, 255, 255]; };
const shown = el => !!el && el.getClientRects().length > 0 && getComputedStyle(el).visibility !== "hidden";
const until = async (test, ms) => { for (let t = 0; t < ms; t += 50) { if (test()) return true; await sleep(50); } return test(); };
const root = () => document.querySelector(".booking-calendar [data-booking-url]");
const request = () => document.querySelector("[data-booking-request] form");
const app = () => document.querySelector(".booking-calendar__app");
const probes = {};
/* With the host answering: the calendar replaces the request form; the
 * first time was just taken (409) and the second books (201). */
probes.calendar = () => (async () => {
  const out = {app: false, request: false, taken: "", booked: ""};
  await until(() => shown(app()), 6000);
  out.app = shown(app());
  out.request = shown(request());
  if (!out.app) { done(out); return; }
  const scope = root(), status = scope.querySelector("[data-booking-status]");
  const form = scope.querySelector("[data-booking-form]");
  const day = scope.querySelector("[data-booking-days] button:not(:disabled)");
  if (!day) { done(out); return; }
  day.click();
  const pick = label => { const b = [...scope.querySelectorAll("[data-booking-time-list] button")].find(t => t.textContent === label); if (b) b.click(); return !!b; };
  if (!pick("Slot one")) { done(out); return; }
  form.elements.name.value = "Test Visitor";
  form.elements.email.value = "visitor@example.org";
  form.elements.agree.checked = true;
  form.requestSubmit();
  await until(() => /taken/.test(status.textContent), 5000);
  out.taken = status.textContent;
  if (!pick("Slot two")) { done(out); return; }
  form.requestSubmit();
  const confirmed = scope.querySelector("[data-booking-confirmed]");
  await until(() => !confirmed.hidden, 5000);
  out.booked = confirmed.hidden ? status.textContent : confirmed.textContent;
  done(out);
})();
/* With no host, or no open times: the request form shows, and every one
 * of its controls has a visible label, a 44 px target, an error text tied
 * by aria-describedby and a readable label in light and dark. */
probes.request = () => (async () => {
  const problems = [];
  let count = 0;
  /* The times request has had its chance to fail. */
  await sleep(1500);
  const out = {request: shown(request()), app: shown(app())};
  if (!out.request) { done({...out, count, problems: ["no request form shown"]}); return; }
  for (const scheme of ["light", "dark"]) {
    document.documentElement.dataset.scheme = scheme;
    await sleep(60);
    for (const control of request().querySelectorAll(".field input, .field textarea, .field select")) {
      if (control.type === "hidden" || control.closest(".form__trap")) continue;
      const name = `${scheme} ${control.type || control.tagName.toLowerCase()} ${control.name}`;
      count++;
      const label = control.labels && control.labels[0];
      if (!label || !label.innerText.trim()) { problems.push(`${name}: no visible label`); continue; }
      const check = control.type === "radio" || control.type === "checkbox";
      const target = check ? label.getBoundingClientRect() : control.getBoundingClientRect();
      if (target.height < 43.5 || (check && target.width < 43.5)) problems.push(`${name}: the target is ${Math.round(target.width)} x ${Math.round(target.height)} px`);
      const ids = (control.getAttribute("aria-describedby") || "").split(/\s+/).filter(Boolean);
      if (!ids.some(id => id.endsWith("-error"))) problems.push(`${name}: no error text in aria-describedby`);
      for (const id of ids) if (!document.getElementById(id)) problems.push(`${name}: aria-describedby names missing ${id}`);
      const text = check && control.type === "radio" ? label : (control.closest("fieldset") && control.type === "radio" ? control.closest("fieldset").querySelector("legend") : label);
      const contrast = ratio(rgb(getComputedStyle(text).color), ground(text));
      if (contrast < 4.5) problems.push(`${name}: the label reads at ${contrast.toFixed(2)}:1`);
    }
  }
  delete document.documentElement.dataset.scheme;
  done({...out, count, problems});
})();
probes[which]();
