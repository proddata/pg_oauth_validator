#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "jwt_envelope.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	PgOAuthJwtEnvelopePolicy policy = {
		.max_token_size = 16384,
		.max_header_size = 4096,
		.max_payload_size = 12288,
		.allowed_algorithms = PG_OAUTH_ALGORITHM_RS256 |
		PG_OAUTH_ALGORITHM_ES256,
		.required_token_type = "at+jwt",
	};
	PgOAuthJwtEnvelope envelope;

	if (size > policy.max_token_size)
		return 0;
	if (pg_oauth_jwt_envelope_parse((const char *) data, size, &policy,
									&envelope) == PG_OAUTH_JWT_ENVELOPE_OK)
		pg_oauth_jwt_envelope_clear(&envelope);
	return 0;
}
