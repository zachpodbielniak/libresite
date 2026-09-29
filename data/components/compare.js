/* Before-and-after slider. Without this script both pictures sit side by side. */
document.querySelectorAll("[data-compare]").forEach(function (pair) {
	var range = pair.querySelector(".compare__range");
	if (!range || pair.querySelectorAll(".compare__side").length !== 2) return;
	function update() { pair.style.setProperty("--split", range.value + "%"); }
	range.addEventListener("input", update);
	range.hidden = false;
	pair.classList.add("is-ready");
	update();
});
