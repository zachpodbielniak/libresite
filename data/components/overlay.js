/* Overlays (docs/components.org, "Skeletons, motion and overlays"): the
 * announcement the visitor may dismiss for the visit, the quote form that
 * opens in a dialog from every link to the form, and the lightbox for the
 * owner's photos. Dialogs are native: the page behind is inert, Escape
 * closes them and the focus goes back to what opened them. Without this
 * script the bar stays, the links lead to the form's page and the photos
 * open on their own. The wide sticky bar rests while the closing band or
 * the footer is in view; without this script it simply stays. */
(function () {
	var action = document.querySelector(".design.overlay--sticky-bar .action-bar");
	if (action && "IntersectionObserver" in window && window.matchMedia("(min-width: 48rem)").matches) {
		var ends = Array.prototype.slice.call(document.querySelectorAll("main > .cta:last-of-type, .site-footer"));
		var seen = [];
		var watch = new IntersectionObserver(function (entries) {
			entries.forEach(function (entry) {
				var at = seen.indexOf(entry.target);
				if (entry.isIntersecting && at < 0) seen.push(entry.target);
				if (!entry.isIntersecting && at >= 0) seen.splice(at, 1);
			});
			action.classList.toggle("is-resting", seen.length > 0);
		});
		ends.forEach(function (end) { watch.observe(end); });
	}
	var bar = document.querySelector(".announce");
	if (bar) {
		var key = "ls-announce:" + bar.textContent.replace(/\s+/g, " ").trim();
		var dismiss = bar.querySelector(".announce__close");
		try {
			if (window.sessionStorage.getItem(key) === "1") bar.hidden = true;
		} catch (error) { /* no storage: the bar stays */ }
		if (dismiss) {
			dismiss.hidden = false;
			dismiss.addEventListener("click", function () {
				var next = document.querySelector(".site-header a");
				bar.hidden = true;
				try { window.sessionStorage.setItem(key, "1"); } catch (error) { /* the bar returns next page */ }
				if (next) next.focus();
			});
		}
	}
	function modal(dialog) {
		var opener = null;
		dialog.addEventListener("close", function () {
			if (opener && document.contains(opener)) opener.focus();
		});
		dialog.addEventListener("click", function (event) {
			if (event.target === dialog) dialog.close();
		});
		Array.prototype.forEach.call(dialog.querySelectorAll(".dialog-close"), function (button) {
			button.addEventListener("click", function () { dialog.close(); });
		});
		return function (from) {
			opener = from;
			if (!dialog.open) dialog.showModal();
		};
	}
	var quote = document.getElementById("quote-dialog");
	if (quote && typeof quote.showModal === "function") {
		var openQuote = modal(quote);
		document.addEventListener("click", function (event) {
			var link = event.target.closest ? event.target.closest('[data-dialog="quote-dialog"]') : null;
			if (!link) return;
			event.preventDefault();
			openQuote(link);
			var field = quote.querySelector("input:not([tabindex='-1']), textarea, select");
			if (field) field.focus();
		});
	}
	var box = document.getElementById("lightbox");
	var zooms = Array.prototype.slice.call(document.querySelectorAll("[data-lightbox]"));
	if (box && zooms.length > 0 && typeof box.showModal === "function") {
		var figure = box.querySelector(".lightbox__figure"), at = 0;
		var image = document.createElement("img"), caption = document.createElement("figcaption");
		var openBox = modal(box);
		figure.appendChild(image);
		figure.appendChild(caption);
		function show(index) {
			var link = zooms[index], thumb = link.querySelector("img"), note = link.closest("figure");
			at = (index + zooms.length) % zooms.length;
			link = zooms[at];
			thumb = link.querySelector("img");
			note = link.closest("figure") ? link.closest("figure").querySelector("figcaption") : null;
			image.src = link.getAttribute("href");
			image.alt = thumb ? thumb.alt : "";
			caption.textContent = note ? note.textContent : "";
		}
		zooms.forEach(function (link, index) {
			link.addEventListener("click", function (event) {
				event.preventDefault();
				show(index);
				openBox(link);
				box.querySelector(".lightbox__next").focus();
			});
		});
		box.querySelector(".lightbox__prev").addEventListener("click", function () { show(at - 1); });
		box.querySelector(".lightbox__next").addEventListener("click", function () { show(at + 1); });
		box.addEventListener("keydown", function (event) {
			if (event.key === "ArrowLeft") show(at - 1);
			else if (event.key === "ArrowRight") show(at + 1);
		});
	}
})();
