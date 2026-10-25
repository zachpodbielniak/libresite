/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
G_BEGIN_DECLS
#define LS_ERROR (ls_error_quark())
#define LS_MAX_DOCUMENT (16 * 1024 * 1024)
#define LS_MAX_PROJECT (64 * 1024 * 1024)
/**
 * LsError:
 * @LS_ERROR_INVALID: Invalid input or provider output.
 * @LS_ERROR_CONFLICT: Conflicting output or workspace.
 * @LS_ERROR_PROVIDER: Provider failure.
 * @LS_ERROR_LIMIT: Resource limit exceeded.
 * @LS_ERROR_VALIDATION: Native validation rejected generated artifacts.
 * @LS_ERROR_RECOVERY: Held/invalid workspace lock or invalid resume checkpoint.
 * @LS_ERROR_APPROVAL: A native gate is waiting for approval. The previous step
 *   is checkpointed; resume with the gate approved to continue.
 */
typedef enum {
	LS_ERROR_INVALID, LS_ERROR_CONFLICT, LS_ERROR_PROVIDER, LS_ERROR_LIMIT,
	LS_ERROR_VALIDATION, LS_ERROR_RECOVERY, LS_ERROR_APPROVAL
} LsError;
GQuark ls_error_quark(void);
/* Internal helpers shared by the parser, artifact store and CLI. */
JsonNode *ls_yaml_parse(const gchar *text, GError **error);
gchar *ls_read_text(const gchar *path, GError **error);
gboolean ls_object_keys(JsonObject *object, const gchar *keys, GError **error);
const gchar *ls_string(JsonObject *object, const gchar *key);
gboolean ls_path_valid(const gchar *path);
/* A CSS object-position: one or two of left/center/right/top/bottom, or two 0-100% values. */
gboolean ls_focus_valid(const gchar *focus);
/**
 * ls_phone_tel:
 * @phone: (nullable): a phone number as supplied, such as "+1 (555) 014-2270"
 *
 * Checks a supplied phone number (digits, spaces, + . - ( ) only, at most 40
 * bytes, 3 to 15 digits, + only before the first digit) and builds its tel:
 * address from the + and the digits.
 * Returns: (transfer full) (nullable): the tel: address, or NULL when @phone is not a phone number
 */
gchar *ls_phone_tel(const gchar *phone);
/**
 * ls_email_valid:
 * @email: (nullable): an address as supplied
 *
 * One @ between a local part of up to 64 characters and a dotted domain of
 * letters, digits, dots and hyphens. Printable ASCII, without spaces, quotes,
 * brackets, separators or the ? # % that would change a mailto: link. At most
 * 254 bytes.
 * Returns: whether @email is safe to draw as a mailto: link
 */
gboolean ls_email_valid(const gchar *email);
/**
 * ls_credit_valid:
 * @credit: (nullable): a caller's footer credit, as supplied
 *
 * A footer credit is exactly {text, url}: text one line of 1 to 80
 * characters, url an https:// address with a host of at most 300 bytes,
 * without spaces, quotes, angle brackets, backslashes or backquotes.
 * Returns: whether @credit is a credit the builder draws
 */
gboolean ls_credit_valid(JsonObject *credit);
/**
 * ls_form_action_valid:
 * @action: (nullable): where a supplied live form posts
 *
 * A same-site path such as /api/forms/contact (letters, digits, / _ . -, no
 * "//" start and no "..") or an https:// address with a host; 2 to 300
 * bytes, without spaces, quotes, angle brackets, backslashes, backquotes,
 * ? or #.
 * Returns: whether @action is a form action the builder draws
 */
gboolean ls_form_action_valid(const gchar *action);
/**
 * ls_form_valid:
 * @form: (nullable): a supplied live form, as site.form gives it
 *
 * Exactly the object the recipe accepts: action under
 * ls_form_action_valid(), one to twelve fields, and an optional honeypot
 * name that is not one of those fields. A field is name, label, and
 * optionally type (text, email, tel, url, number, date or textarea),
 * required, autocomplete, maxlength and hint. The object is not modified.
 * Returns: whether @form is a form the builder draws
 */
gboolean ls_form_valid(JsonObject *form);
/**
 * ls_form_requirements:
 *
 * The refusal sentence for a form ls_form_valid() rejects. Recipe loading
 * and the trusted editor share it, so both name the same shape.
 * Returns: (transfer none): the site.form requirements
 */
const gchar *ls_form_requirements(void);
/**
 * ls_booking_valid:
 * @booking: (nullable): a caller's online booking page, as supplied
 *
 * A booking page is exactly {href}: where the "Book online" action leads, a
 * same-site path or an https:// address under the rules of
 * ls_form_action_valid().
 * Returns: whether @booking is a booking page the builder links
 */
gboolean ls_booking_valid(JsonObject *booking);
gchar *ls_json_encode(JsonNode *node);
/* Strict JSON boundary: rejects duplicate members, NUL escapes and deep trees. */
JsonNode *ls_json_parse(const gchar *text, GError **error);
/**
 * ls_git_run:
 * @directory: working directory for git
 * @arguments: (array zero-terminated=1): git arguments after the command name
 * @output: (out) (optional) (transfer full): standard output when requested
 * @cancellable: (nullable): cancellation token
 * @error: return location for an error
 *
 * Runs git without inherited GIT_* variables, system or global configuration,
 * hooks, attribute files or signing, committing as Libresite. Cancellation
 * reaps the child.
 * Returns: whether git exited successfully
 */
gboolean ls_git_run(const gchar *directory, const gchar *const *arguments, gchar **output,
	GCancellable *cancellable, GError **error);
G_END_DECLS
