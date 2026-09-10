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
(cd /tmp && bash "$root/tests/revision.sh" "$binary")
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
			rg -q 'MOBILE AND DESKTOP DEFAULTS' "$test_dir/preview"
			rg -q 'ACCESSIBILITY AND ARIA DEFAULTS' "$test_dir/preview"
			rg -Fq 'FONT DEFAULTS' "$test_dir/preview"
			rg -Fq 'https://fonts.bunny.net/css' "$test_dir/preview"
			rg -q 'SEO DEFAULTS' "$test_dir/preview"
		fi
	done < "$test_dir/plan"
done
(cd /tmp && "$binary" --recipe "$root/recipes/landing.yaml" --preview design) > "$test_dir/preview"
rg -q 'RESPONSE PROTOCOL' "$test_dir/preview"
# The shipped catalog must match the embedded defaults outside the checkout,
# including styles absent from bundled recipes and the generic generate stage.
for style in editorial minimalist industrial cinematic application
do
	cat > "$test_dir/responsive.yaml" <<YAML
version: 1
name: responsive-defaults
site: {brief: Build a complete site, style: $style}
pipeline:
  steps:
    - {id: site, type: generate}
YAML
	(cd /tmp && "$binary" -r "$test_dir/responsive.yaml" --preview site) > "$test_dir/embedded"
	(cd /tmp && "$binary" -r "$test_dir/responsive.yaml" \
		--prompts "$test_dir/usr/share/libresite/data/prompts.yaml" --preview site) > "$test_dir/catalog"
	cmp "$test_dir/embedded" "$test_dir/catalog"
	# Every style must receive the font source, override and offline contract.
	rg -Fq 'https://fonts.bunny.net/css' "$test_dir/embedded"
	rg -Fq 'requesting display=swap' "$test_dir/embedded"
	rg -Fq 'Explicit font and hosting choices take precedence' "$test_dir/embedded"
	rg -Fq 'when font requests are blocked or offline' "$test_dir/embedded"
	rg -q 'MOBILE AND DESKTOP DEFAULTS' "$test_dir/embedded"
	# All styles inherit SEO in generic generate stages, with no recipe opt-in.
	for contract in 'SEO DEFAULTS' 'config/seo.yaml' 'docs/seo.org' 'tests/seo.sh' \
		'scripts/prepare-seo.sh' 'noindex' 'JSON-LD' 'hreflang' 'sitemap.xml'
	do
		rg -Fq "$contract" "$test_dir/embedded"
	done
	rg -q '360x800 and 390x844' "$test_dir/embedded"
	rg -q '1366x768 laptop and 1440x900 desktop' "$test_dir/embedded"
	# Check the substantive contract, not just its heading, in every style
	# through both installed GResource and replacement-catalog paths.
	for contract in 'WAI-ARIA 1.2' 'Prefer native HTML controls' \
		'accessible name' 'valid non-abstract roles' 'required parent/child role relationships' \
		'IDs unique' 'all ARIA ID references' 'synchronized with actual behavior' \
		'aria-hidden="true"' 'Tab/Shift+Tab' 'focus restoration' \
		'HTMX or other DOM replacements' 'status/live regions' \
		'runnable automated accessibility checks' 'failure exit status' \
		'manual keyboard and screen-reader checks' 'unexecuted' \
		'Native artifact validation does not certify ARIA compliance'
	do
		rg -Fq "$contract" "$test_dir/embedded"
	done
done

# Installed stack selection must use embedded data from outside the checkout.
# Exercise the installed recipe as well, so no source-relative API lookup helps.
(cd /tmp && "$binary" -r "$test_dir/usr/share/libresite/recipes/application.yaml" --preview backend) > "$test_dir/c-stack"
for contract in 'yaml_node_to_json_node' 'htmx_router_post' 'orm_result_get_error' \
	'gitlab.com/zachpodbielniak/orm-glib.git' 'sql/bootstrap-postgresql.sql'
do
	rg -Fq "$contract" "$test_dir/c-stack"
done
# The replacement catalog path must render the same stack contract as GResource.
(cd /tmp && "$binary" -r "$test_dir/usr/share/libresite/recipes/application.yaml" \
	--prompts "$test_dir/usr/share/libresite/data/prompts.yaml" --preview backend) > "$test_dir/c-stack-external"
cmp "$test_dir/c-stack" "$test_dir/c-stack-external"
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
# The published directory is a standalone clean repository with one snapshot.
test "$(git -C "$test_dir/site" rev-parse --show-toplevel)" = "$test_dir/site"
test "$(git -C "$test_dir/site" branch --show-current)" = master
test "$(git -C "$test_dir/site" rev-list --count HEAD)" -eq 1
test "$(git -C "$test_dir/site" log -1 --format=%s)" = 'chore: initial generated project'
test "$(git -C "$test_dir/site" log -1 --format='%an <%ae>')" = 'Libresite <libresite@localhost>'
test -z "$(git -C "$test_dir/site" status --porcelain)"
git -C "$test_dir/site" ls-files > "$test_dir/tracked"
for path in index.html .gitignore .env.example package-lock.json
do
	rg -Fxq "$path" "$test_dir/tracked"
