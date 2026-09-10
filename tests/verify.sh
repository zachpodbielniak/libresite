#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/verify.sh' 'Example: bash tests/verify.sh'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
mkdir -p "$root/build/verification"
# Check that our warning oracle detects a known positive before testing logs.
printf '%s\n' 'fixture.c:1: warning: sentinel' > "$root/build/verification/control.log"
rg -q 'warning:|error:' "$root/build/verification/control.log"
for debug in 0 1
do
	log="$root/build/verification/build-$debug.log"
	if ! make -C "$root" -j4 clean all tests DEBUG="$debug" > "$log" 2>&1
	then
		cat "$log"
		exit 1
	fi
	if rg -n 'warning:|error:' "$log"
	then
		exit 1
	fi
done
shellcheck "$root"/tests/*.sh "$root"/tests/fixtures/*.sh
printf '%s\n' 'LIBRESITE VERIFIED'
