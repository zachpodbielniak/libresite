/* Motion kits: figures count up once as they scroll into view. The page
 * keeps the exact supplied text; without scripts, without an observer or
 * with reduced motion nothing changes. */
(function () {
	if (!window.IntersectionObserver || !window.matchMedia || matchMedia("(prefers-reduced-motion: reduce)").matches)
		return;
	var cells = document.querySelectorAll("main .numbers__value, main .facts__item dd");
	var shape = /^(\D*?)(\d{1,3}(?:,\d{3})+|\d+)((?:\.\d+)?\D*)$/;
	function run(cell) {
		var text = cell.textContent, parts = text.trim().match(shape);
		if (!parts || cell.children.length > 0)
			return;
		var target = parseInt(parts[2].replace(/,/g, ""), 10), grouped = parts[2].indexOf(",") >= 0, start = null;
		/* Small counts and years stay as they are. */
		if (!(target > 9) || (!grouped && /^(1[89]|20)\d\d$/.test(parts[2])))
			return;
		cell.setAttribute("aria-label", text.trim());
		function frame(now) {
			if (start === null)
				start = now;
			var t = Math.min(1, (now - start) / 1100), value = Math.round(target * (1 - Math.pow(1 - t, 3)));
			cell.textContent = parts[1] + (grouped ? value.toLocaleString("en-US") : String(value)) + parts[3];
			if (t < 1)
				requestAnimationFrame(frame);
			else {
				cell.textContent = text;
				cell.removeAttribute("aria-label");
			}
		}
		requestAnimationFrame(frame);
	}
	var watch = new IntersectionObserver(function (entries) {
		entries.forEach(function (entry) {
			if (entry.isIntersecting) {
				watch.unobserve(entry.target);
				run(entry.target);
			}
		});
	}, {threshold: 0.6});
	Array.prototype.forEach.call(cells, function (cell) { watch.observe(cell); });
})();
