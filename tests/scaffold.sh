#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/scaffold.sh BINARY' 'Example: bash tests/scaffold.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
binary=$(realpath "${1:?binary required}")
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
cat > "$test_dir/native.yaml" <<'YAML'
version: 1
name: scaffold-test
site: {brief: Native-only static scaffold}
seed_artifacts:
  - {path: index.html, content: '<!doctype html><title>Fixture</title><h1>Fixture</h1>'}
pipeline:
  steps:
    - {id: scaffold, type: scaffold, options: {profile: static}}
    - id: check
      type: validate
      options:
        required: [404.html, privacy.html, terms.html]
YAML
# No provider executable is configured: all generation is native.
(cd /tmp && "$binary" -r "$test_dir/native.yaml" --preview scaffold) > "$test_dir/preview"
rg -Fq 'privacy.html' "$test_dir/preview"
# Static sites ship no build, test or agent files.
for developer_file in Makefile AGENTS.md docs/contact.org tests/scaffold.sh
do
	if rg -Fq "\"$developer_file\"" "$test_dir/preview"
	then
		exit 1
	fi
done
"$binary" --list-modules | rg -x scaffold
"$binary" -r "$test_dir/native.yaml" --plan > "$test_dir/plan"
rg -q $'scaffold\tscaffold\tnative\t\(none\)' "$test_dir/plan"
(cd /tmp && "$binary" -r "$test_dir/native.yaml" -o "$test_dir/site")
for page in 404.html privacy.html terms.html
do
	test -s "$test_dir/site/$page"
done
test ! -e "$test_dir/site/Makefile"
test ! -e "$test_dir/site/AGENTS.md"
# Invalid native options fail preflight before workspace creation.
for options in 'profile: unknown' 'profile: true' 'overwrite: true'
do
	sed "s/profile: static/$options/" "$test_dir/native.yaml" > "$test_dir/invalid.yaml"
	if "$binary" -r "$test_dir/invalid.yaml" -o "$test_dir/rejected" > "$test_dir/invalid.log" 2>&1
	then
		exit 1
	fi
	test ! -e "$test_dir/rejected.libresite-work"
	rg -q "Step 'scaffold':" "$test_dir/invalid.log"
done
# Later model previews include the native files, without any provider call.
sed 's/type: validate/type: frontend/' "$test_dir/native.yaml" > "$test_dir/generate.yaml"
"$binary" -r "$test_dir/generate.yaml" --preview check > "$test_dir/prompt"
rg -Fq 'privacy.html' "$test_dir/prompt"
rg -Fq 'CONTACT FORMS' "$test_dir/prompt"
# A completed output is never overwritten, even with resume requested.
"$binary" -r "$test_dir/native.yaml" -o "$test_dir/site" --resume > "$test_dir/resume.log" 2>&1 && exit 1
rg -q 'already exists' "$test_dir/resume.log"
printf '%s\n' 'SCAFFOLD VERIFIED'
