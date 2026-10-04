/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* Shared input checks: supplied phone numbers and email addresses. */
#include "libresite.h"
#include <string.h>

/* A tel: address holds a leading + and digits only, built from a checked number. */
static void
phone_numbers(void){
	const gchar *good[][2] = {{"+1 (555) 014-2270", "tel:+15550142270"}, {"0161 496.0000", "tel:01614960000"},
		{" +44 20 7946 0000", "tel:+442079460000"}, {"112", "tel:112"}};
	const gchar *bad[] = {"call us", "555-CALL", "12", "1+2 345", "++1 555 0100", "", "   ", "+",
		"1234567890123456789012", "+1 555 0100 ext 2", "+1 555\n0100", "+1 (555) 014-2270 <b>",
		"012345678901234567890123456789012345678901"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(good); i++) {
		g_autofree gchar *tel = ls_phone_tel(good[i][0]);
		g_assert_cmpstr(tel, ==, good[i][1]);
	}
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autofree gchar *tel = ls_phone_tel(bad[i]);
		g_assert_null(tel);
	}
	g_assert_null(ls_phone_tel(NULL));
}

static void
email_addresses(void){
	const gchar *good[] = {"hello@harbor.example", "a.b+c@x.example", "a_b-c@mail.x.example"};
	const gchar *bad[] = {"not-an-address", "a b@x.example", "a@b@x.example", "@x.example", "a@example",
		"a@.example", "a@x.example.", "a@x..example", "<a@x.example>", "a@x.example\n", "o'hara@x.example", "",
		"a@x.example?cc=b", "a@x.example/path", "a@x.example#top", "a?b@x.example", "a@x_y.example"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(good); i++)
		if (!ls_email_valid(good[i]))
			g_error("'%s' was refused", good[i]);
	for (i = 0; i < G_N_ELEMENTS(bad); i++)
		if (ls_email_valid(bad[i]))
			g_error("'%s' was accepted", bad[i]);
	g_assert_false(ls_email_valid(NULL));
}

/* A footer credit: a short line of text and an https:// address, both required. */
static void
credits(void){
	const gchar *good[] = {"{\"text\": \"Built with Example\", \"url\": \"https://example.com/\"}",
		"{\"text\": \"Hosted by A & B\", \"url\": \"https://hosting.example/?ref=site\"}"};
	const gchar *bad[] = {"{}", "{\"text\": \"Built with Example\"}", "{\"url\": \"https://example.com/\"}",
		"{\"text\": \"   \", \"url\": \"https://example.com/\"}",
		"{\"text\": \"Built\\nwith\", \"url\": \"https://example.com/\"}",
		"{\"text\": \"Built with Example\", \"url\": \"http://example.com/\"}",
		"{\"text\": \"Built with Example\", \"url\": \"javascript:alert(1)\"}",
		"{\"text\": \"Built with Example\", \"url\": \"https:///path\"}",
		"{\"text\": \"Built with Example\", \"url\": \"https://example.com/\\\"onmouseover\"}",
		"{\"text\": \"Built with Example\", \"url\": \"https://example.com/\", \"logo\": \"x\"}",
		"{\"text\": 3, \"url\": \"https://example.com/\"}",
		"{\"text\": \"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\", "
		"\"url\": \"https://example.com/\"}"};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(good); i++) {
		g_autoptr(JsonNode) node = ls_json_parse(good[i], NULL);
		if (!ls_credit_valid(json_node_get_object(node)))
			g_error("'%s' was refused", good[i]);
	}
	for (i = 0; i < G_N_ELEMENTS(bad); i++) {
		g_autoptr(JsonNode) node = ls_json_parse(bad[i], NULL);
		g_assert_nonnull(node);
		if (ls_credit_valid(json_node_get_object(node)))
			g_error("'%s' was accepted", bad[i]);
	}
	g_assert_false(ls_credit_valid(NULL));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/util/phone-numbers", phone_numbers);
	g_test_add_func("/util/email-addresses", email_addresses);
	g_test_add_func("/util/credits", credits);
	return g_test_run();
}
