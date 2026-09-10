/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* API conformance probes for the pinned generation context, not a generated app. */
#include <yaml-glib.h>
#include <htmx-glib.h>
#include <orm.h>
#include <glib/gstdio.h>
#include <string.h>

/* The pinned parser preserves style but coerces quoted numeric scalars during
 * JSON conversion. Strict configuration must check style before scalar types. */
static void
config_types(void){
	g_autoptr(YamlParser) parser = yaml_parser_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(JsonNode) port = NULL;
	YamlMapping *mapping;
	g_assert_true(yaml_parser_load_from_data(parser, "port: 8080\n", -1, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(yaml_parser_get_n_documents(parser), ==, 1);
	g_assert_cmpint(yaml_node_get_node_type(yaml_parser_get_root(parser)), ==, YAML_NODE_MAPPING);
	mapping = yaml_node_get_mapping(yaml_parser_get_root(parser));
	g_assert_cmpint(yaml_node_get_scalar_style(yaml_mapping_get_member(mapping, "port")), ==, YAML_SCALAR_STYLE_PLAIN);
	port = yaml_node_to_json_node(yaml_mapping_get_member(mapping, "port"));
	g_assert_cmpuint(json_node_get_value_type(port), ==, G_TYPE_INT64);
	g_assert_cmpint(json_node_get_int(port), ==, 8080);
	g_clear_pointer(&port, json_node_unref);
	yaml_parser_reset(parser);
	g_assert_true(yaml_parser_load_from_data(parser, "port: '8080'\n", -1, &error));
	g_assert_no_error(error);
	mapping = yaml_node_get_mapping(yaml_parser_get_root(parser));
	port = yaml_node_to_json_node(yaml_mapping_get_member(mapping, "port"));
	g_assert_cmpint(yaml_node_get_scalar_style(yaml_mapping_get_member(mapping, "port")), ==, YAML_SCALAR_STYLE_SINGLE_QUOTED);
	g_assert_cmpstr(yaml_node_get_scalar(yaml_mapping_get_member(mapping, "port")), ==, "8080");
	g_assert_cmpuint(json_node_get_value_type(port), ==, G_TYPE_INT64);
}

/* Use the actual three-argument callback and owned response contract. */
static HtmxResponse *
page_response(HtmxRequest *request, GHashTable *params, gpointer user_data){
	g_autoptr(HtmxBuilder) builder = htmx_builder_new();
	g_autofree gchar *html = NULL;
	HtmxResponse *response;
	(void)params;
	(void)user_data;
	g_assert_cmpstr(htmx_request_get_query_param(request, "filter"), ==, "unread");
	htmx_builder_begin(builder, "form");
	htmx_builder_attr(builder, "method", "post");
	htmx_builder_attr(builder, "action", "/entries");
	htmx_builder_hx_post(builder, "/entries");
	htmx_builder_hx_target(builder, "#entries");
	htmx_builder_hx_swap(builder, HTMX_SWAP_OUTER_HTML);
	htmx_builder_text(builder, "<script>unsafe</script>");
	htmx_builder_end(builder);
	html = htmx_builder_render(builder);
	g_assert_nonnull(strstr(html, "&lt;script&gt;"));
	g_assert_null(strstr(html, "<script>"));
	g_assert_nonnull(strstr(html, "hx-post=\"/entries\""));
	response = htmx_response_new_with_content(html);
	htmx_response_set_status(response, 200);
	htmx_response_add_header(response, "Vary", "HX-Request");
	return response;
}

/* In-process routing verifies ownership without opening a network listener. */
static void
page_api(void){
	g_autoptr(HtmxServer) server = htmx_server_new();
	g_autoptr(HtmxRequest) request = htmx_request_new_for_path(HTMX_METHOD_GET, "/entries?filter=unread");
	g_autoptr(HtmxResponse) response = NULL;
	g_autoptr(GHashTable) params = NULL;
	HtmxRouter *router = htmx_server_get_router(server);
	htmx_router_get(router, "/entries", page_response, NULL);
	htmx_router_post(router, "/entries", page_response, NULL);
	response = htmx_router_match(router, request, &params);
	g_assert_nonnull(response);
}

/* Query iteration has a separate error channel; never equate failure with EOF. */
static gint64
row_count(OrmConnection *connection){
	g_autoptr(GError) error = NULL;
	g_autoptr(OrmResult) result = orm_connection_query_with_params(connection,
		"SELECT count(*) FROM entries", NULL, &error);
	gint64 count;
	g_assert_no_error(error);
	g_assert_nonnull(result);
	g_assert_true(orm_result_next(result));
	count = orm_row_get_integer(orm_result_get_row(result), 0);
	g_assert_false(orm_result_next(result));
	g_assert_null(orm_result_get_error(result));
	return count;
}

/* A temporary file probes first creation, parameter ownership, rollback and
 * reopen persistence through the exact lower-level ORM API in the profile. */
static void
database_api(void){
	g_autofree gchar *temp = g_dir_make_tmp("libresite-stack-api-XXXXXX", NULL);
	g_autofree gchar *path = g_build_filename(temp, "application.db", NULL);
	g_autoptr(GError) error = NULL;
	g_autoptr(OrmEngine) engine = orm_engine_new_sqlite(path, &error);
	g_autoptr(OrmConnection) connection = NULL;
	g_autoptr(OrmTransaction) transaction = NULL;
	GList *params = NULL;
	g_assert_no_error(error);
	connection = orm_engine_connect(engine, &error);
	g_assert_no_error(error);
	g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	g_assert_true(orm_connection_execute(connection,
		"CREATE TABLE entries (id INTEGER PRIMARY KEY, title TEXT NOT NULL UNIQUE)", &error));
	g_assert_no_error(error);
	params = g_list_append(params, orm_value_new_string("'); DROP TABLE entries; --"));
	g_assert_true(orm_connection_execute_with_params(connection,
		"INSERT INTO entries (title) VALUES (?)", params, &error));
	g_assert_no_error(error);
	g_assert_cmpint(row_count(connection), ==, 1);
	/* Constraint failure must be reported and leave the existing row intact. */
	g_assert_false(orm_connection_execute_with_params(connection,
		"INSERT INTO entries (title) VALUES (?)", params, &error));
	g_assert_nonnull(error);
	g_clear_error(&error);
	g_list_free_full(params, (GDestroyNotify)orm_value_free);
	transaction = orm_connection_begin_transaction(connection, &error);
	g_assert_no_error(error);
	g_assert_true(orm_connection_execute(connection, "DELETE FROM entries", &error));
	g_assert_no_error(error);
	g_assert_cmpint(row_count(connection), ==, 0);
	g_assert_true(orm_transaction_rollback(transaction, &error));
	g_assert_no_error(error);
	g_clear_object(&transaction);
	g_assert_cmpint(row_count(connection), ==, 1);
	transaction = orm_connection_begin_transaction(connection, &error);
	g_assert_no_error(error);
	g_assert_true(orm_transaction_commit(transaction, &error));
	g_assert_no_error(error);
	g_clear_object(&transaction);
	orm_connection_close(connection);
	g_clear_object(&connection);
	g_clear_object(&engine);
	engine = orm_engine_new_sqlite(path, &error);
	g_assert_no_error(error);
	connection = orm_engine_connect(engine, &error);
	g_assert_no_error(error);
	g_assert_cmpint(row_count(connection), ==, 1);
	orm_connection_close(connection);
	g_clear_object(&connection);
	g_clear_object(&engine);
	g_assert_cmpint(g_unlink(path), ==, 0);
	g_assert_cmpint(g_rmdir(temp), ==, 0);
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/stack-api/config-types", config_types);
	g_test_add_func("/stack-api/pages", page_api);
	g_test_add_func("/stack-api/database", database_api);
	return g_test_run();
}
