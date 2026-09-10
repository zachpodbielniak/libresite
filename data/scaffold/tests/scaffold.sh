#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/scaffold.sh' 'Example: make tests'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
	'') ;;
	*) printf '%s\n' "Unknown option: $1" >&2; exit 2 ;;
esac
# Anchor checks to this project, including when invoked from another directory.
CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
for path in index.html 404.html privacy.html terms.html Makefile AGENTS.md docs/contact.org
do
	if [[ ! -s "$path" ]]
	then
		printf 'Missing or empty required file: %s\n' "$path" >&2
		exit 1
	fi
done
printf '%s\n' 'Static scaffold files present; browser, HTTP, legal review and application tests have not run.'
