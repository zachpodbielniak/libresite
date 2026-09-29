/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Component steps. "plan" writes site/plan.json in one call; "pages" writes
 * every page specification and artwork at the same time on worker threads,
 * then composes the site natively. See docs/components.org. */
#include "steps/ls-step.h"
#include "core/ls-components.h"
#include <string.h>

#define PLAN_PATH "site/plan.json"
#define SPEC_DIR "site/pages/"
#define DEFAULT_PARALLEL 6
#define MAX_PARALLEL 16

struct _LsPlanStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsPlanStep, ls_plan_step, LS_TYPE_STEP)
struct _LsPagesStep {
	LsStep parent_instance;
};
G_DEFINE_TYPE(LsPagesStep, ls_pages_step, LS_TYPE_STEP)

static const gchar *plan_theme(JsonObject *config);

static JsonObject *
member_object_of(JsonObject *object, const gchar *name){
	JsonNode *node = object != NULL ? json_object_get_member(object, name) : NULL;
	return node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
}

static JsonObject *
step_options(JsonObject *config){
	JsonNode *node = config != NULL ? json_object_get_member(config, "options") : NULL;
	return node != NULL && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : NULL;
}

/* The plan step's options name a library theme and the business sector. */
static const gchar *
plan_theme(JsonObject *config){
	JsonObject *options = step_options(config);
	return options != NULL ? ls_string(options, "theme") : NULL;
}

static const gchar *
plan_sector(JsonObject *config){
	JsonObject *options = step_options(config);
	return options != NULL ? ls_string(options, "sector") : NULL;
}

/* Pixel size of a PNG, JPEG or WebP image, read from its header. */
static gboolean
image_size(const guint8 *data, gsize size, gint64 *width, gint64 *height){
	gsize i;
	if (size >= 24 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(data + 12, "IHDR", 4) == 0) {
		*width = (gint64)data[16] << 24 | data[17] << 16 | data[18] << 8 | data[19];
		*height = (gint64)data[20] << 24 | data[21] << 16 | data[22] << 8 | data[23];
		return *width > 0 && *height > 0;
	}
	if (size >= 30 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0) {
		if (memcmp(data + 12, "VP8X", 4) == 0) {
			*width = 1 + (data[24] | data[25] << 8 | data[26] << 16);
			*height = 1 + (data[27] | data[28] << 8 | data[29] << 16);
			return TRUE;
		}
		if (memcmp(data + 12, "VP8L", 4) == 0 && data[20] == 0x2f) {
			*width = 1 + ((data[22] & 0x3f) << 8 | data[21]);
			*height = 1 + ((data[24] & 0x0f) << 10 | data[23] << 2 | (data[22] & 0xc0) >> 6);
			return TRUE;
		}
		if (memcmp(data + 12, "VP8 ", 4) == 0 && data[23] == 0x9d && data[24] == 0x01 && data[25] == 0x2a) {
			*width = (data[26] | data[27] << 8) & 0x3fff;
			*height = (data[28] | data[29] << 8) & 0x3fff;
			return *width > 0 && *height > 0;
		}
		return FALSE;
	}
	if (size < 4 || data[0] != 0xff || data[1] != 0xd8)
		return FALSE;
	/* Walk JPEG segments to the first start-of-frame marker. */
	for (i = 2; i + 9 < size;) {
		guint8 marker;
		gsize length;
		if (data[i] != 0xff)
			return FALSE;
		marker = data[i + 1];
		if (marker == 0xff) {
			i++;
			continue;
		}
		length = (gsize)data[i + 2] << 8 | data[i + 3];
		if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
			*height = data[i + 5] << 8 | data[i + 6];
			*width = data[i + 7] << 8 | data[i + 8];
			return *width > 0 && *height > 0;
		}
		if (length < 2)
			return FALSE;
		i += 2 + length;
	}
	return FALSE;
}

/* The recipe's supplied photographs with their pixel sizes, read from the
 * seeded images in @context. %NULL context (a preview) omits the sizes. */
static JsonArray *
plan_photos(LsRecipe *recipe, LsContext *context, GError **error){
	JsonObject *site = member_object_of(ls_recipe_get_root(recipe), "site");
	JsonNode *node = site != NULL ? json_object_get_member(site, "photos") : NULL;
	JsonArray *photos = node != NULL && JSON_NODE_HOLDS_ARRAY(node) ? json_node_get_array(node) : NULL;
	g_autoptr(JsonArray) out = json_array_new();
	guint i;
	for (i = 0; photos != NULL && i < json_array_get_length(photos); i++) {
		JsonObject *photo = json_array_get_object_element(photos, i);
		JsonObject *entry = json_object_new();
		json_object_set_string_member(entry, "path", ls_string(photo, "path"));
		json_object_set_string_member(entry, "alt", ls_string(photo, "alt"));
		if (ls_string(photo, "credit") != NULL)
			json_object_set_string_member(entry, "credit", ls_string(photo, "credit"));
		if (ls_string(photo, "focus") != NULL)
			json_object_set_string_member(entry, "focus", ls_string(photo, "focus"));
		json_array_add_object_element(out, entry);
		if (context != NULL) {
			const LsArtifact *artifact = ls_context_lookup(context, ls_string(photo, "path"));
			const guint8 *data;
			gsize size = 0;
			gint64 width = 0, height = 0;
			data = artifact != NULL && ls_artifact_is_binary(artifact) ? ls_artifact_get_data(artifact, &size) : NULL;
			if (data == NULL || !image_size(data, size, &width, &height)) {
				g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
					"site.photos %s must be a seeded binary PNG, JPEG or WebP image", ls_string(photo, "path"));
				return NULL;
			}
			json_object_set_int_member(entry, "width", width);
			json_object_set_int_member(entry, "height", height);
		}
	}
	return g_steal_pointer(&out);
}

