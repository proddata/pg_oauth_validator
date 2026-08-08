#include <stddef.h>
#include <stdint.h>

#include "identity.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	PgOAuthIdentityPolicy policy = {
		.max_issuer_size = 2048,
		.max_subject_size = 1024,
		.max_authn_id_size = 1024,
	};
	PgOAuthIdentity identity;
	size_t		split;

	if (size > 3072)
		return 0;
	split = size / 2;
	if (pg_oauth_identity_build((const char *) data, split,
								(const char *) data + split, size - split, &policy,
								&identity) == PG_OAUTH_IDENTITY_OK)
		pg_oauth_identity_clear(&identity);
	return 0;
}
