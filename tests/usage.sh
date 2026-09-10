#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/usage.sh [BINARY]' 'Example: bash tests/usage.sh build/release/libresite'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
binary=$(realpath "${1:-$root/build/release/libresite}")
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
cat > "$test_dir/usage.json" <<'JSON'
{"version":1,"usage_complete":true,"usage":[{"step":"generate","provider":"fixture","model":"model","success":false,"attempts":2,"retries":1,"bytes_in":1000000,"bytes_out":2000000,"elapsed_us":500000}]}
JSON
cat > "$test_dir/rates.yaml" <<'YAML'
version: 1
currency: USD
rates:
  - provider: fixture
    model: model
    per_million_input_bytes: 2
    per_million_output_bytes: 3
    per_second: 4
YAML
# No provider executable or diagnostic file is involved in offline pricing.
(cd /tmp && "$binary" --price-usage "$test_dir/usage.json" --rates "$test_dir/rates.yaml") > "$test_dir/price.json" 2> "$test_dir/stderr"
[[ ! -s "$test_dir/stderr" ]]
rg -q '"total":10([.,}])' "$test_dir/price.json"
rg -q '"amount":10([.,}])' "$test_dir/price.json"
rg -q '"success":false' "$test_dir/price.json"
for flag in --validate --resume --license
do
	if "$binary" --price-usage "$test_dir/usage.json" --rates "$test_dir/rates.yaml" "$flag" > /dev/null 2>&1
	then
		exit 1
	fi
done
if "$binary" --price-usage "$test_dir/usage.json" > /dev/null 2>&1
then
	exit 1
fi
sed 's/"usage_complete":true/"usage_complete":false/' "$test_dir/usage.json" > "$test_dir/incomplete.json"
if "$binary" --price-usage "$test_dir/incomplete.json" --rates "$test_dir/rates.yaml" > "$test_dir/rejected" 2>&1
then
	exit 1
fi
rg -q 'Historical usage is incomplete' "$test_dir/rejected"
printf '%s\n' 'USAGE VERIFIED'
