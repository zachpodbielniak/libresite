/* Category filtering. Without this script every entry stays visible and the
 * controls stay hidden. */
document.querySelectorAll("[data-filter]").forEach(function (root) {
	var controls = root.querySelector(".filter__controls");
	var buttons = Array.prototype.slice.call(root.querySelectorAll("[data-filter-value]"));
	var items = Array.prototype.slice.call(root.querySelectorAll("[data-filter-item]"));
	var status = root.querySelector("[data-filter-status]");
	var empty = root.querySelector("[data-filter-empty]");
	if (!controls || !status || !empty) return;
	function apply(value) {
		var shown = 0;
		items.forEach(function (item) {
			var visible = value === "" || item.getAttribute("data-filter-item") === value;
			item.hidden = !visible;
			if (visible) shown += 1;
		});
		buttons.forEach(function (button) {
			button.setAttribute("aria-pressed", String(button.getAttribute("data-filter-value") === value));
		});
		empty.hidden = shown !== 0;
		status.textContent = (shown === 1 ? status.getAttribute("data-template-one")
			: status.getAttribute("data-template-many")).replace("{n}", String(shown));
	}
	buttons.forEach(function (button) {
		button.addEventListener("click", function () { apply(button.getAttribute("data-filter-value")); });
	});
	controls.hidden = false;
	apply("");
});
