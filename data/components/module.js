/* Supplied podbay modules. A module form posts as an ordinary form without
 * scripts (the builder gives it action and method from its hx-post). With
 * scripts it sends the same fields in the background as htmx would, with
 * HX-Request: true, and places the host's answer into its hx-target using
 * its hx-swap, one of podbay's eight modes; elements of the answer marked
 * hx-swap-oob replace the page element with the same id. The answer is
 * parsed, never run as script: svg and math are dropped so they cannot set
 * a handler after the walk. When nothing comes back the target shows the
 * site's own "not sent" words and every entry is kept. A page that loads
 * htmx itself is left to htmx. */
document.querySelectorAll("form[data-podbay-form]").forEach(function (form) {
	var modes = ["innerHTML", "outerHTML", "beforebegin", "afterbegin", "beforeend", "afterend", "delete", "none"];
	var button = form.querySelector("button[type=submit]");
	var pending = false;
	function target() {
		var id = (form.getAttribute("hx-target") || "").replace(/^#/, "");
		return id ? document.getElementById(id) : null;
	}
	function nodes(html) {
		var body = new DOMParser().parseFromString("<body>" + html + "</body>", "text/html").body;
		/* Nothing in the answer runs. Drop scripts and embedded documents,
		 * plus svg and math: an animate or set element can write an event
		 * handler or a javascript: address onto a live node after this walk.
		 * A template's contents are not in this tree, so drop the template too. */
		Array.prototype.slice.call(body.querySelectorAll("script, iframe, frame, object, embed, base, meta, link, svg, math, template, animate, set, animatetransform, animatemotion"))
			.forEach(function (s) { s.remove(); });
		Array.prototype.slice.call(body.querySelectorAll("*")).forEach(function (element) {
			Array.prototype.slice.call(element.attributes).forEach(function (attribute) {
				var name = attribute.name.toLowerCase();
				var value = attribute.value.replace(/[\u0000-\u0020]/g, "").toLowerCase();
				if (name.indexOf("on") === 0 || (/^(javascript|vbscript|data):/.test(value) &&
					["href", "src", "action", "formaction", "xlink:href", "srcset", "poster"].indexOf(name) >= 0))
					element.removeAttribute(attribute.name);
			});
		});
		Array.prototype.slice.call(body.querySelectorAll("[hx-swap-oob][id]")).forEach(function (oob) {
			var old = document.getElementById(oob.id);
			oob.remove();
			oob.removeAttribute("hx-swap-oob");
			if (old) old.replaceWith(oob);
		});
		return Array.prototype.slice.call(body.childNodes);
	}
	function place(into, swap, html) {
		var parts = nodes(html);
		if (swap === "none") return;
		if (swap === "delete") { into.remove(); return; }
		if (swap === "innerHTML") into.replaceChildren.apply(into, parts);
		else if (swap === "outerHTML") into.replaceWith.apply(into, parts);
		else if (swap === "beforebegin") into.before.apply(into, parts);
		else if (swap === "afterbegin") into.prepend.apply(into, parts);
		else if (swap === "beforeend") into.append.apply(into, parts);
		else into.after.apply(into, parts);
	}
	function failed(into) {
		into.textContent = form.getAttribute("data-failed");
		into.setAttribute("data-state", "failed");
	}
	if (window.htmx || !window.fetch || !window.DOMParser || !window.FormData) return;
	form.addEventListener("submit", function (event) {
		var into = target();
		var swap = form.getAttribute("hx-swap") || "innerHTML";
		if (!into || modes.indexOf(swap) < 0) return;
		event.preventDefault();
		if (pending) return;
		pending = true;
		if (button) button.disabled = true;
		into.removeAttribute("data-state");
		into.setAttribute("aria-busy", "true");
		fetch(form.getAttribute("hx-post"), {
			method: "POST",
			body: new FormData(form),
			headers: { "HX-Request": "true", "Accept": "text/html" },
			credentials: "same-origin"
		}).then(function (response) {
			return response.text().then(function (html) { return { ok: response.ok, html: html }; });
		}).then(function (answer) {
			if (!answer.html.trim()) {
				if (answer.ok) form.reset(); else failed(into);
				return;
			}
			place(into, swap, answer.html);
			if (answer.ok) form.reset();
		}).catch(function () {
			failed(into);
		}).then(function () {
			pending = false;
			if (button) button.disabled = false;
			into.removeAttribute("aria-busy");
		});
	});
});
