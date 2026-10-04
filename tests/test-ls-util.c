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
		"a@.example", "a@x.example.", "a@x..example", "<a@x.example>", "a@x.example\n", "o'hara@x.example", ""};
	guint i;
	for (i = 0; i < G_N_ELEMENTS(good); i++)
		if (!ls_email_valid(good[i]))
			g_error("'%s' was refused", good[i]);
	for (i = 0; i < G_N_ELEMENTS(bad); i++)
		if (ls_email_valid(bad[i]))
			g_error("'%s' was accepted", bad[i]);
	g_assert_false(ls_email_valid(NULL));
}

int
main(int argc, char **argv){
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/util/phone-numbers", phone_numbers);
	g_test_add_func("/util/email-addresses", email_addresses);
	return g_test_run();
}
