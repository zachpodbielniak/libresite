/* Narrow-screen menu. Without this script the navigation is always visible. */
(function () {
	var toggle = document.querySelector(".nav-toggle");
	var nav = document.getElementById("site-nav");
	document.documentElement.classList.add("js");
	if (!toggle || !nav) return;
	toggle.hidden = false;
	toggle.addEventListener("click", function () {
		var open = toggle.getAttribute("aria-expanded") !== "true";
		toggle.setAttribute("aria-expanded", String(open));
		nav.classList.toggle("is-open", open);
	});
	document.addEventListener("keydown", function (event) {
		if (event.key === "Escape" && nav.classList.contains("is-open")) {
			nav.classList.remove("is-open");
			toggle.setAttribute("aria-expanded", "false");
			toggle.focus();
		}
	});
})();
