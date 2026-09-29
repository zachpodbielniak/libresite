#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Screenshot every page of a published site at phone and desktop widths, for
# a person to review. An operator tool: the builder never runs it, and it
# serves the site on loopback only.
set -euo pipefail

usage() {
	cat <<'USAGE'
Usage: scripts/screenshots.sh SITE-DIR OUT-DIR [PORT]

Serve SITE-DIR on 127.0.0.1 and save phone (390x844) and desktop
(1440x1000) full-width screenshots of every top-level .html page to
OUT-DIR, which must not exist. Needs python3 and firefox.

Examples:
  scripts/screenshots.sh ./my-site ./my-site-shots
  scripts/screenshots.sh ./my-site /tmp/shots 8899

Options:
  -h, --help     Show this help
  --license      Print the license notice
USAGE
}

case "${1:-}" in
	-h|--help) usage; exit 0 ;;
	--license) echo "libresite screenshots.sh is licensed AGPL-3.0-or-later."; exit 0 ;;
esac
if [[ $# -lt 2 || $# -gt 3 ]]; then
	usage >&2
	exit 2
fi
site=$1 out=$2 port=${3:-8899}
if [[ ! -f "$site/index.html" ]]; then
	echo "screenshots.sh: $site has no index.html" >&2
	exit 2
fi
if [[ -e "$out" ]]; then
	echo "screenshots.sh: $out already exists" >&2
	exit 2
fi
for tool in python3 firefox; do
	command -v "$tool" > /dev/null || { echo "screenshots.sh: $tool is required" >&2; exit 2; }
done
mkdir -p "$out"
profile=$(mktemp -d)
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$site" > /dev/null 2>&1 &
server=$!
trap 'kill "$server" 2> /dev/null || true; rm -rf -- "$profile"' EXIT
sleep 1
count=0
for page in "$site"/*.html; do
	name=$(basename "$page" .html)
	for size in phone:390,844 desktop:1440,1000; do
		label=${size%%:*}
		timeout 60 firefox --headless --no-remote --profile "$profile" --window-size="${size#*:}" \
			--screenshot "$out/$name-$label.png" "http://127.0.0.1:$port/$name.html" > /dev/null 2>&1 || \
			echo "screenshots.sh: $name at $label failed" >&2
	done
	count=$((count + 1))
done
echo "Saved $((count * 2)) screenshots of $count pages to $out"
