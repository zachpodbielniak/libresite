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
 * inside it or running into its neighbours, but never below 16px (or its
 * own size, if smaller): past that the word wraps at its last possible
 * point, as it does without this script. */
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
	/* Entrances scale and shift sections while they run, or while they wait
	 * to scroll in; measured through them, a heading looks wider than its
	 * column and would shrink to nothing. The page is measured at rest:
	 * each running animation is lifted off and put back where it was. */
	function atRest(run) {
		var lifted = document.getAnimations ? document.getAnimations().filter(function (animation) {
			return animation.effect !== null;
		}).map(function (animation) {
			var effect = animation.effect;
			animation.effect = null;
			return [animation, effect];
		}) : [];
		try {
			run();
		} finally {
			lifted.forEach(function (pair) { pair[0].effect = pair[1]; });
		}
	}
	function fit() {
		atRest(fitAll);
		atRest(fitFold);
	}
	/* The home headline and first action belong on the first screen: when a
	 * tall header or picture pushes them below, the opening's picture gives
	 * up the difference, down to 40% of the screen or a 0.28 strip. */
	function fitFold() {
		var title = document.querySelector("main h1"), opening = title && title.closest("main > *");
		var pictures, action, round, over, img, box, height, next;
		if (!opening) return;
		pictures = Array.prototype.filter.call(opening.querySelectorAll("img"), function (each) {
			[each, each.parentElement].forEach(function (b) { b.style.height = b.style.minHeight = b.style.maxHeight = b.style.aspectRatio = ""; });
			return !/\.svg(\?|#|$)/.test(each.getAttribute("src") || "");
		});
		action = Array.prototype.filter.call(opening.querySelectorAll(".actions a.button"), function (link) {
			return link.offsetHeight > 0;
		})[0];
		for (round = 0; round < 3; round++) {
			over = Math.max(title.getBoundingClientRect().bottom, action ? action.getBoundingClientRect().bottom : 0) +
				window.scrollY + 16 - window.innerHeight;
			/* The tallest picture starting above the headline. */
			img = pictures.filter(function (each) {
				return each.offsetHeight > 0 && each.getBoundingClientRect().top < title.getBoundingClientRect().top;
			}).sort(function (a, b) { return b.offsetHeight - a.offsetHeight; })[0];
			if (over <= 0 || !img) return;
			/* A picture laid over its box takes the box's height. */
			box = getComputedStyle(img).position === "absolute" ? img.parentElement : img;
			height = box.getBoundingClientRect().height;
			next = Math.max(window.innerHeight * 0.4, box.getBoundingClientRect().width * 0.28, height - over);
			if (next >= height - 1) return;
			box.style.minHeight = "0";
			box.style.maxHeight = "none";
			box.style.aspectRatio = "auto";
			box.style.height = next + "px";
		}
	}
	function fitAll() {
		large().forEach(function (element) {
			var size, floor;
			element.style.fontSize = "";
			element.style.overflowWrap = "normal";
			if (element.clientWidth > 0 && tooWide(element)) {
				size = parseFloat(getComputedStyle(element).fontSize);
				floor = Math.max(size * 0.3, Math.min(size, 16));
				while (tooWide(element) && size * 0.94 >= floor) {
					size *= 0.94;
					element.style.fontSize = size + "px";
				}
				/* One more small step absorbs late font-metric drift. */
				element.style.fontSize = Math.max(size * 0.97, floor) + "px";
			}
			element.style.overflowWrap = "";
		});
	}
	var pending = false;
	fit();
	if (document.fonts && document.fonts.ready) document.fonts.ready.then(fit);
	window.addEventListener("load", fit);
	window.addEventListener("resize", function () {
		if (pending) return;
		pending = true;
		window.requestAnimationFrame(function () { pending = false; fit(); });
	});
})();

/* Supplied photos fit the box the layout gives them. A photo with several
 * sizes asks for the one its box needs at this screen's density, whatever
 * share of the screen its template guessed in sizes; a photo the box would
 * crop to a strip, under 40 percent of itself (a portrait in a wide band),
 * is shown whole on a mat instead. Without this script the template's guess and the crop
 * stand. */
(function () {
	function fit() {
		Array.prototype.forEach.call(document.querySelectorAll("main img[width][height]"), function (img) {
			var width = Number(img.getAttribute("width")), height = Number(img.getAttribute("height"));
			var boxWidth, boxHeight, ratio, shape, cover;
			if (/\.svg(\?|#|$)/.test(img.getAttribute("src")) || !width || !height) return;
			img.classList.remove("is-matted");
			boxWidth = img.offsetWidth;
			boxHeight = img.offsetHeight;
			if (boxWidth < 2 || boxHeight < 2) return;
			ratio = width / height;
			shape = boxWidth / boxHeight;
			cover = getComputedStyle(img).objectFit === "cover";
			if (cover && (ratio > shape ? shape / ratio : ratio / shape) < 0.4) {
				img.classList.add("is-matted");
				cover = false;
			}
			/* A covering photo spans the box along the side its shape needs. */
			if (img.hasAttribute("srcset"))
				img.sizes = Math.ceil(cover ? Math.max(boxWidth, boxHeight * ratio) : boxWidth) + "px";
		});
	}
	var pending = false;
	fit();
	window.addEventListener("resize", function () {
		if (pending) return;
		pending = true;
		window.requestAnimationFrame(function () { pending = false; fit(); });
	});
})();
