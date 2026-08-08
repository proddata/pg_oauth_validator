#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "validator.h"

static void
fail(const char *message)
{
	fprintf(stderr, "validator_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthValidatorPolicy
policy(const char *issuer, int64_t current_time)
{
	PgOAuthValidatorPolicy value = {
		.envelope = {
			.max_token_size = 16384,
			.max_header_size = 4096,
			.max_payload_size = 12288,
			.allowed_algorithms = PG_OAUTH_ALGORITHM_RS256,
			.required_token_type = "at+jwt",
		},
		.issuer_key = {
			.metadata_http = {2048, 65536, 500, 1000, "localhost", NULL, NULL, true},
			.metadata = {65536, 2048, true},
			.jwks_http = {2048, 65536, 500, 1000, "localhost", NULL, NULL, true},
			.jwks = {65536, 16, 1024, PG_OAUTH_ALGORITHM_RS256, 2048, 8192},
		},
		.claims = {
			.issuer = issuer,
			.audiences = "postgres://primary",
			.required_scopes = "connect:postgres",
			.identity_claim = "sub",
			.current_time = current_time,
			.clock_skew_seconds = 60,
			.max_identity_size = 1024,
			.max_audiences = 16,
			.max_scopes = 64,
		},
		.identity = {2048, 1024, 1024},
	};

	return value;
}

static void
expect(const char *token, const char *metadata_url,
	   PgOAuthValidatorPolicy *selected_policy, PgOAuthValidatorError expected,
	   const char *message, PgOAuthValidatorResult *result)
{
	if (pg_oauth_validate_access_token(token, strlen(token), metadata_url,
									   selected_policy, result) != expected)
		fail(message);
	if (expected != PG_OAUTH_VALIDATOR_OK && result->identity.value != NULL)
		fail("rejected token retained an authenticated identity");
}

int
main(int argc, char **argv)
{
	PgOAuthValidatorPolicy selected_policy;
	PgOAuthValidatorResult result;
	int64_t		current_time;

	if (argc != 11)
		fail("expected issuer, time, seven tokens, and expected identity");
	if (sscanf(argv[2], "%" SCNd64, &current_time) != 1)
		fail("invalid test time");
	selected_policy = policy(argv[1], current_time);
	expect(argv[3], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_OK,
		   "valid access token was rejected", &result);
	if (strcmp(result.identity.value, argv[10]) != 0 ||
		result.expires_at != current_time + 300)
		fail("validated identity or expiry was incorrect");
	pg_oauth_validator_result_clear(&result);

	expect(argv[4], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_SIGNATURE,
		   "modified signed payload was accepted", &result);
	if (result.signature_error != PG_OAUTH_SIGNATURE_INVALID)
		fail("signature failure lost its typed cause");
	pg_oauth_validator_result_clear(&result);
	expect(argv[5], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_CLAIMS,
		   "wrong signed issuer was accepted", &result);
	if (result.claims_error != PG_OAUTH_CLAIMS_INVALID_ISSUER)
		fail("issuer failure lost its typed cause");
	pg_oauth_validator_result_clear(&result);
	expect(argv[6], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_CLAIMS,
		   "missing required scope was accepted", &result);
	if (result.claims_error != PG_OAUTH_CLAIMS_INSUFFICIENT_SCOPE)
		fail("scope failure lost its typed cause");
	pg_oauth_validator_result_clear(&result);
	expect(argv[7], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_CLAIMS,
		   "expired token was accepted", &result);
	if (result.claims_error != PG_OAUTH_CLAIMS_EXPIRED)
		fail("expiry failure lost its typed cause");
	pg_oauth_validator_result_clear(&result);
	expect(argv[8], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_ENVELOPE,
		   "ID-token type was accepted", &result);
	if (result.envelope_error != PG_OAUTH_JWT_ENVELOPE_INVALID_TOKEN_TYPE)
		fail("token-type failure lost its typed cause");
	pg_oauth_validator_result_clear(&result);
	expect(argv[9], argv[1], &selected_policy, PG_OAUTH_VALIDATOR_ISSUER_KEY,
		   "unknown key identifier was accepted", &result);
	if (result.jwks_error != PG_OAUTH_JWKS_KEY_NOT_FOUND)
		fail("key-selection failure lost its typed cause");
	pg_oauth_validator_result_clear(&result);

	if (strstr(pg_oauth_validator_error_code(PG_OAUTH_VALIDATOR_SIGNATURE),
			   "principal") != NULL)
		fail("stable validator error exposed token material");
	return EXIT_SUCCESS;
}