/* Everything dealt to a site hangs off its recipe name and brief. */
static gchar *
plan_seed(LsRecipe *recipe){
	JsonObject *root = ls_recipe_get_root(recipe);
	const gchar *name = ls_string(root, "name");
	const gchar *brief = ls_string(member_object_of(root, "site"), "brief");
	return g_strconcat(name != NULL ? name : "", "\n", brief != NULL ? brief : "", NULL);
}

/* The identity dealt to this site: stable for its recipe name and brief. */
static JsonObject *
plan_identity(LsComponents *components, LsRecipe *recipe, JsonObject *config){
	g_autofree gchar *seed = plan_seed(recipe);
	return ls_components_suggest_identity(components, seed, plan_sector(config));
}

static gboolean
check_plan_options(LsRecipe *recipe, JsonObject *config, GError **error){
	JsonObject *options = step_options(config);
	g_autoptr(LsComponents) components = NULL;
	if (options == NULL)
		return TRUE;
	if (!ls_object_keys(options, "|theme|sector|", NULL) ||
		(json_object_has_member(options, "theme") && ls_string(options, "theme") == NULL) ||
		(json_object_has_member(options, "sector") && ls_string(options, "sector") == NULL)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"plan options allow only theme (a theme name) and sector (a sector name)");
		return FALSE;
	}
	components = ls_recipe_new_components(recipe, error);
	if (components == NULL)
		return FALSE;
	if (ls_string(options, "sector") != NULL) {
		g_auto(GStrv) sectors = ls_components_list_sectors(components);
		if (!g_strv_contains((const gchar *const *)sectors, ls_string(options, "sector"))) {
			g_autofree gchar *list = g_strjoinv(", ", sectors);
			g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unknown sector '%s'; available: %s",
				ls_string(options, "sector"), list);
			return FALSE;
		}
	}
	if (ls_string(options, "theme") != NULL &&
		ls_components_get_theme_summary(components, ls_string(options, "theme")) == NULL) {
		g_auto(GStrv) names = ls_components_list_themes(components);
		g_autofree gchar *list = g_strjoinv(", ", names);
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Unknown theme '%s'; available: %s",
			ls_string(options, "theme"), list);
		return FALSE;
	}
	return TRUE;
}

gboolean
ls_pages_step_check_options(JsonObject *options, GError **error){
	JsonNode *node;
	const gchar *mode, *base;
	if (options == NULL)
		return TRUE;
	if (!ls_object_keys(options, "|parallel|mode|base_url|", NULL))
		goto invalid;
	node = json_object_get_member(options, "parallel");
	if (node != NULL && (!JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_INT64 ||
		json_node_get_int(node) < 1 || json_node_get_int(node) > MAX_PARALLEL))
		goto invalid;
	mode = ls_string(options, "mode");
	if (json_object_has_member(options, "mode") && g_strcmp0(mode, "write") != 0 && g_strcmp0(mode, "review") != 0)
		goto invalid;
	base = ls_string(options, "base_url");
	if (json_object_has_member(options, "base_url") && (base == NULL || !g_str_has_prefix(base, "https://") ||
		strlen(base) < 10 || strpbrk(base, " ?#@\"'<>\\") != NULL))
		goto invalid;
	return TRUE;
invalid:
	g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
		"pages options allow parallel (1-16), mode (write or review) and base_url (https://...)");
	return FALSE;
}

/* Literal checks from every validate step, grouped by path, tell the plan
 * which routes must exist and each writer which text its page must contain. */
static JsonObject *
acceptance_literals(LsRecipe *recipe){
	JsonObject *result = json_object_new();
	JsonArray *steps = ls_recipe_get_steps(recipe);
	guint i, j, k;
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *step = json_array_get_object_element(steps, i);
		JsonObject *options = step_options(step);
		JsonNode *required = options != NULL ? json_object_get_member(options, "required") : NULL;
		JsonNode *checks = options != NULL ? json_object_get_member(options, "checks") : NULL;
		if (g_strcmp0(ls_string(step, "type"), "validate") != 0)
			continue;
		for (j = 0; required != NULL && JSON_NODE_HOLDS_ARRAY(required) &&
			j < json_array_get_length(json_node_get_array(required)); j++) {
			const gchar *path = json_array_get_string_element(json_node_get_array(required), j);
			if (path != NULL && g_str_has_suffix(path, ".html") && !json_object_has_member(result, path))
				json_object_set_array_member(result, path, json_array_new());
		}
		for (j = 0; checks != NULL && JSON_NODE_HOLDS_ARRAY(checks) &&
			j < json_array_get_length(json_node_get_array(checks)); j++) {
			JsonObject *rule = json_array_get_object_element(json_node_get_array(checks), j);
			const gchar *path = ls_string(rule, "path");
			JsonNode *contains = json_object_get_member(rule, "contains");
			JsonArray *list;
			if (path == NULL || !g_str_has_suffix(path, ".html"))
				continue;
			if (!json_object_has_member(result, path))
				json_object_set_array_member(result, path, json_array_new());
			list = json_object_get_array_member(result, path);
			for (k = 0; contains != NULL && JSON_NODE_HOLDS_ARRAY(contains) &&
				k < json_array_get_length(json_node_get_array(contains)); k++)
				json_array_add_string_element(list, json_array_get_string_element(json_node_get_array(contains), k));
		}
	}
	return result;
}

static gboolean
is_utility_route(const gchar *path){
	return g_strcmp0(path, "404.html") == 0 || g_strcmp0(path, "privacy.html") == 0 ||
		g_strcmp0(path, "terms.html") == 0;
}

/* Validate immediately following native checks against a candidate project,
 * exactly as generate steps do; failed output never reaches the context. */
