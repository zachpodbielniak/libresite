/* Open-now badge from the supplied schedule, in the business's time zone.
 * Without this script, or with an unknown zone, the badge stays hidden. */
document.querySelectorAll("[data-hours]").forEach(function (badge) {
	var days = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"];
	var rows = Array.prototype.slice.call(badge.parentNode.querySelectorAll("[data-day]"));
	var parts;
	try {
		parts = new Intl.DateTimeFormat("en-GB", { timeZone: badge.getAttribute("data-tz"), weekday: "short", hour: "2-digit", minute: "2-digit", hourCycle: "h23" }).formatToParts(new Date());
	} catch (error) { return; }
	function part(type) { var found = parts.filter(function (p) { return p.type === type; })[0]; return found ? found.value : ""; }
	function minutes(text) { var bits = text.split(":"); return Number(bits[0]) * 60 + Number(bits[1]); }
	var today = days.indexOf(part("weekday").slice(0, 3).toLowerCase());
	var now = Number(part("hour")) * 60 + Number(part("minute"));
	if (today < 0) return;
	var open = rows.some(function (row) {
		var day = days.indexOf(row.getAttribute("data-day"));
		var from = minutes(row.getAttribute("data-opens")), to = minutes(row.getAttribute("data-closes"));
		if (to <= from) return (day === today && now >= from) || ((day + 1) % 7 === today && now < to);
		return day === today && now >= from && now < to;
	});
	badge.textContent = badge.getAttribute(open ? "data-open" : "data-closed");
	badge.classList.toggle("is-open", open);
	badge.hidden = false;
});
