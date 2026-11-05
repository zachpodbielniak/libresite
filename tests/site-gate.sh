#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Proves each check of scripts/site-gate.sh: the good fixture site passes,
# and each case page (the good home page with one defect) fails exactly the
# check its name starts with. Needs firefox and geckodriver, or WEBDRIVER_URL.
set -euo pipefail
case ${1:-} in
-h|--help)
	printf '%s\n' 'Usage: tests/site-gate.sh [GATE]' 'Runs GATE (default scripts/site-gate.sh) on tests/fixtures/site-gate: the good site must pass,' 'and each cases/NAME.html must fail only the check named by NAME up to its first dash.'
	exit 0 ;;
--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
gate=${1:-$root/scripts/site-gate.sh}
fixtures=$root/tests/fixtures/site-gate
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
fail () { printf 'site-gate test: %s\n' "$*" >&2; exit 1; }

# Bad arguments are usage errors, not gate failures.
status=0
bash "$gate" "$work/missing" > /dev/null 2>&1 || status=$?
[[ $status == 2 ]] || fail "a missing site exited $status, expected 2"

# The good site alone passes, with a plain PASS line.
cp -r "$fixtures/good" "$work/good"
status=0
bash "$gate" "$work/good" > "$work/good.out" 2>&1 || status=$?
cat "$work/good.out"
[[ $status == 0 ]] || fail "the good site exited $status"
grep -q '^PASS good' "$work/good.out" || fail 'no PASS line for the good site'

# Every case in one run: one browser for all of them.
sites=()
for page in "$fixtures"/cases/*.html; do
	name=$(basename "$page" .html)
	cp -r "$fixtures/good" "$work/$name"
	cp "$page" "$work/$name/index.html"
	sites+=("$work/$name")
done
status=0
bash "$gate" --json "$work/cases.json" "${sites[@]}" > "$work/cases.out" 2>&1 || status=$?
cat "$work/cases.out"
[[ $status == 1 ]] || fail "the case sites exited $status, expected 1"
for site in "${sites[@]}"; do
	name=$(basename "$site")
	want=${name%%-*}
	got=$(jq -r --arg site "$name" '.[] | select(.site == $site) | .failed | join(" ")' "$work/cases.json")
	[[ $got == "$want" ]] || fail "$name failed [$got], expected [$want]"
	# One plain line per failed check, under the site's heading.
	sed -n "/^FAIL $name\$/,/^\(PASS\|FAIL\) /p" "$work/cases.out" | grep -q "^  $want: " ||
		fail "$name has no plain '$want:' line"
done
jq -e 'all(.[]; .seconds < 60)' "$work/cases.json" > /dev/null || fail 'a site took a minute or more'

# --scheme dark: the visitor's system asks for dark. A site whose dark
# scheme loses its contrast passes in light and fails in dark.
cp -r "$fixtures/good" "$work/dim"
printf '%s\n' '@media (prefers-color-scheme: dark) { body, main, section { background: #111111 !important; } p { color: #333333 !important; } }' >> "$work/dim/styles.css"
status=0
bash "$gate" "$work/dim" > "$work/dim-light.out" 2>&1 || status=$?
cat "$work/dim-light.out"
[[ $status == 0 ]] || fail "the dim site failed in light ($status)"
status=0
bash "$gate" --scheme dark --json "$work/dim.json" "$work/dim" > "$work/dim-dark.out" 2>&1 || status=$?
cat "$work/dim-dark.out"
[[ $status == 1 ]] || fail "the dim site exited $status in dark, expected 1"
[[ $(jq -r '.[0].failed | join(" ")' "$work/dim.json") == contrast ]] || fail 'the dim site did not fail contrast alone in dark'
grep -q '^FAIL dim (dark)' "$work/dim-dark.out" || fail 'the dark run does not name its scheme'
status=0
bash "$gate" --scheme dusk "$work/dim" > /dev/null 2>&1 || status=$?
[[ $status == 2 ]] || fail "an unknown scheme exited $status, expected 2"
printf '%s\n' 'SITE GATE VERIFIED'