static gboolean
check_acceptance(LsRecipe *recipe, JsonObject *config, LsPrompts *prompts, LsContext *candidate,
	LsProvider *provider, GCancellable *cancellable, GError **error){
	JsonArray *steps = ls_recipe_get_steps(recipe);
	gboolean found = FALSE;
	guint i;
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *next = json_array_get_object_element(steps, i);
		g_autoptr(LsStep) validator = NULL;
		if (!found) {
			found = g_strcmp0(ls_string(next, "id"), ls_string(config, "id")) == 0;
			continue;
		}
		if (!g_str_equal(ls_string(next, "type"), "validate"))
			break;
		validator = g_object_new(LS_TYPE_VALIDATE_STEP, NULL);
		if (!ls_step_execute(validator, recipe, next, prompts, candidate, provider, cancellable, error))
			return FALSE;
	}
	return TRUE;
}

static JsonNode *
parse_object(const gchar *text, GError **error){
	g_autoptr(GError) local_error = NULL;
	JsonNode *node = ls_json_parse(text, &local_error);
	if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
			"The response is not one JSON object%s%s; return only the object, with no prose or fences",
			local_error != NULL ? ": " : "", local_error != NULL ? local_error->message : "");
		g_clear_pointer(&node, json_node_unref);
		return NULL;
	}
	return node;
}

static gchar *
retry_prompt(const gchar *prompt, const gchar *feedback){
	return g_strdup_printf("%s\nPREVIOUS ATTEMPT REJECTED\n%.2000s\nDeliver the corrected, complete object the same way as before: once, with no prose.\n",
		prompt, feedback);
}

static gchar *
first_line(const gchar *text){
	gsize length = strcspn(text, "\n");
	gchar *line = g_strndup(text, MIN(length, (gsize)300));
	/* A multi-problem report starts with a summary; show the first problem too. */
	if (text[length] == '\n' && g_str_has_suffix(line, ":")) {
		const gchar *next = text + length + 1;
		gchar *joined = g_strdup_printf("%s %.*s", line, (gint)MIN(strcspn(next, "\n"), (gsize)200), next);
		g_free(line);
		line = joined;
	}
	return line;
}

static gboolean
store(LsContext *context, const gchar *path, const gchar *content, GError **error){
	g_autoptr(JsonBuilder) builder = json_builder_new();
	g_autoptr(JsonNode) root = NULL;
	g_autofree gchar *bundle = NULL;
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "files");
	json_builder_begin_array(builder);
	json_builder_begin_object(builder);
	json_builder_set_member_name(builder, "path");
	json_builder_add_string_value(builder, path);
	json_builder_set_member_name(builder, "content");
	json_builder_add_string_value(builder, content);
	json_builder_end_object(builder);
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	root = json_builder_get_root(builder);
	bundle = ls_json_encode(root);
	return ls_context_apply(context, bundle, error);
}

static JsonObject *
spec_settings(LsRecipe *recipe, JsonObject *config, const gchar *schema){
	JsonObject *settings = ls_recipe_resolve_ai(recipe, config);
	json_object_set_string_member(settings, "json_schema", schema);
	return settings;
}

/* ---- Plan -------------------------------------------------------------- */

static gchar *
plan_prompt(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, LsComponents *components,
	JsonObject *identity, JsonArray *photos, GError **error){
	g_autoptr(JsonObject) literals = acceptance_literals(recipe);
	g_autoptr(JsonObject) task = json_object_new();
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	g_autofree gchar *reference = ls_components_describe(components);
	g_autofree gchar *task_json = NULL;
	JsonArray *routes = json_array_new();
	g_autoptr(GList) paths = json_object_get_members(literals);
	GList *item;
	for (item = paths; item != NULL; item = item->next)
		if (!is_utility_route(item->data))
			json_array_add_string_element(routes, item->data);
	json_object_set_array_member(task, "required_routes", routes);
	if (photos != NULL && json_array_get_length(photos) > 0)
		json_object_set_array_member(task, "supplied_photos", json_array_ref(photos));
	{
		JsonObject *root = ls_recipe_get_root(recipe);
		const gchar *name = ls_string(root, "name");
		const gchar *brief = ls_string(member_object_of(root, "site"), "brief");
		g_autofree gchar *seed = g_strconcat(name != NULL ? name : "", "\n", brief != NULL ? brief : "", NULL);
		g_autofree gchar *opening = ls_components_suggest_opening(components, seed);
		if (opening != NULL)
			json_object_set_string_member(task, "suggested_opening", opening);
	}
	if (plan_theme(config) != NULL) {
		JsonObject *theme = json_object_new();
		json_object_set_string_member(theme, "name", plan_theme(config));
		json_object_set_string_member(theme, "summary", ls_components_get_theme_summary(components, plan_theme(config)));
		json_object_set_object_member(task, "theme", theme);
	} else {
		JsonObject *root = ls_recipe_get_root(recipe);
		const gchar *name = ls_string(root, "name");
		const gchar *brief = ls_string(member_object_of(root, "site"), "brief");
		g_autofree gchar *seed = g_strconcat(name != NULL ? name : "", "\n", brief != NULL ? brief : "", NULL);
		json_object_set_object_member(task, "suggested_look", ls_components_suggest_look(components, seed));
		if (json_object_has_member(identity, "palette"))
			json_object_set_member(task, "suggested_palette",
				json_node_copy(json_object_get_member(identity, "palette")));
		if (json_object_has_member(identity, "type"))
			json_object_set_member(task, "suggested_type", json_node_copy(json_object_get_member(identity, "type")));
	}
	if (json_object_has_member(identity, "home"))
		json_object_set_member(task, "suggested_home", json_node_copy(json_object_get_member(identity, "home")));
	json_object_set_object_member(task, "required_text", json_object_ref(literals));
	json_node_set_object(node, task);
	task_json = ls_json_encode(node);
	return ls_prompts_render_spec(prompts, recipe, config,
		ls_string(config, "prompt") != NULL ? ls_string(config, "prompt") : "plan", reference, task_json, error);
}