done
for path in .env .libresite/run.json build/output.o custom-cache/item
do
	test -f "$test_dir/site/$path"
	git -C "$test_dir/site" check-ignore -q "$path"
done
rg -Fxq '/custom-cache/' "$test_dir/site/.gitignore"
# Inherited repository pointers and user config must not affect publication.
mkdir -p "$test_dir/hooks" "$test_dir/template/hooks"
printf '#!/bin/sh\nexit 99\n' > "$test_dir/hooks/pre-commit"
chmod +x "$test_dir/hooks/pre-commit"
cp "$test_dir/hooks/pre-commit" "$test_dir/template/hooks/pre-commit"
git config --file "$test_dir/gitconfig" core.hooksPath "$test_dir/hooks"
git config --file "$test_dir/gitconfig" init.templateDir "$test_dir/template"
git config --file "$test_dir/gitconfig" commit.gpgSign true
git config --file "$test_dir/gitconfig" core.excludesFile "$test_dir/tracked"
GIT_DIR="$test_dir/site/.git" GIT_WORK_TREE="$test_dir/site" \
	GIT_INDEX_FILE="$test_dir/foreign-index" GIT_CONFIG_GLOBAL="$test_dir/gitconfig" \
	"$binary" --plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" -o "$test_dir/isolated site"
test ! -e "$test_dir/foreign-index"
test "$(git -C "$test_dir/isolated site" rev-list --count HEAD)" -eq 1
git -C "$test_dir/isolated site" ls-files --error-unmatch index.html .gitignore > /dev/null
test -z "$(git -C "$test_dir/site" status --porcelain)"
# Fail each Git phase, keeping the final destination absent and checkpoint
# available; resume must reconstruct the repository from completed artifacts.
mkdir "$test_dir/bin"
export LS_REAL_GIT
LS_REAL_GIT=$(command -v git)
cat > "$test_dir/bin/git" <<'BASH'
#!/usr/bin/env bash
if [[ -n ${LS_GIT_READY:-} ]]
then
	printf '%s\n' "$$" > "$LS_GIT_READY"
	exec sleep 60
fi
for arg in "$@"
do
	if [[ "$arg" == "${LS_GIT_FAIL:?}" ]]
	then
		printf '%s\n' 'injected git failure' >&2
		exit 73
	fi
done
exec "${LS_REAL_GIT:?}" "$@"
BASH
chmod +x "$test_dir/bin/git"
for phase in init add commit
do
	if PATH="$test_dir/bin:$PATH" LS_GIT_FAIL="$phase" "$binary" \
		--plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" \
		-o "$test_dir/fail-$phase" > "$test_dir/git-failure.log" 2>&1
	then
		exit 1
	fi
	rg -q 'injected git failure' "$test_dir/git-failure.log"
	test ! -e "$test_dir/fail-$phase"
	test -f "$test_dir/fail-$phase.libresite-work/checkpoint.json"
	"$binary" --plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" \
		-o "$test_dir/fail-$phase" --resume
	test "$(git -C "$test_dir/fail-$phase" rev-list --count HEAD)" -eq 1
done
# Cancellation during Git publication must terminate and reap the child.
PATH="$test_dir/bin:$PATH" LS_GIT_READY="$test_dir/git-ready" \
	"$binary" --plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" \
	-o "$test_dir/git-cancel" > "$test_dir/git-cancel.log" 2>&1 &
run_pid=$!
for ((poll = 0; poll < 100; poll++))
do
	[[ -s "$test_dir/git-ready" ]] && break
	sleep 0.1
done
test -s "$test_dir/git-ready"
git_pid=$(< "$test_dir/git-ready")
kill -TERM "$run_pid"
run_status=0
wait "$run_pid" || run_status=$?
test "$run_status" -eq 130
if kill -0 "$git_pid" 2>/dev/null
then
	exit 1
fi
test ! -e "$test_dir/git-cancel"
"$binary" --plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" \
	-o "$test_dir/git-cancel" --resume
# Missing Git also leaves a resumable export, without invoking any provider.
mkdir "$test_dir/empty-path"
if PATH="$test_dir/empty-path" "$binary" --plugin "$test_dir/seed.so" \
	-r "$test_dir/native.yaml" -o "$test_dir/missing-git" > "$test_dir/missing-git.log" 2>&1
then
	exit 1
fi
rg -q 'Cannot run git init:' "$test_dir/missing-git.log"
test ! -e "$test_dir/missing-git"
test -f "$test_dir/missing-git.libresite-work/checkpoint.json"
"$binary" --plugin "$test_dir/seed.so" -r "$test_dir/native.yaml" \
	-o "$test_dir/missing-git" --resume
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
