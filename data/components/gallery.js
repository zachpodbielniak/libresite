/* Click to enlarge. Without this script each picture links to its full file. */
document.querySelectorAll("[data-lightbox]").forEach(function (list) {
	var dialog = list.parentNode.querySelector(".gallery__dialog");
	if (!dialog || typeof dialog.showModal !== "function") return;
	var big = dialog.querySelector("img");
	var caption = dialog.querySelector(".gallery__caption");
	list.querySelectorAll(".gallery__open").forEach(function (link) {
		link.addEventListener("click", function (event) {
			var image = link.querySelector("img");
			var figure = link.closest("figure");
			var text = figure && figure.querySelector("figcaption");
			event.preventDefault();
			big.src = link.getAttribute("href");
			big.alt = image ? image.alt : "";
			caption.textContent = text ? text.textContent : "";
			dialog.showModal();
		});
	});
	dialog.addEventListener("click", function (event) { if (event.target === dialog) dialog.close(); });
});