/* Every acceptance route must be planned; the plan cannot drop a page. */
static gboolean
check_routes(LsRecipe *recipe, JsonNode *plan, GError **error){
	g_autoptr(JsonObject) literals = acceptance_literals(recipe);
	g_autoptr(GList) paths = json_object_get_members(literals);
	JsonArray *pages = json_object_get_array_member(json_node_get_object(plan), "pages");
	GString *missing = g_string_new(NULL);
	GList *item;
	guint i;
	for (item = paths; item != NULL; item = item->next) {
		gboolean found = is_utility_route(item->data);
		for (i = 0; !found && i < json_array_get_length(pages); i++)
			found = g_strcmp0(ls_string(json_array_get_object_element(pages, i), "path"), item->data) == 0;
		if (!found)
			g_string_append_printf(missing, "%s%s", missing->len > 0 ? ", " : "", (const gchar *)item->data);
	}
	if (missing->len > 0) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID,
			"The site plan omits routes the recipe requires: %s", missing->str);
		g_string_free(missing, TRUE);
		return FALSE;
	}
	g_string_free(missing, TRUE);
	return TRUE;
}

static gboolean
plan_execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	g_autoptr(LsComponents) components = NULL;
	g_autoptr(JsonObject) settings = NULL;
	g_autoptr(JsonObject) identity = NULL;
	g_autoptr(JsonArray) photos = NULL;
	g_autofree gchar *prompt = NULL, *retry = NULL;
	const gchar *system = ls_prompts_get_spec_system(prompts);
	const gchar *theme;
	gint64 retries, attempt;
	if (ls_context_is_revision(context)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"Component steps generate new sites; revise an existing project with generate steps");
		return FALSE;
	}
	components = ls_recipe_new_components(recipe, error);
	if (components == NULL || !ls_spec_step_check(prompts, recipe, config, error))
		return FALSE;
	theme = plan_theme(config);
	settings = spec_settings(recipe, config, theme != NULL ? ls_components_get_themed_plan_schema(components)
		: ls_components_get_plan_schema(components));
	retries = json_object_get_int_member(settings, "retries");
	identity = plan_identity(components, recipe, config);
	photos = plan_photos(recipe, context, error);
	if (photos == NULL)
		return FALSE;
	prompt = plan_prompt(prompts, recipe, config, components, identity, photos, error);
	if (prompt == NULL)
		return FALSE;
	for (attempt = 0; attempt <= retries; attempt++) {
		g_autoptr(GError) local_error = NULL;
		g_autoptr(JsonNode) plan = NULL;
		g_autofree gchar *response = NULL;
		g_autofree gchar *status = g_strdup_printf("attempt %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT
			"; provider %s; timeout %" G_GINT64_FORMAT "s%s", attempt + 1, retries + 1,
			ls_string(settings, "provider"), json_object_get_int_member(settings, "timeout"),
			json_object_get_int_member(settings, "timeout") == 0 ? " (unlimited)" : "");
		g_signal_emit_by_name(self, "progress", status);
		response = ls_provider_generate(provider, settings, system, retry != NULL ? retry : prompt,
			cancellable, &local_error);
		if (response != NULL && (plan = parse_object(response, &local_error)) != NULL &&
			theme != NULL && !ls_components_apply_theme(components, plan, theme, &local_error))
			g_clear_pointer(&plan, json_node_unref);
		/* Structure is the builder's too: the frame, rhythm and genes are
		 * dealt, so sites cannot converge on the model's favourite layout. */
		if (plan != NULL && JSON_NODE_HOLDS_OBJECT(plan)) {
			g_autofree gchar *seed = plan_seed(recipe);
			ls_components_apply_structure(components, plan, seed, theme != NULL);
		}
		/* Supplied photographs are the builder's facts, never the model's. */
		if (plan != NULL && JSON_NODE_HOLDS_OBJECT(plan)) {
			JsonObject *root = json_node_get_object(plan);
			if (json_object_has_member(root, "photos"))
				json_object_remove_member(root, "photos");
			if (json_array_get_length(photos) > 0) {
				g_autoptr(JsonNode) copy = json_node_new(JSON_NODE_ARRAY);
				g_autofree gchar *text = NULL;
				json_node_set_array(copy, photos);
				text = ls_json_encode(copy);
				json_object_set_member(root, "photos", ls_json_parse(text, NULL));
			}
		}
		if (plan != NULL) {
			g_autofree gchar *repairs = ls_components_repair_plan(components, plan);
			if (repairs != NULL) {
				g_autofree gchar *note = g_strdup_printf("repaired plan tokens: %.400s", repairs);
				g_signal_emit_by_name(self, "progress", note);
			}
		}
		if (plan != NULL && ls_components_check_plan(components, plan, &local_error) &&
			check_routes(recipe, plan, &local_error) &&
			(theme != NULL || ls_components_check_identity(components, plan, identity, &local_error))) {
			g_autofree gchar *text = ls_json_encode(plan);
			if (g_cancellable_set_error_if_cancelled(cancellable, error))
				return FALSE;
			return store(context, PLAN_PATH, text, error);
		}
		if (attempt == retries || g_cancellable_is_cancelled(cancellable) ||
			g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
		{
			g_autofree gchar *line = first_line(local_error->message);
			g_free(status);
			status = g_strdup_printf("attempt %" G_GINT64_FORMAT " failed: %s; retrying", attempt + 1, line);
			g_signal_emit_by_name(self, "progress", status);
		}
		g_free(retry);
		retry = retry_prompt(prompt, local_error->message);
	}
	return FALSE;
}

