#include <stddef.h>
#include <stdint.h>

#include "identity.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	PgOAuthIdentityPolicy policy = {
		.max_subject_size = 1024,
		.max_authn_id_size = 1024,
	};
	PgOAuthIdentity identity;

	if (size > 3072)
		return 0;
	if (pg_oauth_identity_build((const char *) data, size, &policy,
								&identity) == PG_OAUTH_IDENTITY_OK)
		pg_oauth_identity_clear(&identity);
	return 0;
}
