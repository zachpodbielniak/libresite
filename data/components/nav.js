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

/* Large text (headings, figures, prices, pull quotes) whose longest word
 * is wider than its column shrinks until the word fits, instead of breaking
 * inside it or running into its neighbours. Without this script such a
 * word wraps at its last possible point. */
(function () {
	function large() {
		return Array.prototype.filter.call(document.querySelectorAll("main *, .site-footer .brand"), function (element) {
			var node, text = false;
			for (node = element.firstChild; node; node = node.nextSibling)
				if (node.nodeType === 3 && node.textContent.trim()) text = true;
			if (!text) return false;
			var style = getComputedStyle(element);
			return style.display !== "inline" && (/^H[1-4]$/.test(element.tagName) || parseFloat(style.fontSize) >= 22);
		});
	}
	/* Too wide: a word overflows the element's box, or the box itself has
	 * been stretched past its parent's content width by that word. */
	function tooWide(element) {
		var right = element.getBoundingClientRect().right, ancestor = element.parentElement, depth;
		if (element.scrollWidth > element.clientWidth + 1) return true;
		/* A box sized to its content can outgrow the column that holds it.
		 * Only the right side counts: headings may hang left on purpose. */
		for (depth = 0; ancestor && ancestor.tagName !== "MAIN" && depth < 6; depth++, ancestor = ancestor.parentElement) {
			var box = ancestor.getBoundingClientRect(), style = getComputedStyle(ancestor);
			/* Scrolling rows and clipped bands overflow by design. */
			if (style.overflowX !== "visible") return false;
			var edge = box.right - parseFloat(style.paddingRight) - parseFloat(style.borderRightWidth);
			if (ancestor.clientWidth > 0 && right > edge + 1) return true;
		}
		return false;
	}
	function fit() {
		large().forEach(function (element) {
			var size, floor;
			element.style.fontSize = "";
			element.style.overflowWrap = "normal";
			if (element.clientWidth > 0 && tooWide(element)) {
				size = parseFloat(getComputedStyle(element).fontSize);
				floor = size * 0.3;
				while (tooWide(element) && size > floor) {
					size *= 0.94;
					element.style.fontSize = size + "px";
				}
				/* One more small step absorbs late font-metric drift. */
				element.style.fontSize = size * 0.97 + "px";
			}
			element.style.overflowWrap = "";
		});
	}
	var pending = false;
	fit();
	if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
	window.addEventListener("resize", function () {
		if (pending) return;
		pending = true;
		window.requestAnimationFrame(function () { pending = false; fit(); });
	});
})();
