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
