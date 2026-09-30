/* share: fill share links from the visitor's own address when the site
 * has none, and offer the device share sheet and a copy button where the
 * browser supports them. Nothing loads from a network until a visitor
 * follows a link. */
(function () {
	var lists = document.querySelectorAll(".share__list");
	if (!lists.length) return;
	var url = location.href.split("#")[0];
	lists.forEach(function (list) {
		var status = list.parentNode.querySelector(".share__status");
		list.querySelectorAll("[data-share]").forEach(function (link) {
			if (link.hasAttribute("href")) return;
			var title = link.getAttribute("data-share-title") || document.title;
			link.setAttribute("href", link.getAttribute("data-share")
				.split("{url}").join(encodeURIComponent(url))
				.split("{title}").join(encodeURIComponent(title)));
			link.parentNode.hidden = false;
		});
		var native = list.querySelector("[data-share-native]");
		if (native && navigator.share) {
			native.parentNode.hidden = false;
			native.addEventListener("click", function () {
				navigator.share({title: native.getAttribute("data-share-title") || document.title, url: url})
					.catch(function () {});
			});
		}
		var copy = list.querySelector("[data-share-copy]");
		if (copy && navigator.clipboard && window.isSecureContext) {
			copy.parentNode.hidden = false;
			copy.addEventListener("click", function () {
				navigator.clipboard.writeText(url).then(function () {
					if (status) status.textContent = copy.getAttribute("data-copied");
					copy.setAttribute("data-done", "");
				}).catch(function () {});
			});
		}
	});
})();