static void
ls_plan_step_class_init(LsPlanStepClass *klass){
	LS_STEP_CLASS(klass)->execute = plan_execute;
}
static void
ls_plan_step_init(LsPlanStep *self){
	(void)self;
}

/* ---- Pages ------------------------------------------------------------- */

typedef enum { JOB_PAGE, JOB_ART } JobKind;

typedef struct {
	JobKind kind;
	gchar *path;
	gchar *prompt;
	gchar *feedback;
	JsonObject *settings;
	gint64 attempts;
	/* Written by the worker, read by the step thread after the queue pop. */
	gchar *response;
	GError *error;
	gint64 bytes_in;
	gint64 bytes_out;
	gint64 elapsed;
	/* Accepted result. */
	JsonNode *spec;
	gchar *svg;
} Job;

static void
job_free(gpointer data){
	Job *job = data;
	g_free(job->path);
	g_free(job->prompt);
	g_free(job->feedback);
	g_clear_pointer(&job->settings, json_object_unref);
	g_free(job->response);
	g_clear_error(&job->error);
	g_clear_pointer(&job->spec, json_node_unref);
	g_free(job->svg);
	g_free(job);
}

typedef struct {
	LsProvider *provider;
	const gchar *system;
	GCancellable *cancellable;
	GAsyncQueue *done;
} Shared;

/* Worker threads only dispatch: no signals, no context access. */
static void
worker(gpointer data, gpointer user_data){
	Job *job = data;
	Shared *shared = user_data;
	g_autofree gchar *prompt = job->feedback != NULL ? retry_prompt(job->prompt, job->feedback) : NULL;
	gint64 started = g_get_monotonic_time();
	g_clear_pointer(&job->response, g_free);
	g_clear_error(&job->error);
	job->response = ls_provider_dispatch(shared->provider, job->settings, shared->system,
		prompt != NULL ? prompt : job->prompt, &job->bytes_in, &job->bytes_out,
		shared->cancellable, &job->error);
	job->elapsed = g_get_monotonic_time() - started;
	g_async_queue_push(shared->done, job);
}

static const gchar *
job_label(Job *job){
	return job->kind == JOB_PAGE ? "page" : "art";
}

/* Runs on the step thread: parse and check one answer. */
static gboolean
accept_job(Job *job, LsComponents *components, JsonNode *plan, GError **error){
	g_autoptr(JsonNode) node = NULL;
	if (job->response == NULL) {
		g_propagate_error(error, g_steal_pointer(&job->error));
		return FALSE;
	}
	node = parse_object(job->response, error);
	if (node == NULL)
		return FALSE;
	if (job->kind == JOB_PAGE) {
		if (!ls_components_check_page(components, plan, job->path, node, error))
			return FALSE;
		g_clear_pointer(&job->spec, json_node_unref);
		job->spec = g_steal_pointer(&node);
		return TRUE;
	}
	if (ls_string(json_node_get_object(node), "svg") == NULL) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID, "The response needs an svg string");
		return FALSE;
	}
	if (!ls_components_check_art(ls_string(json_node_get_object(node), "svg"), error))
		return FALSE;
	g_free(job->svg);
	job->svg = g_strdup(ls_string(json_node_get_object(node), "svg"));
	return TRUE;
}

static void
cancel_child(GCancellable *parent, gpointer data){
	(void)parent;
	g_cancellable_cancel(G_CANCELLABLE(data));
}

/* Dispatch @jobs with at most @parallel in flight. Every signal and every
 * check runs here, on the step thread; the first job to exhaust its retries
 * cancels the rest, and nothing returns before all workers have finished. */
