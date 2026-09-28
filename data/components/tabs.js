/* Tabs: without this script every panel is shown in order with its heading. */
document.querySelectorAll("[data-tabs]").forEach(function (root) {
	var list = root.querySelector("[role=tablist]");
	var tabs = Array.prototype.slice.call(root.querySelectorAll("[role=tab]"));
	var panels = tabs.map(function (tab) { return document.getElementById(tab.getAttribute("aria-controls")); });
	if (!list || !tabs.length || panels.indexOf(null) !== -1) return;
	function select(index, focus) {
		tabs.forEach(function (tab, i) {
			var on = i === index;
			tab.setAttribute("aria-selected", String(on));
			tab.tabIndex = on ? 0 : -1;
			panels[i].hidden = !on;
		});
		if (focus) tabs[index].focus();
	}
	tabs.forEach(function (tab, i) {
		panels[i].setAttribute("role", "tabpanel");
		panels[i].setAttribute("aria-labelledby", tab.id);
		tab.addEventListener("click", function () { select(i, false); });
		tab.addEventListener("keydown", function (event) {
			var next = event.key === "ArrowRight" ? i + 1 : event.key === "ArrowLeft" ? i - 1 : event.key === "Home" ? 0 : event.key === "End" ? tabs.length - 1 : null;
			if (next === null) return;
			event.preventDefault();
			select((next + tabs.length) % tabs.length, true);
		});
	});
	list.hidden = false;
	root.classList.add("is-ready");
	select(0, false);
});
