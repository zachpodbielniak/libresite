/* Motion (docs/components.org, "Skeletons, motion and overlays"). Content
 * below the first screen fades and rises as it scrolls in; with the lively
 * level, items of a list follow one another and small whole numbers count
 * up. Content is visible without this script and before it runs: it hides
 * only what is below the first screen, and does nothing at all when the
 * visitor asks for reduced motion. */
(function () {
	var body = document.body;
	if (!window.matchMedia || window.matchMedia("(prefers-reduced-motion: reduce)").matches) return;
	if (!("IntersectionObserver" in window) || !body) return;
	var lively = body.classList.contains("motion--lively");
	var targets = [];
	Array.prototype.forEach.call(document.querySelectorAll("main > .block:not(:first-child) > .wrap > *, main > .block:not(:first-child) > figure"),
		function (element) {
			var tag = element.tagName;
			if (lively && (tag === "UL" || tag === "OL" || tag === "DL" || element.classList.contains("faq__list"))) {
				Array.prototype.forEach.call(element.children, function (item, index) {
					item.style.setProperty("--reveal-delay", Math.min(index, 5) * 90 + "ms");
					targets.push(item);
				});
			} else
				targets.push(element);
		});
	function show(element) {
		element.classList.add("is-shown");
		element.classList.remove("is-pending");
	}
	var observer = new IntersectionObserver(function (entries) {
		entries.forEach(function (entry) {
			if (!entry.isIntersecting) return;
			observer.unobserve(entry.target);
			show(entry.target);
		});
	}, {rootMargin: "0px 0px -6% 0px", threshold: 0.01});
	targets.forEach(function (element) {
		var box = element.getBoundingClientRect();
		/* On the first screen, or not laid out: never hidden. */
		if (box.width === 0 || box.height === 0 || (box.top < window.innerHeight && box.bottom > 0)) return;
		element.classList.add("is-pending");
		observer.observe(element);
	});
	/* Printing shows everything. */
	window.addEventListener("beforeprint", function () { targets.forEach(show); });
	if (!lively) return;
	/* Counters: a small whole number in the trust facts counts up once,
	 * ending on the number as written. */
	var counter = new IntersectionObserver(function (entries) {
		entries.forEach(function (entry) {
			if (!entry.isIntersecting) return;
			counter.unobserve(entry.target);
			var element = entry.target, end = parseInt(element.textContent, 10), start = null;
			function step(now) {
				if (start === null) start = now;
				var done = Math.min(1, (now - start) / 900);
				element.textContent = done < 1 ? String(Math.round(end * done)) : element.getAttribute("data-count");
				if (done < 1) window.requestAnimationFrame(step);
			}
			window.requestAnimationFrame(step);
		});
	}, {threshold: 0.5});
	Array.prototype.forEach.call(document.querySelectorAll(".facts dd, .hero__stat dd"), function (element) {
		var text = element.textContent.trim();
		if (!/^\d{1,3}$/.test(text)) return;
		element.setAttribute("data-count", text);
		counter.observe(element);
	});
})();
