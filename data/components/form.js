/* Local forms build a plain-text file in the browser. Nothing is sent over the
 * network; entered text is only ever written with textContent. */
document.querySelectorAll("[data-local-form]").forEach(function (form) {
	var actions = form.querySelector("[data-form-actions]");
	var output = form.querySelector("[data-form-output]");
	var preview = form.querySelector("[data-form-preview]");
	var download = form.querySelector("[data-form-download]");
	var summary = form.querySelector("[data-form-summary]");
	var fallback = form.querySelector("[data-form-fallback]");
	var controls = Array.prototype.slice.call(form.querySelectorAll("input, select, textarea"));
	var url = null;
	if (!actions || !output || !preview || !download || !summary || typeof Blob === "undefined") return;
	function labelOf(control) {
		var label = form.querySelector('label[for="' + control.id + '"]');
		var text = label ? label.cloneNode(true) : null;
		if (!text) return control.name;
		Array.prototype.slice.call(text.querySelectorAll(".required")).forEach(function (node) { node.remove(); });
		return text.textContent.trim();
	}
	function errorOf(control) { return document.getElementById(control.id + "-error"); }
	function clear() {
		controls.forEach(function (control) {
			control.removeAttribute("aria-invalid");
			if (errorOf(control)) errorOf(control).textContent = "";
		});
		summary.hidden = true;
		summary.textContent = "";
	}
	function release() {
		if (url) { URL.revokeObjectURL(url); url = null; }
		download.setAttribute("href", "#");
	}
	function check(control) {
		var value = control.type === "checkbox" ? (control.checked ? "yes" : "") : control.value.trim();
		if (control.required && value === "") return form.getAttribute("data-error-required");
		if (value !== "" && control.type !== "checkbox" && control.checkValidity && !control.checkValidity())
			return form.getAttribute("data-error-invalid");
		if (control.maxLength > 0 && control.value.length > control.maxLength) return form.getAttribute("data-error-invalid");
		return "";
	}
	form.addEventListener("submit", function (event) {
		var invalid = [];
		var lines = [];
		event.preventDefault();
		clear();
		controls.forEach(function (control) {
			var message = check(control);
			if (message) {
				invalid.push(control);
				control.setAttribute("aria-invalid", "true");
				if (errorOf(control)) errorOf(control).textContent = message;
			}
		});
		if (invalid.length) {
			output.hidden = true;
			release();
			summary.textContent = form.getAttribute("data-error-summary").replace("{n}", String(invalid.length));
			summary.hidden = false;
			invalid[0].focus();
			return;
		}
		controls.forEach(function (control) {
			var value = control.type === "checkbox" ? (control.checked ? "yes" : "no") : control.value.replace(/\r\n?/g, "\n").trim();
			lines.push(labelOf(control) + ":\n" + (value === "" ? "-" : value) + "\n");
		});
		preview.textContent = lines.join("\n");
		var next = URL.createObjectURL(new Blob([preview.textContent + "\n"], { type: "text/plain;charset=utf-8" }));
		if (url) URL.revokeObjectURL(url);
		url = next;
		download.setAttribute("href", url);
		output.hidden = false;
		preview.focus();
	});
	form.addEventListener("reset", function () {
		clear();
		release();
		preview.textContent = "";
		output.hidden = true;
	});
	actions.hidden = false;
	if (fallback) fallback.hidden = true;
});

/* Endpoint forms still work as an ordinary POST: the host answers it, and
 * returns the visitor to the site's thank-you address when one is
 * set, else to the page with ?sent=1 or ?error=CODE. With scripts they send
 * the same fields in the background, asking for JSON, and stay on the page.
 * Only a 2xx answer whose JSON says "received" is a send: the form is
 * cleared and hidden and the confirmation panel shown and focused in its
 * place. A JSON refusal shows the error banner and marks the field it names;
 * any other answer, or none, shows the same banner. Every entry is kept.
 * Visible text is the site's own; the host's English message is not shown. */
document.querySelectorAll("[data-endpoint-form]").forEach(function (form) {
	var status = form.querySelector("[data-form-status]");
	var summary = form.querySelector("[data-form-summary]");
	var button = form.querySelector("button[type=submit]");
	var sent = form.parentNode.querySelector("[data-form-sent]");
	var again = sent ? sent.querySelector("[data-form-again]") : null;
	var controls = Array.prototype.slice.call(form.querySelectorAll(".field input, .field select, .field textarea"));
	var calm = !window.matchMedia || window.matchMedia("(prefers-reduced-motion: reduce)").matches;
	var pending = false;
	var returned = null;
	function say(text, state) {
		if (!status) return;
		status.textContent = text;
		if (state) status.setAttribute("data-state", state); else status.removeAttribute("data-state");
	}
	function reveal(node) {
		if (node.scrollIntoView) node.scrollIntoView({ block: "center", behavior: calm ? "auto" : "smooth" });
	}
	function confirm() {
		if (!sent) return;
		form.hidden = true;
		sent.hidden = false;
		sent.focus({ preventScroll: true });
		reveal(sent);
	}
	function errorOf(control) { return document.getElementById(control.id + "-error"); }
	function clear() {
		controls.forEach(function (control) {
			control.removeAttribute("aria-invalid");
			if (errorOf(control)) errorOf(control).textContent = "";
		});
		if (!summary) return;
		summary.hidden = true;
		summary.textContent = "";
	}
	/* The banner, and the field the host named when this form has one by that
	 * name: marked, told why and focused. Nothing is cleared or hidden. */
	function refuse(field) {
		var named = null;
		clear();
		if (!summary) return;
		controls.forEach(function (control) { if (field && control.name === field) named = control; });
		summary.textContent = form.getAttribute("data-failed");
		summary.hidden = false;
		if (named) {
			named.setAttribute("aria-invalid", "true");
			if (errorOf(named)) errorOf(named).textContent = form.getAttribute("data-error-invalid");
			named.focus();
		} else reveal(summary);
	}
	/* Back from a send without scripts: the page's first live form shows
	 * what the host returned with. The host adds no fragment. */
	if (document.querySelector("[data-endpoint-form]") === form) {
		returned = /[?&]error=([^&]*)/.exec(window.location.search);
		if (/[?&]sent=1(&|$)/.test(window.location.search)) confirm();
		else if (returned) refuse(returned[1]);
	}
	if (again) again.addEventListener("click", function () {
		sent.hidden = true;
		form.hidden = false;
		if (controls.length) controls[0].focus();
	});
	if (!status || !summary || typeof fetch === "undefined" || typeof URLSearchParams === "undefined" ||
		typeof FormData === "undefined") return;
	form.addEventListener("submit", function (event) {
		event.preventDefault();
		if (pending || !form.checkValidity()) { if (!pending) form.reportValidity(); return; }
		pending = true;
		if (button) button.disabled = true;
		clear();
		say(form.getAttribute("data-sending"), null);
		fetch(form.action, {
			method: "POST",
			headers: { "Content-Type": "application/x-www-form-urlencoded", "Accept": "application/json" },
			body: new URLSearchParams(new FormData(form)).toString()
		}).then(function (response) {
			return response.json().then(function (answer) {
				return { ok: response.ok, answer: answer };
			}, function () {
				return { ok: false, answer: null };
			});
		}).then(function (result) {
			var answer = result.answer || {};
			say("", null);
			if (result.ok && answer.status === "received") {
				form.reset();
				confirm();
			} else refuse(result.ok ? null : answer.field);
		}).catch(function () {
			say("", null);
			refuse(null);
		}).then(function () {
			pending = false;
			if (button) button.disabled = false;
		});
	});
});
