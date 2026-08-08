#include <stddef.h>
#include <stdint.h>

#include "jwks.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	PgOAuthJwksPolicy policy = {
		.max_jwks_size = 65536,
		.max_keys = 16,
		.max_key_id_size = 1024,
		.allowed_algorithms = PG_OAUTH_ALGORITHM_RS256 |
		PG_OAUTH_ALGORITHM_ES256,
		.minimum_rsa_bits = 2048,
		.maximum_rsa_bits = 8192,
	};
	PgOAuthSelectedJwk selected;

	if (size > policy.max_jwks_size)
		return 0;
	if (pg_oauth_jwks_select((const char *) data, size, "fuzz-key",
							 PG_OAUTH_ALGORITHM_RS256, &policy, &selected) == PG_OAUTH_JWKS_OK)
		pg_oauth_selected_jwk_clear(&selected);
	return 0;
}
