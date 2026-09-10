#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/packages.sh BINARY' 'Example: bash tests/packages.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
# This script owns only the newly-created temporary directory.
trap 'rm -rf -- "$test_dir"' EXIT
for sku in trades professional-services restaurant nonprofit studio small-saas application landing
do
	"$binary" -r "$root/recipes/$sku.yaml" --seal-package "$test_dir/$sku"
	cmp "$root/recipes/$sku.yaml" "$test_dir/$sku/recipe.yaml"
	cmp "$root/data/prompts.yaml" "$test_dir/$sku/prompts.yaml"
	if [[ "$sku" == application ]]
	then
		[[ ! -e "$test_dir/$sku/scaffold.json" ]]
	else
		[[ -s "$test_dir/$sku/scaffold.json" ]]
		"$binary" --package "$test_dir/$sku" --preview scaffold > "$test_dir/scaffold-preview"
		cmp <(printf '%s\n' "$(< "$test_dir/$sku/scaffold.json")") "$test_dir/scaffold-preview"
	fi
	(cd /tmp && "$binary" --package "$test_dir/$sku" --validate)
	(cd /tmp && "$binary" --package "$test_dir/$sku" --preview design) > "$test_dir/preview"
	rg -Fq 'FLAGSHIP QUALITY DEFAULT' "$test_dir/preview"
	rg -Fq 'APPLICATION SKU BOUNDARY' "$test_dir/preview"
	rg -Fq 'docs/acceptance.org' "$test_dir/preview"
	# A second seal cannot replace an existing sale, including an empty target.
	if "$binary" -r "$root/recipes/$sku.yaml" --seal-package "$test_dir/$sku" > "$test_dir/error" 2>&1
	then
		exit 1
	fi
	cmp "$root/recipes/$sku.yaml" "$test_dir/$sku/recipe.yaml"
done
mkdir "$test_dir/empty"
if "$binary" -r "$root/recipes/trades.yaml" --seal-package "$test_dir/empty" > "$test_dir/error" 2>&1
then
	exit 1
fi
# Package loading must never silently accept replacement inputs or mixed modes.
for option in --recipe --prompts --seal-package --from-form
do
	if "$binary" --package "$test_dir/studio" "$option" "$root/recipes/studio.yaml" --validate > "$test_dir/error" 2>&1
	then
		exit 1
	fi
done
printf '\n# Changed catalog\n' >> "$test_dir/trades/prompts.yaml"
if "$binary" --package "$test_dir/trades" -o "$test_dir/site" > "$test_dir/error" 2>&1
then
	exit 1
fi
rg -Fq 'catalog SHA-256 mismatch' "$test_dir/error"
[[ ! -e "$test_dir/site.libresite-work" ]]
# Missing native inputs are an error, never a fallback to installed templates.
mv "$test_dir/studio/scaffold.json" "$test_dir/saved-scaffold"
if "$binary" --package "$test_dir/studio" -o "$test_dir/native-site" > "$test_dir/error" 2>&1
then
	exit 1
fi
[[ ! -e "$test_dir/native-site.libresite-work" ]]
mv "$test_dir/saved-scaffold" "$test_dir/studio/scaffold.json"
# Simulate an intact older archive with different native template contents.
printf '%s' '{"files":[{"path":"Makefile","content":"Archived native template 2026"}]}' > "$test_dir/studio/scaffold.json"
read -r native_digest _ < <(sha256sum "$test_dir/studio/scaffold.json")
sed -i "s/^scaffold_sha256:.*/scaffold_sha256: '$native_digest'/" "$test_dir/studio/package.yaml"
"$binary" --package "$test_dir/studio" --preview scaffold > "$test_dir/native-preview"
rg -Fq 'Archived native template 2026' "$test_dir/native-preview"
# A caller cannot relabel a different stack as the supported application SKU.
for stack in react tailwind
do
	sed "s/stack: c-glib/stack: $stack/" "$root/recipes/application.yaml" > "$test_dir/wrong.yaml"
	if "$binary" -r "$test_dir/wrong.yaml" --seal-package "$test_dir/wrong" > "$test_dir/error" 2>&1
	then
		exit 1
	fi
	rg -Fq 'stack c-glib' "$test_dir/error"
	[[ ! -e "$test_dir/wrong" ]]
done
"$binary" --package "$test_dir/application" --preview backend > "$test_dir/application-preview"
for contract in c-glib htmx-glib orm-glib
do
	rg -Fq "$contract" "$test_dir/application-preview"
done
printf '%s\n' 'PACKAGES VERIFIED'
