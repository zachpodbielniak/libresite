/* Headers that move or open (docs/components.org, "Skeletons, motion and
 * overlays"). The header over the home's photo is held at the top and turns
 * solid once the page scrolls; the sticky header gets slimmer. The drawer
 * and overlay menus open from the Menu button: the rest of the page is
 * inert while they are open, Tab stays inside, Escape and the close button
 * shut them and the focus goes back to the button. Without this script the
 * links stay in the bar. */
(function () {
	var body = document.body, header = document.querySelector(".site-header");
	if (!body || !header) return;
	if (body.classList.contains("header--transparent-solid") || body.classList.contains("header--shrink")) {
		if (body.classList.contains("header--transparent-solid")) body.classList.add("header-fixed");
		var update = function () { header.classList.toggle("is-scrolled", window.scrollY > 40); };
		update();
		window.addEventListener("scroll", update, {passive: true});
	}
	if (!body.classList.contains("header--drawer") && !body.classList.contains("header--overlay")) return;
	var toggle = header.querySelector(".nav-toggle"), nav = document.getElementById("site-nav");
	if (!toggle || !nav) return;
	var close = nav.querySelector(".nav-close");
	if (close) close.hidden = false;
	function outside(on) {
		[document.querySelector("main"), document.querySelector(".site-footer"), document.querySelector(".announce"),
			header.querySelector(".brand"), nav.querySelector(":scope > .button")].forEach(function (element) {
			if (!element) return;
			if (on) element.setAttribute("inert", "");
			else element.removeAttribute("inert");
		});
	}
	function focusable() {
		return Array.prototype.filter.call(nav.querySelectorAll(".nav-close, ul a"), function (element) {
			return element.offsetWidth > 0 || element.offsetHeight > 0;
		});
	}
	function shut(back) {
		nav.classList.remove("is-open");
		toggle.setAttribute("aria-expanded", "false");
		outside(false);
		if (back) toggle.focus();
	}
	/* nav.js toggles the menu first; this follows its state. */
	toggle.addEventListener("click", function () {
		var open = nav.classList.contains("is-open");
		outside(open);
		if (open && close) close.focus();
	});
	if (close) close.addEventListener("click", function () { shut(true); });
	nav.addEventListener("click", function (event) {
		var link = event.target.closest ? event.target.closest("ul a") : null;
		if (link && nav.classList.contains("is-open")) shut(false);
	});
	document.addEventListener("keydown", function (event) {
		if (event.key === "Escape") {
			if (nav.classList.contains("is-open")) shut(true);
			else outside(false);
			return;
		}
		if (event.key !== "Tab" || !nav.classList.contains("is-open")) return;
		var items = focusable();
		if (items.length === 0) return;
		var first = items[0], last = items[items.length - 1];
		if (event.shiftKey && document.activeElement === first) {
			event.preventDefault();
			last.focus();
		} else if (!event.shiftKey && document.activeElement === last) {
			event.preventDefault();
			first.focus();
		} else if (items.indexOf(document.activeElement) < 0) {
			event.preventDefault();
			first.focus();
		}
	});
})();
