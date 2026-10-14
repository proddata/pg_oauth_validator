#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "identity.h"

static void
expect_error(const char *value, size_t length, PgOAuthIdentityPolicy *policy,
			 PgOAuthIdentityError expected)
{
	PgOAuthIdentity identity;

	if (pg_oauth_identity_build(value, length, policy, &identity) != expected ||
		identity.value != NULL || identity.length != 0)
	{
		fprintf(stderr, "identity_test: invalid identity was accepted\n");
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	PgOAuthIdentityPolicy policy = {63, 63};
	PgOAuthIdentity identity;
	static const char invalid_utf8[] = {(char) 0xc0, (char) 0xaf};
	static const char c1_control[] = {(char) 0xc2, (char) 0x85};

	if (pg_oauth_identity_build("provider|123", strlen("provider|123"),
								&policy, &identity) != PG_OAUTH_IDENTITY_OK ||
		strcmp(identity.value, "provider|123") != 0)
		return EXIT_FAILURE;
	pg_oauth_identity_clear(&identity);
	expect_error("", 0, &policy, PG_OAUTH_IDENTITY_INVALID_SUBJECT);
	expect_error("line\nfeed", strlen("line\nfeed"), &policy,
				 PG_OAUTH_IDENTITY_INVALID_SUBJECT);
	expect_error(invalid_utf8, sizeof(invalid_utf8), &policy,
				 PG_OAUTH_IDENTITY_INVALID_SUBJECT);
	expect_error(c1_control, sizeof(c1_control), &policy,
				 PG_OAUTH_IDENTITY_INVALID_SUBJECT);
	policy.max_authn_id_size = 3;
	expect_error("large", 5, &policy, PG_OAUTH_IDENTITY_INVALID_SUBJECT);
	return EXIT_SUCCESS;
}
