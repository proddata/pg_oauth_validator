#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64url.h"
#include "identity.h"

static void
fail(const char *message)
{
	fprintf(stderr, "identity_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthIdentityPolicy
valid_policy(void)
{
	PgOAuthIdentityPolicy policy = {
		.max_issuer_size = 2048,
		.max_subject_size = 1024,
		.max_authn_id_size = 1024,
	};

	return policy;
}

static void
expect_error(const char *issuer, size_t issuer_length, const char *subject,
			 size_t subject_length, PgOAuthIdentityPolicy *policy,
			 PgOAuthIdentityError expected, const char *message)
{
	PgOAuthIdentity identity;

	if (pg_oauth_identity_build(issuer, issuer_length, subject, subject_length,
								policy, &identity) != expected)
		fail(message);
	if (identity.value != NULL || identity.length != 0)
		fail("rejected identity retained output");
}

static void
expect_round_trip(const char *issuer, const char *subject)
{
	PgOAuthIdentityPolicy policy = valid_policy();
	PgOAuthIdentity identity;
	const char *separator;
	size_t		issuer_encoded_length;
	size_t		subject_encoded_length;
	size_t		issuer_decoded_length;
	size_t		subject_decoded_length;
	unsigned char issuer_decoded[256];
	unsigned char subject_decoded[256];

	if (pg_oauth_identity_build(issuer, strlen(issuer), subject, strlen(subject),
								&policy, &identity) != PG_OAUTH_IDENTITY_OK)
		fail("valid identity was rejected");
	if (identity.length != strlen(identity.value) ||
		strncmp(identity.value, "v1.", 3) != 0)
		fail("identity prefix or length was incorrect");
	separator = strchr(identity.value + 3, '.');
	if (separator == NULL || strchr(separator + 1, '.') != NULL)
		fail("identity framing was ambiguous");
	issuer_encoded_length = (size_t) (separator - (identity.value + 3));
	subject_encoded_length = identity.length - (size_t) (separator + 1 - identity.value);
	if (!pg_oauth_base64url_decoded_size(issuer_encoded_length,
			sizeof(issuer_decoded), &issuer_decoded_length) ||
		!pg_oauth_base64url_decode(identity.value + 3, issuer_encoded_length,
			issuer_decoded, issuer_decoded_length) ||
		issuer_decoded_length != strlen(issuer) ||
		memcmp(issuer_decoded, issuer, issuer_decoded_length) != 0)
		fail("issuer did not round trip");
	if (!pg_oauth_base64url_decoded_size(subject_encoded_length,
			sizeof(subject_decoded), &subject_decoded_length) ||
		!pg_oauth_base64url_decode(separator + 1, subject_encoded_length,
			subject_decoded, subject_decoded_length) ||
		subject_decoded_length != strlen(subject) ||
		memcmp(subject_decoded, subject, subject_decoded_length) != 0)
		fail("subject did not round trip");
	pg_oauth_identity_clear(&identity);
}

int
main(void)
{
	PgOAuthIdentityPolicy policy = valid_policy();
	PgOAuthIdentity first;
	PgOAuthIdentity second;
	static const char invalid_utf8[] = {(char) 0xc0, (char) 0xaf};
	static const char c1_control[] = {(char) 0xc2, (char) 0x85};

	expect_round_trip("https://issuer.example/realm|one", "subject.with|delimiters");
	expect_round_trip("https://issuer.example/", "müller-東京");
	if (pg_oauth_identity_build("https://idp.example/realms/acme",
			strlen("https://idp.example/realms/acme"), "248289761001",
			strlen("248289761001"), &policy, &first) != PG_OAUTH_IDENTITY_OK ||
		strcmp(first.value,
			   "v1.aHR0cHM6Ly9pZHAuZXhhbXBsZS9yZWFsbXMvYWNtZQ."
			   "MjQ4Mjg5NzYxMDAx") != 0)
		fail("documented authn_id example drifted");
	pg_oauth_identity_clear(&first);

	if (pg_oauth_identity_build("issuer:a", strlen("issuer:a"), "b.c",
			strlen("b.c"), &policy, &first) != PG_OAUTH_IDENTITY_OK ||
		pg_oauth_identity_build("issuer:a.b", strlen("issuer:a.b"), "c",
			strlen("c"), &policy, &second) != PG_OAUTH_IDENTITY_OK)
		fail("collision fixtures were rejected");
	if (strcmp(first.value, second.value) == 0)
		fail("distinct issuer/subject pairs collided");
	pg_oauth_identity_clear(&first);
	pg_oauth_identity_clear(&second);

	expect_error("", 0, "subject", strlen("subject"), &policy,
		PG_OAUTH_IDENTITY_INVALID_ISSUER, "empty issuer was accepted");
	expect_error("issuer", strlen("issuer"), "", 0, &policy,
		PG_OAUTH_IDENTITY_INVALID_SUBJECT, "empty subject was accepted");
	expect_error("issuer\nlog", strlen("issuer\nlog"), "subject",
		strlen("subject"), &policy, PG_OAUTH_IDENTITY_INVALID_ISSUER,
		"control character in issuer was accepted");
	expect_error("issuer", strlen("issuer"), "subject\nlog",
		strlen("subject\nlog"), &policy, PG_OAUTH_IDENTITY_INVALID_SUBJECT,
		"control character in subject was accepted");
	expect_error("issuer", strlen("issuer"), invalid_utf8,
		sizeof(invalid_utf8), &policy, PG_OAUTH_IDENTITY_INVALID_SUBJECT,
		"overlong UTF-8 was accepted");
	expect_error("issuer", strlen("issuer"), c1_control, sizeof(c1_control),
		&policy, PG_OAUTH_IDENTITY_INVALID_SUBJECT,
		"Unicode control character was accepted");

	policy = valid_policy();
	policy.max_authn_id_size = 12;
	expect_error("issuer", strlen("issuer"), "subject", strlen("subject"),
		&policy, PG_OAUTH_IDENTITY_TOO_LARGE,
		"oversized final authn_id was accepted");
	if (strstr(pg_oauth_identity_error_code(PG_OAUTH_IDENTITY_INVALID_SUBJECT),
			   "subject.with") != NULL)
		fail("identity error code exposed identity material");
	return EXIT_SUCCESS;
}