static gboolean
run_jobs(LsStep *self, LsProvider *provider, const gchar *system, GPtrArray *jobs, guint parallel,
	gint64 retries, LsComponents *components, JsonNode *plan, GCancellable *cancellable, GError **error){
	g_autoptr(GCancellable) child = g_cancellable_new();
	g_autoptr(GAsyncQueue) done = g_async_queue_new();
	g_autoptr(GError) failure = NULL;
	Shared shared = {provider, system, child, done};
	GThreadPool *pool;
	gulong handler = 0;
	guint pending = jobs->len, i;
	if (jobs->len == 0)
		return TRUE;
	if (cancellable != NULL)
		handler = g_cancellable_connect(cancellable, G_CALLBACK(cancel_child), child, NULL);
	pool = g_thread_pool_new(worker, &shared, (gint)MIN(parallel, jobs->len), FALSE, error);
	if (pool == NULL) {
		g_cancellable_disconnect(cancellable, handler);
		return FALSE;
	}
	for (i = 0; i < jobs->len; i++) {
		Job *job = g_ptr_array_index(jobs, i);
		job->attempts++;
		g_thread_pool_push(pool, job, NULL);
	}
	while (pending > 0) {
		Job *job = g_async_queue_pop(done);
		g_autoptr(GError) local_error = NULL;
		if (job->bytes_in > 0)
			ls_provider_record_usage(provider, job->settings, job->bytes_in, job->bytes_out);
		if (!g_cancellable_is_cancelled(child) && accept_job(job, components, plan, &local_error)) {
			g_autofree gchar *status = g_strdup_printf("%s %s ready in %.1fs", job_label(job), job->path,
				(gdouble)job->elapsed / G_USEC_PER_SEC);
			g_signal_emit_by_name(self, "progress", status);
			pending--;
			continue;
		}
		if (local_error == NULL && job->error != NULL)
			local_error = g_steal_pointer(&job->error);
		if (local_error == NULL)
			g_set_error_literal(&local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Operation was cancelled");
		if (!g_cancellable_is_cancelled(child) && job->attempts <= retries &&
			!g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_autofree gchar *line = first_line(local_error->message);
			g_autofree gchar *status = g_strdup_printf("%s %s attempt %" G_GINT64_FORMAT " failed: %s; retrying",
				job_label(job), job->path, job->attempts, line);
			g_signal_emit_by_name(self, "progress", status);
			g_free(job->feedback);
			job->feedback = g_strdup(local_error->message);
			job->attempts++;
			g_thread_pool_push(pool, job, NULL);
			continue;
		}
		pending--;
		/* Keep the first real failure; sibling cancellations follow from it. */
		if (failure == NULL && !g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			failure = g_steal_pointer(&local_error);
			g_prefix_error(&failure, "%s %s: ", job_label(job), job->path);
		}
		g_cancellable_cancel(child);
	}
	g_thread_pool_free(pool, FALSE, TRUE);
	if (handler != 0)
		g_cancellable_disconnect(cancellable, handler);
	if (g_cancellable_set_error_if_cancelled(cancellable, error))
		return FALSE;
	if (failure != NULL) {
		g_propagate_error(error, g_steal_pointer(&failure));
		return FALSE;
	}
	return TRUE;
}

static gchar *
route_slug(const gchar *path){
	return g_strndup(path, strlen(path) - 5);
}

/* Each writer sees the whole plan but only its own page brief, so parallel
 * writers share one design system without duplicating each other's pages. */
static gchar *
page_prompt(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, const gchar *stage,
	const gchar *reference, JsonObject *plan, JsonObject *entry, JsonArray *required,
	JsonNode *current, GError **error){
	g_autoptr(JsonObject) task = json_object_new();
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	g_autoptr(JsonNode) plan_copy = NULL;
	g_autoptr(JsonNode) plan_node = json_node_new(JSON_NODE_OBJECT);
	g_autofree gchar *slug = route_slug(ls_string(entry, "path"));
	g_autofree gchar *task_json = NULL;
	JsonArray *pages;
	guint i;
	json_node_set_object(plan_node, plan);
	/* json_node_copy() shares nested objects; round-trip for a private copy. */
	{
		g_autofree gchar *text = ls_json_encode(plan_node);
		plan_copy = ls_json_parse(text, error);
		if (plan_copy == NULL)
			return NULL;
	}
	pages = json_object_get_array_member(json_node_get_object(plan_copy), "pages");
	for (i = 0; i < json_array_get_length(pages); i++) {
		JsonObject *page = json_array_get_object_element(pages, i);
		if (g_strcmp0(ls_string(page, "path"), ls_string(entry, "path")) != 0)
			json_object_remove_member(page, "brief");
	}
	json_object_set_string_member(task, "route", ls_string(entry, "path"));
	json_object_set_string_member(task, "slug", slug);
	json_object_set_object_member(task, "page", json_object_ref(entry));
	json_object_set_array_member(task, "required", required != NULL ? json_array_ref(required) : json_array_new());
	if (current != NULL)
		json_object_set_member(task, "current", json_node_ref(current));
	json_object_set_member(task, "plan", g_steal_pointer(&plan_copy));
	json_node_set_object(node, task);
	task_json = ls_json_encode(node);
	return ls_prompts_render_spec(prompts, recipe, config, stage, reference, task_json, error);
}

static gchar *
art_prompt(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, LsComponents *components,
	JsonObject *plan, JsonObject *entry, GError **error){
	g_autoptr(JsonObject) task = json_object_new();
	g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
	g_autofree gchar *task_json = NULL;
	const gchar *keys[] = {"name", "tagline", "voice"};
	guint i;
	json_object_set_string_member(task, "path", ls_string(entry, "path"));
	json_object_set_object_member(task, "art", json_object_ref(entry));
	for (i = 0; i < G_N_ELEMENTS(keys); i++)
		if (ls_string(plan, keys[i]) != NULL)
			json_object_set_string_member(task, keys[i], ls_string(plan, keys[i]));
	json_object_set_object_member(task, "tokens", json_object_ref(json_object_get_object_member(plan, "tokens")));
	/* The site's art style: every artwork on the site is drawn the same way. */
	{
		const gchar *style = ls_string(member_object_of(plan, "look"), "art");
		const gchar *how = style != NULL ? ls_components_get_look_description(components, "art", style) : NULL;
		if (how != NULL && g_strcmp0(style, "open") != 0)
			json_object_set_string_member(task, "style", how);
	}
	json_node_set_object(node, task);
	task_json = ls_json_encode(node);
	return ls_prompts_render_spec(prompts, recipe, config, "art", NULL, task_json, error);
}

/* Route names appear quoted or delimited in validation errors; require a
 * boundary so atlas.html never matches work-atlas.html. */
static Job *
job_for_error(GPtrArray *jobs, const gchar *message){
	guint i;
	for (i = 0; i < jobs->len; i++) {
		Job *job = g_ptr_array_index(jobs, i);
		const gchar *p = message;
		if (job->kind != JOB_PAGE)
			continue;
		while ((p = strstr(p, job->path)) != NULL) {
			if (p == message || (!g_ascii_isalnum(p[-1]) && p[-1] != '-' && p[-1] != '_'))
				return job;
			p += strlen(job->path);
		}
	}
	return NULL;
}

static void
add_file(JsonArray *files, const gchar *path, const gchar *content){
	JsonObject *file = json_object_new();
	json_object_set_string_member(file, "path", path);
	json_object_set_string_member(file, "content", content);
	json_array_add_object_element(files, file);
}

/* Specifications, artwork and the composed site as one bundle. */
static gchar *
site_bundle(LsComponents *components, JsonNode *plan, GPtrArray *jobs, const gchar *base_url, GError **error){
	g_autoptr(JsonObject) pages = json_object_new();
	g_autoptr(JsonNode) composed = NULL;
	g_autoptr(JsonNode) root = json_node_new(JSON_NODE_OBJECT);
	g_autofree gchar *text = NULL;
	JsonObject *bundle;
	JsonArray *files;
	guint i;
	for (i = 0; i < jobs->len; i++) {
		Job *job = g_ptr_array_index(jobs, i);
		if (job->kind == JOB_PAGE)
			json_object_set_member(pages, job->path, json_node_ref(job->spec));
	}
	text = ls_components_compose(components, plan, pages, base_url, error);
	if (text == NULL)
		return NULL;
	composed = ls_json_parse(text, error);
	if (composed == NULL)
		return NULL;
	bundle = json_node_get_object(composed);
	files = json_object_get_array_member(bundle, "files");
	for (i = 0; i < jobs->len; i++) {
		Job *job = g_ptr_array_index(jobs, i);
		if (job->kind == JOB_PAGE) {
			g_autofree gchar *slug = route_slug(job->path);
			g_autofree gchar *path = g_strconcat(SPEC_DIR, slug, ".json", NULL);
			g_autofree gchar *spec = ls_json_encode(job->spec);
			add_file(files, path, spec);
		} else
			add_file(files, job->path, job->svg);
	}
	json_node_set_object(root, bundle);
	return ls_json_encode(root);
}

static const gchar *
site_base_url(LsRecipe *recipe, JsonObject *options){
	JsonObject *site = json_object_get_object_member(ls_recipe_get_root(recipe), "site");
	JsonObject *contact = site != NULL && json_object_has_member(site, "contact")
		? json_object_get_object_member(site, "contact") : NULL;
	const gchar *url = contact != NULL ? ls_string(contact, "url") : NULL;
	if (options != NULL && ls_string(options, "base_url") != NULL)
		return ls_string(options, "base_url");
	return url != NULL && g_str_has_prefix(url, "https://") ? url : NULL;
}

static gboolean
pages_execute(LsStep *self, LsRecipe *recipe, JsonObject *config, LsPrompts *prompts,
	LsContext *context, LsProvider *provider, GCancellable *cancellable, GError **error){
	JsonObject *options = step_options(config);
	gboolean review = options != NULL && g_strcmp0(ls_string(options, "mode"), "review") == 0;
	guint parallel = options != NULL && json_object_has_member(options, "parallel")
		? (guint)json_object_get_int_member(options, "parallel") : DEFAULT_PARALLEL;
	const LsArtifact *artifact = ls_context_lookup(context, PLAN_PATH);
	g_autoptr(LsComponents) components = NULL;
	g_autoptr(JsonNode) plan = NULL;
	g_autoptr(JsonObject) literals = NULL;
	g_autoptr(GPtrArray) jobs = g_ptr_array_new_with_free_func(job_free);
	g_autofree gchar *reference = NULL, *page_schema = NULL, *art_schema = NULL, *snapshot = NULL;
	g_autofree gchar *status = NULL;
	const gchar *system = ls_prompts_get_spec_system(prompts);
	JsonArray *pages, *art;
	JsonObject *plan_object;
	gint64 retries;
	guint i, art_jobs = 0;
	if (!ls_pages_step_check_options(options, error) || !ls_spec_step_check(prompts, recipe, config, error))
		return FALSE;
	if (artifact == NULL || ls_artifact_is_binary(artifact)) {
		g_set_error_literal(error, LS_ERROR, LS_ERROR_INVALID,
			"The pages step needs " PLAN_PATH "; run a plan step first");
		return FALSE;
	}
	components = ls_recipe_new_components(recipe, error);
	if (components == NULL)
		return FALSE;
	plan = ls_json_parse(ls_artifact_get_content(artifact), error);
	if (plan == NULL || !ls_components_check_plan(components, plan, error))
		return FALSE;
	plan_object = json_node_get_object(plan);
	literals = acceptance_literals(recipe);
	reference = ls_components_describe(components);
	page_schema = ls_components_dup_page_schema(components);
	art_schema = ls_components_dup_art_schema(components);
	pages = json_object_get_array_member(plan_object, "pages");
	art = json_object_has_member(plan_object, "art") ? json_object_get_array_member(plan_object, "art") : NULL;
	for (i = 0; i < json_array_get_length(pages); i++) {
		JsonObject *entry = json_array_get_object_element(pages, i);
		const gchar *path = ls_string(entry, "path");
		JsonArray *required = json_object_has_member(literals, path) ? json_object_get_array_member(literals, path) : NULL;
		g_autoptr(JsonNode) current = NULL;
		Job *job = g_new0(Job, 1);
		g_ptr_array_add(jobs, job);
		job->kind = JOB_PAGE;
		job->path = g_strdup(path);
		job->settings = spec_settings(recipe, config, page_schema);
		if (review) {
			g_autofree gchar *slug = route_slug(path);
			g_autofree gchar *spec_path = g_strconcat(SPEC_DIR, slug, ".json", NULL);
			const LsArtifact *spec = ls_context_lookup(context, spec_path);
			if (spec == NULL || ls_artifact_is_binary(spec) ||
				(current = ls_json_parse(ls_artifact_get_content(spec), error)) == NULL) {
				if (error != NULL && *error == NULL)
					g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "A review needs %s from an earlier pages step", spec_path);
				return FALSE;
			}
		}
		job->prompt = page_prompt(prompts, recipe, config, review ? "page-review" : "page", reference,
			plan_object, entry, required, current, error);
		if (job->prompt == NULL)
			return FALSE;
	}
	for (i = 0; !review && art != NULL && i < json_array_get_length(art); i++) {
		JsonObject *entry = json_array_get_object_element(art, i);
		Job *job = g_new0(Job, 1);
		g_ptr_array_add(jobs, job);
		job->kind = JOB_ART;
		job->path = g_strdup(ls_string(entry, "path"));
		job->settings = spec_settings(recipe, config, art_schema);
		job->prompt = art_prompt(prompts, recipe, config, components, plan_object, entry, error);
		art_jobs++;
		if (job->prompt == NULL)
			return FALSE;
	}
	retries = json_object_get_int_member(((Job *)g_ptr_array_index(jobs, 0))->settings, "retries");
	status = g_strdup_printf("%s %u page%s%s with up to %u at once; provider %s",
		review ? "reviewing" : "writing", json_array_get_length(pages), json_array_get_length(pages) == 1 ? "" : "s",
		art_jobs > 0 ? " and artwork" : "", MIN(parallel, jobs->len),
		ls_string(((Job *)g_ptr_array_index(jobs, 0))->settings, "provider"));
	g_signal_emit_by_name(self, "progress", status);
	/* A review rewrites specifications only; planned art is already present. */
	if (!run_jobs(self, provider, system, jobs, parallel, retries, components, plan, cancellable, error))
		return FALSE;
	snapshot = ls_context_snapshot(context);
	for (;;) {
		g_autoptr(GError) local_error = NULL;
		g_autoptr(LsContext) candidate = ls_context_new();
		g_autoptr(GPtrArray) redo = NULL;
		g_autofree gchar *bundle = site_bundle(components, plan, jobs, site_base_url(recipe, options), error);
		Job *culprit;
		if (bundle == NULL)
			return FALSE;
		if (ls_context_apply(candidate, snapshot, &local_error) && ls_context_apply(candidate, bundle, &local_error) &&
			check_acceptance(recipe, config, prompts, candidate, provider, cancellable, &local_error)) {
			if (g_cancellable_set_error_if_cancelled(cancellable, error))
				return FALSE;
			return ls_context_apply(context, bundle, error);
		}
		culprit = job_for_error(jobs, local_error->message);
		if (culprit == NULL || culprit->attempts > retries || g_cancellable_is_cancelled(cancellable)) {
			g_propagate_error(error, g_steal_pointer(&local_error));
			return FALSE;
		}
		{
			g_autofree gchar *line = first_line(local_error->message);
			g_free(status);
			status = g_strdup_printf("acceptance rejected %s: %s; rewriting it", culprit->path, line);
			g_signal_emit_by_name(self, "progress", status);
		}
		g_free(culprit->feedback);
		culprit->feedback = g_strdup_printf("The composed %s failed the recipe's acceptance check: %s",
			culprit->path, local_error->message);
		redo = g_ptr_array_new();
		g_ptr_array_add(redo, culprit);
		if (!run_jobs(self, provider, system, redo, 1, retries, components, plan, cancellable, error))
			return FALSE;
	}
}

