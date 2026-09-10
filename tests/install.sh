#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
case "${1-}" in
	-h|--help) printf '%s\n' 'Usage: bash tests/install.sh' 'Example: bash tests/install.sh'; exit 0 ;;
	--license) printf '%s\n' 'AGPL-3.0-or-later'; exit 0 ;;
esac
root=$(realpath "$(dirname -- "${BASH_SOURCE[0]}")/..")
test_dir=$(mktemp -d)
trap 'rm -rf -- "$test_dir"' EXIT
if ! make -C "$root" install DESTDIR="$test_dir" PREFIX=/usr > "$test_dir/install.log" 2>&1
then
	cat "$test_dir/install.log"
	exit 1
fi
if rg -n 'warning:|error:' "$test_dir/install.log"
then
	exit 1
fi
binary="$test_dir/usr/bin/libresite"
for recipe in "$root"/recipes/*.yaml
do
	(cd /tmp && "$binary" --recipe "$recipe" --validate)
	(cd /tmp && "$binary" --recipe "$recipe" --plan) > "$test_dir/plan"
	while IFS=$'\t' read -r _ step module _ _
	do
		if [[ "$module" != validate ]]
		then
			(cd /tmp && "$binary" --recipe "$recipe" --preview "$step") > "$test_dir/preview"
			rg -q 'CURRENT PROJECT' "$test_dir/preview"
		fi
	done < "$test_dir/plan"
done
(cd /tmp && "$binary" --recipe "$root/recipes/landing.yaml" --preview design) > "$test_dir/preview"
rg -q 'RESPONSE PROTOCOL' "$test_dir/preview"
# Compile an external plugin using only installed public headers. This checks
# the real library extension boundary rather than internal registration calls.
read -r -a flags <<< "$(pkg-config --cflags --libs gio-2.0 gobject-2.0 json-glib-1.0 gmodule-2.0)"
gcc -std=gnu89 -Wall -Wextra -Werror -Wstrict-prototypes -Wmissing-prototypes -shared -fPIC \
	-I"$test_dir/usr/include/libresite-1.0" "$root/tests/fixture-plugin.c" \
	-o "$test_dir/seed.so" "${flags[@]}"
cat > "$test_dir/native.yaml" <<'YAML'
version: 1
name: native-test
site: {brief: Test a native module}
pipeline:
  steps:
    - {id: seed, type: seed}
    - id: acceptance
      type: validate
      options:
        required: [index.html]
        checks: [{path: index.html, contains: ['Native module'], min_bytes: 16}]
YAML
(cd /tmp && "$binary" --plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" -o "$test_dir/site")
rg -q 'Native module' "$test_dir/site/index.html"
# Dynamic library consumers must load the installed library and its ABI.
cat > "$test_dir/consumer.c" <<'C'
#include <libresite.h>
int main(void)
{
	g_autoptr(LsContext) context = ls_context_new();
	g_autoptr(GError) error = NULL;
	/* Resolve the new public validator from the installed shared library. */
	if (context == NULL || ls_validate_step_check_options(NULL, &error))
		return 1;
	return !g_error_matches(error, LS_ERROR, LS_ERROR_INVALID);
}
C
gcc -std=gnu89 -Wall -Wextra -Werror -I"$test_dir/usr/include/libresite-1.0" \
	"$test_dir/consumer.c" -L"$test_dir/usr/lib" -llibresite-1.0 -o "$test_dir/consumer" "${flags[@]}"
LD_LIBRARY_PATH="$test_dir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$test_dir/consumer"
# Invalid registration callbacks must stop before preflight or publication,
# including callbacks that return success while supplying an error.
cat > "$test_dir/broken.c" <<'C'
#include <libresite.h>
#include <gmodule.h>
G_MODULE_EXPORT gboolean ls_plugin_register(LsPipeline *pipeline, GError **error);
/**
 * ls_plugin_register:
 * @pipeline: host pipeline
 * @error: return location for an error
 * Returns: a deliberately invalid registration result for boundary testing
 */
gboolean
ls_plugin_register(LsPipeline *pipeline, GError **error){
	(void)pipeline;
	/* Environment selects two distinct broken callback contracts. */
	if (g_strcmp0(g_getenv("LS_TEST_PLUGIN_FAILURE"), "both") == 0) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "registration sentinel");
		return TRUE;
	}
	return FALSE;
}
C
gcc -std=gnu89 -Wall -Wextra -Werror -Wstrict-prototypes -Wmissing-prototypes -shared -fPIC \
	-I"$test_dir/usr/include/libresite-1.0" "$test_dir/broken.c" \
	-o "$test_dir/broken.so" "${flags[@]}"
for failure in both missing
do
	if LS_TEST_PLUGIN_FAILURE="$failure" "$binary" --plugin "$test_dir/broken.so" \
		-r "$root/recipes/checked-landing.yaml" --validate > "$test_dir/broken.log" 2>&1
	then
		exit 1
	fi
	if [[ "$failure" == both ]]
	then
		rg -q 'registration sentinel' "$test_dir/broken.log"
	else
		rg -q 'Plugin registration failed without an error' "$test_dir/broken.log"
	fi
done
printf '%s\n' 'INSTALL VERIFIED'
