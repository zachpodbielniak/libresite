/* Carousel buttons. Without this script the row still scrolls and swipes. */
document.querySelectorAll("[data-carousel]").forEach(function (root) {
	var track = root.querySelector(".carousel__track");
	var controls = root.querySelector(".carousel__controls");
	if (!track || !controls) return;
	function step(direction) {
		var item = track.querySelector(".carousel__item");
		var width = item ? item.getBoundingClientRect().width : track.clientWidth;
		track.scrollBy({ left: direction * width, behavior: "auto" });
	}
	root.querySelector("[data-carousel-prev]").addEventListener("click", function () { step(-1); });
	root.querySelector("[data-carousel-next]").addEventListener("click", function () { step(1); });
	controls.hidden = false;
});