static void
ls_pages_step_class_init(LsPagesStepClass *klass){
	LS_STEP_CLASS(klass)->execute = pages_execute;
}
static void
ls_pages_step_init(LsPagesStep *self){
	(void)self;
}

/* ---- Preflight and preview --------------------------------------------- */

gboolean
ls_spec_step_check(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, GError **error){
	const gchar *type = ls_string(config, "type");
	gboolean pages = g_strcmp0(type, "pages") == 0;
	JsonObject *options = step_options(config);
	gboolean review = pages && options != NULL && g_strcmp0(ls_string(options, "mode"), "review") == 0;
	const gchar *stages[3];
	JsonArray *steps = ls_recipe_get_steps(recipe);
	gboolean planned = FALSE;
	guint i, count = 0;
	if (pages && !ls_pages_step_check_options(options, error))
		return FALSE;
	if (!pages && !check_plan_options(recipe, config, error))
		return FALSE;
	if (!pages)
		stages[count++] = ls_string(config, "prompt") != NULL ? ls_string(config, "prompt") : "plan";
	else if (review)
		stages[count++] = "page-review";
	else {
		stages[count++] = "page";
		stages[count++] = "art";
	}
	for (i = 0; i < count; i++) {
		g_autofree gchar *text = ls_prompts_render_spec(prompts, recipe, config, stages[i], NULL, "{}", error);
		if (text == NULL)
			return FALSE;
	}
	if (!pages)
		return TRUE;
	for (i = 0; i < json_array_get_length(steps); i++) {
		JsonObject *step = json_array_get_object_element(steps, i);
		if (step == config || g_strcmp0(ls_string(step, "id"), ls_string(config, "id")) == 0)
			break;
		planned |= g_strcmp0(ls_string(step, "type"), "plan") == 0;
	}
	if (!planned) {
		g_set_error(error, LS_ERROR, LS_ERROR_INVALID, "Step '%s' needs a plan step before it",
			ls_string(config, "id"));
		return FALSE;
	}
	return TRUE;
}

gchar *
ls_spec_step_preview(LsPrompts *prompts, LsRecipe *recipe, JsonObject *config, GError **error){
	g_autoptr(LsComponents) components = ls_recipe_new_components(recipe, error);
	g_autofree gchar *body = NULL;
	JsonObject *options = step_options(config);
	if (components == NULL || !ls_spec_step_check(prompts, recipe, config, error))
		return NULL;
	if (g_strcmp0(ls_string(config, "type"), "plan") == 0) {
		g_autoptr(JsonObject) identity = plan_identity(components, recipe, config);
		g_autoptr(JsonArray) photos = plan_photos(recipe, NULL, NULL);
		body = plan_prompt(prompts, recipe, config, components, identity, photos, error);
	} else {
		g_autofree gchar *reference = ls_components_describe(components);
		body = ls_prompts_render_spec(prompts, recipe, config,
			options != NULL && g_strcmp0(ls_string(options, "mode"), "review") == 0 ? "page-review" : "page",
			reference, "{\"route\": \"(each planned route)\", \"plan\": \"(written by the plan step)\"}", error);
	}
	if (body == NULL)
		return NULL;
	return g_strdup_printf("%s\n\n%s", ls_prompts_get_spec_system(prompts), body);
}
