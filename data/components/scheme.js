/* Light and dark (docs/components.org, "Light and dark"). The head script
 * has already set a stored choice before the stylesheet painted; this one
 * shows the toggles and cycles system, light and dark. A choice is kept in
 * localStorage; system removes it, so the visitor's device decides again.
 * Nothing moves when the scheme changes. Without scripts the toggles stay
 * hidden and the stylesheet follows the system setting. */
(function () {
	var order = ["system", "light", "dark"];
	var root = document.documentElement;
	var toggles = Array.prototype.slice.call(document.querySelectorAll("[data-scheme-toggle]"));
	function current() {
		return root.dataset.scheme === "light" || root.dataset.scheme === "dark" ? root.dataset.scheme : "system";
	}
	function show(state) {
		toggles.forEach(function (toggle) {
			var words = toggle.getAttribute("data-" + state) || state;
			var text = toggle.querySelector(".scheme-toggle__state");
			toggle.setAttribute("aria-label", (toggle.getAttribute("data-label") || "") + ": " + words);
			toggle.setAttribute("data-state", state);
			if (text) text.textContent = words;
		});
	}
	function choose(state) {
		if (state === "system") delete root.dataset.scheme;
		else root.dataset.scheme = state;
		try {
			if (state === "system") localStorage.removeItem("ls-scheme");
			else localStorage.setItem("ls-scheme", state);
		} catch (error) { /* A private window may refuse storage; the choice still holds on this page. */ }
		show(state);
	}
	if (!toggles.length) return;
	toggles.forEach(function (toggle) {
		toggle.hidden = false;
		toggle.addEventListener("click", function () {
			choose(order[(order.indexOf(current()) + 1) % order.length]);
		});
	});
	show(current());
})();
