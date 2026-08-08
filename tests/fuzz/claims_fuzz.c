#include <stddef.h>
#include <stdint.h>

#include <jansson.h>

#include "claims.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	PgOAuthClaimsPolicy policy = {
		.issuer = "https://issuer.example/",
		.audiences = "postgres://primary,postgres://secondary",
		.required_scopes = "connect:postgres read:metadata",
		.identity_claim = "sub",
		.current_time = 1000,
		.clock_skew_seconds = 60,
		.max_identity_size = 1024,
		.max_audiences = 16,
		.max_scopes = 64,
	};
	PgOAuthValidatedClaims claims;
	json_error_t error;
	json_t	   *payload;

	if (size > 16384)
		return 0;
	payload = json_loadb((const char *) data, size, JSON_REJECT_DUPLICATES,
						 &error);
	if (payload != NULL)
	{
		(void) pg_oauth_claims_validate(payload, &policy, &claims);
		json_decref(payload);
	}
	return 0;
}
