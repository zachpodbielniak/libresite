#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Optional real-library probes. Default builder tests need no ORM/HTTP libraries.
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/stack-api.sh' 'Example: git submodule update --init --recursive; make stack-tests'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
	'') ;;
	*) printf '%s\n' 'Unknown option; use --help' >&2; exit 2 ;;
esac
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
out="$root/build/stack-api"
mkdir -p "$out"
trap 'printf "Stack API check failed; dependency build log: %s\n" "$out/deps.log" >&2' ERR
for library in yaml-glib htmx-glib orm-glib
do
	if [[ ! -f "$root/deps/$library/Makefile" ]]
	then
		printf 'Initialize pinned submodule deps/%s before running stack-tests.\n' "$library" >&2
		exit 1
	fi
	pin=$(git -C "$root" rev-parse "HEAD:deps/$library")
	if [[ "$(git -C "$root/deps/$library" rev-parse HEAD)" != "$pin" ]]
	then
		printf 'deps/%s must be at pinned commit %s.\n' "$library" "$pin" >&2
		exit 1
	fi
	if [[ -n "$(git -C "$root/deps/$library" status --porcelain --untracked-files=normal)" ]]
	then
		printf 'deps/%s has local changes; preserve them and use a clean pinned checkout for this probe.\n' "$library" >&2
		exit 1
	fi
	# Catch a catalog pin drifting independently of the code tested here.
	rg -Fq "$pin" "$root/data/prompts.yaml"
done
# Build only the pinned archives needed by the probe, without global installs.
{
	make -C "$root/deps/yaml-glib" DEBUG=0 GIR=0 build/release/libyaml-glib-1.0.a
	make -C "$root/deps/htmx-glib" DEBUG=0 static
	# ORM does not track changed provider flags in its object dependencies.
	make -C "$root/deps/orm-glib" DEBUG=0 clean
	make -C "$root/deps/orm-glib" DEBUG=0 ENABLE_SQLITE=1 ENABLE_POSTGRES=0 ENABLE_MYSQL=0 lib
} > "$out/deps.log" 2>&1
if rg -n 'warning:|error:' "$out/deps.log"
then
	exit 1
fi
read -r -a flags <<< "$(pkg-config --cflags --libs glib-2.0 gobject-2.0 gio-2.0 json-glib-1.0 libsoup-3.0 yaml-0.1 sqlite3)"
gcc -std=gnu89 -Wall -Wextra -Werror -Wformat=2 -Wshadow -Wstrict-prototypes \
	-Wmissing-prototypes -Wdeclaration-after-statement \
	-I"$root/deps/yaml-glib/src" -I"$root/deps/yaml-glib/build/release" \
	-I"$root/deps/htmx-glib/src" -I"$root/deps/htmx-glib/build/release" \
	-I"$root/deps/orm-glib/src" -I"$root/deps/orm-glib/build/release" \
	"$root/tests/stack-api.c" \
	"$root/deps/yaml-glib/build/release/libyaml-glib-1.0.a" \
	"$root/deps/htmx-glib/build/release/libhtmx-glib-1.0.a" \
	"$root/deps/orm-glib/build/release/liborm-glib-0.2.a" \
	"${flags[@]}" -o "$out/probe"
G_DEBUG=fatal-warnings "$out/probe"
printf '%s\n' 'STACK API VERIFIED'
