/* The host owns availability and the booking. This script only draws the
 * public slots and leaves the ordinary booking link when that list cannot
 * be read. Venture's GET ?format=json body is a JSON array of
 * {start, end, label, day}. A {"slots":[...]} object is accepted too. */
document.querySelectorAll(".booking-calendar [data-booking-url]").forEach(function (root) {
	var url = root.getAttribute("data-booking-url");
	var app = root.querySelector(".booking-calendar__app");
	var fallback = root.querySelector(".booking-calendar__fallback");
	var days = root.querySelector("[data-booking-days]");
	var monthLabel = root.querySelector("[data-booking-month]");
	var status = root.querySelector("[data-booking-status]");
	var times = root.querySelector("[data-booking-times]");
	var timeList = root.querySelector("[data-booking-time-list]");
	var form = root.querySelector("[data-booking-form]");
	var confirmed = root.querySelector("[data-booking-confirmed]");
	var slots = [];
	var selected = "";
	var offset = 0;
	var origin = new Date();
	origin = new Date(origin.getFullYear(), origin.getMonth(), 1);
	if (!url || !app || !fallback || !days || !monthLabel || !status || !times || !timeList || !form || !confirmed || typeof fetch === "undefined")
		return;
	function pad(value) { return String(value).padStart(2, "0"); }
	function monthKey(date) { return date.getFullYear() + "-" + pad(date.getMonth() + 1); }
	function dateLabel(day) {
		return new Date(day + "T12:00:00").toLocaleDateString(undefined, { weekday: "long", month: "long", day: "numeric" });
	}
	function usable(slot) {
		return slot && /^\d{4}-\d\d-\d\d$/.test(slot.day) && typeof slot.start === "string" && slot.start !== "" && typeof slot.label === "string";
	}
	function listFrom(data) {
		if (Array.isArray(data)) return data;
		if (data && Array.isArray(data.slots)) return data.slots;
		return null;
	}
	function showMonth() {
		var current = new Date(origin.getFullYear(), origin.getMonth() + offset, 1);
		var count = new Date(current.getFullYear(), current.getMonth() + 1, 0).getDate();
		var prefix = current.getDay();
		var key = monthKey(current);
		var i;
		monthLabel.textContent = current.toLocaleDateString(undefined, { month: "long", year: "numeric" });
		days.replaceChildren();
		for (i = 0; i < prefix; i++) days.appendChild(document.createElement("span"));
		for (i = 1; i <= count; i++) {
			var day = key + "-" + pad(i);
			var available = slots.some(function (slot) { return slot.day === day; });
			var button = document.createElement("button");
			button.type = "button";
			button.textContent = String(i);
			button.dataset.day = day;
			button.disabled = !available;
			button.setAttribute("aria-label", dateLabel(day) + (available ? ", open times" : ", no open times"));
			button.setAttribute("aria-pressed", day === selected ? "true" : "false");
			days.appendChild(button);
		}
		root.querySelector("[data-booking-prev]").disabled = offset <= 0;
		root.querySelector("[data-booking-next]").disabled = !slots.some(function (slot) { return slot.day.slice(0, 7) > key; });
	}
	function chooseDay(day) {
		selected = day;
		showMonth();
		timeList.replaceChildren();
		form.hidden = true;
		times.hidden = false;
		root.querySelector("[data-booking-day-title]").textContent = dateLabel(day);
		slots.filter(function (slot) { return slot.day === day; }).forEach(function (slot) {
			var button = document.createElement("button");
			button.type = "button";
			button.textContent = slot.label;
			button.dataset.start = slot.start;
			button.setAttribute("aria-pressed", "false");
			timeList.appendChild(button);
		});
		status.textContent = "Choose an open time.";
	}
	days.addEventListener("click", function (event) {
		var button = event.target.closest("button[data-day]");
		if (button && !button.disabled) chooseDay(button.dataset.day);
	});
	timeList.addEventListener("click", function (event) {
		var button = event.target.closest("button[data-start]");
		if (!button) return;
		timeList.querySelectorAll("button").forEach(function (item) {
			item.setAttribute("aria-pressed", item === button ? "true" : "false");
		});
		form.elements.start.value = button.dataset.start;
		root.querySelector("[data-booking-picked]").textContent = dateLabel(selected) + " at " + button.textContent;
		form.hidden = false;
		status.textContent = "Enter your name and email to confirm.";
		form.elements.name.focus();
	});
	function moveMonth(delta) {
		offset += delta;
		selected = "";
		times.hidden = true;
		form.hidden = true;
		showMonth();
		status.textContent = "Choose an open day.";
	}
	root.querySelector("[data-booking-prev]").addEventListener("click", function () { moveMonth(-1); });
	root.querySelector("[data-booking-next]").addEventListener("click", function () { moveMonth(1); });
	function confirmBooking() {
		form.hidden = true;
		times.hidden = true;
		days.hidden = true;
		root.querySelector(".booking-calendar__nav").hidden = true;
		root.querySelector(".booking-calendar__weekdays").hidden = true;
		confirmed.textContent = "Your booking is confirmed. Check your email for the details.";
		confirmed.hidden = false;
		confirmed.focus();
		status.textContent = "";
	}
	function release(message) {
		status.textContent = message;
		form.querySelector("[type=submit]").disabled = false;
	}
	form.addEventListener("submit", function (event) {
		var submit = form.querySelector("[type=submit]");
		var fields = new FormData(form);
		event.preventDefault();
		if (!form.checkValidity()) {
			form.reportValidity();
			return;
		}
		submit.disabled = true;
		status.textContent = "Confirming your booking…";
		fetch(url, {
			method: "POST",
			headers: { "Content-Type": "application/json", "Accept": "application/json" },
			body: JSON.stringify({
				start: fields.get("start"),
				name: fields.get("name"),
				email: fields.get("email"),
				notes: fields.get("notes")
			})
		}).then(function (reply) {
			if (reply.status === 409) return { taken: true };
			if (!reply.ok) return { failed: true };
			return { ok: true };
		}).then(function (result) {
			var start;
			if (!result || result.failed) {
				release("Booking could not be confirmed. Please try again.");
				return;
			}
			if (result.taken) {
				start = form.elements.start.value;
				slots = slots.filter(function (slot) { return slot.start !== start; });
				if (!slots.some(function (slot) { return slot.day === selected; })) {
					times.hidden = true;
					form.hidden = true;
					selected = "";
					showMonth();
					release(slots.length ? "That time was just taken. Choose another day." : "No open times are available right now.");
					return;
				}
				chooseDay(selected);
				release("That time was just taken. Choose another.");
				return;
			}
			confirmBooking();
		}).catch(function () {
			release("Booking could not be confirmed. Please try again.");
		});
	});
	fetch(url + (url.indexOf("?") < 0 ? "?" : "&") + "format=json", { headers: { "Accept": "application/json" } })
		.then(function (reply) {
			if (!reply.ok) throw new Error("unavailable");
			return reply.json();
		})
		.then(function (data) {
			var list = listFrom(data);
			var parts;
			if (!list) throw new Error("unavailable");
			slots = list.filter(usable);
			slots.sort(function (a, b) { return a.day < b.day ? -1 : a.day > b.day ? 1 : 0; });
			if (slots.length) {
				parts = slots[0].day.split("-");
				origin = new Date(Number(parts[0]), Number(parts[1]) - 1, 1);
			}
			app.hidden = false;
			fallback.hidden = true;
			showMonth();
			status.textContent = slots.length ? "Choose an open day." : "No open times are available right now.";
		})
		.catch(function () { fallback.hidden = false; });
});
