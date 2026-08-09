#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "claims.h"

static void
fail(const char *message)
{
	fprintf(stderr, "claims_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthClaimsPolicy
valid_policy(void)
{
	PgOAuthClaimsPolicy policy = {
		.issuer = "https://issuer.example/",
		.audiences = "postgres://primary, postgres://secondary",
		.required_scopes = "connect:postgres read:metadata",
		.identity_claim = "sub",
		.current_time = 1000,
		.clock_skew_seconds = 60,
		.max_identity_size = 1024,
		.max_audiences = 16,
		.max_scopes = 64,
	};

	return policy;
}

static json_t *
parse_payload(const char *document)
{
	json_error_t error;
	json_t	   *payload = json_loads(document, JSON_REJECT_DUPLICATES, &error);

	if (payload == NULL)
		fail("test fixture JSON was invalid");
	return payload;
}

static void
expect_error(const char *document, PgOAuthClaimsPolicy *policy,
			 PgOAuthClaimsError expected, const char *message)
{
	json_t	   *payload = parse_payload(document);
	PgOAuthValidatedClaims claims;

	if (pg_oauth_claims_validate(payload, policy, &claims) != expected)
		fail(message);
	if (claims.identity != NULL || claims.identity_length != 0)
		fail("rejected claims retained identity data");
	json_decref(payload);
}

int
main(void)
{
	static const char valid[] =
		"{\"iss\":\"https://issuer.example/\","
		"\"aud\":\"postgres://primary\",\"exp\":1100,\"nbf\":900,"
		"\"iat\":900,\"sub\":\"principal-123\","
		"\"scope\":\"connect:postgres read:metadata extra\"}";
	PgOAuthClaimsPolicy policy = valid_policy();
	PgOAuthValidatedClaims claims;
	json_t	   *payload = parse_payload(valid);

	if (pg_oauth_claims_validate(payload, &policy, &claims) !=
		PG_OAUTH_CLAIMS_OK)
		fail("valid claims were rejected");
	if (claims.identity_length != strlen("principal-123") ||
		memcmp(claims.identity, "principal-123", claims.identity_length) != 0 ||
		claims.expires_at != 1100)
		fail("validated claims result was incorrect");
	json_decref(payload);

	payload = parse_payload(
							"{\"iss\":\"https://issuer.example/\","
							"\"aud\":[\"unrelated\",\"postgres://secondary\"],\"exp\":1100,"
							"\"sub\":\"p\",\"scope\":[\"read:metadata\",\"connect:postgres\"]}");
	if (pg_oauth_claims_validate(payload, &policy, &claims) !=
		PG_OAUTH_CLAIMS_OK)
		fail("valid audience and scope arrays were rejected");
	json_decref(payload);

	expect_error(
				 "{\"aud\":\"postgres://primary\",\"exp\":1100,"
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ISSUER,
				 "missing issuer was accepted");
	expect_error(
				 "{\"iss\":1,\"aud\":\"postgres://primary\",\"exp\":1100,"
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ISSUER,
				 "non-string issuer was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ISSUER,
				 "issuer URL normalization was applied");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary/extra\","
				 "\"exp\":1100,\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_AUDIENCE,
				 "audience substring was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"exp\":1100,"
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_AUDIENCE,
				 "missing audience was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":[],\"exp\":1100,"
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_AUDIENCE,
				 "empty audience array was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\","
				 "\"aud\":[\"postgres://primary\",\"postgres://primary\"],\"exp\":1100,"
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_AUDIENCE,
				 "duplicate audience was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":1,\"exp\":1100,"
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_AUDIENCE,
				 "non-string audience was accepted");

	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_MISSING_EXPIRY, "missing expiry was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100.0,\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_EXPIRY,
				 "floating-point expiry was accepted");

	policy = valid_policy();
	policy.current_time = 1060;
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1000,\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_EXPIRED,
				 "token at expiry-skew boundary was accepted");
	policy.current_time = 1059;
	payload = parse_payload(
							"{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
							"\"exp\":1000,\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\"}");
	if (pg_oauth_claims_validate(payload, &policy, &claims) != PG_OAUTH_CLAIMS_OK)
		fail("token inside expiry skew was rejected");
	json_decref(payload);

	policy = valid_policy();
	payload = parse_payload(
							"{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
							"\"exp\":1100,\"nbf\":1060,\"iat\":1060,\"sub\":\"p\","
							"\"scope\":\"connect:postgres read:metadata\"}");
	if (pg_oauth_claims_validate(payload, &policy, &claims) != PG_OAUTH_CLAIMS_OK)
		fail("temporal claims at the future-skew boundary were rejected");
	json_decref(payload);
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"nbf\":\"1060\",\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_NOT_BEFORE,
				 "non-integer not-before was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"iat\":false,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ISSUED_AT,
				 "non-integer issued-at was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"nbf\":1061,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_NOT_YET_VALID,
				 "not-before beyond skew was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"iat\":1061,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_ISSUED_IN_FUTURE,
				 "issued-at beyond skew was accepted");

	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_IDENTITY,
				 "missing configured identity was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":1,\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_IDENTITY,
				 "non-string identity was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"line\\nfeed\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_IDENTITY,
				 "control character in identity was accepted");

	policy = valid_policy();
	policy.identity_claim = "uid";
	policy.max_identity_size = 1;
	payload = parse_payload(
							"{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
							"\"exp\":1100,\"uid\":\"u\","
							"\"scope\":\"connect:postgres read:metadata\"}");
	if (pg_oauth_claims_validate(payload, &policy, &claims) != PG_OAUTH_CLAIMS_OK)
		fail("configured identity at the size boundary was rejected");
	json_decref(payload);
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"u\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_IDENTITY,
				 "missing custom identity claim was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"uid\":[\"u\"],"
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_IDENTITY,
				 "wrong-type custom identity claim was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"uid\":\"uu\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_IDENTITY,
				 "identity beyond the size boundary was accepted");
	policy = valid_policy();

	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_SCOPE, "missing scope was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\",\"scope\":\"connect:postgres\"}",
				 &policy, PG_OAUTH_CLAIMS_INSUFFICIENT_SCOPE,
				 "missing required scope was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres  read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_SCOPE,
				 "empty scope token was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata connect:postgres\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_SCOPE,
				 "duplicate scope token was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\",\"scp\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_SCOPE,
				 "provider-specific scp claim was accepted implicitly");

	if (strstr(pg_oauth_claims_error_code(PG_OAUTH_CLAIMS_INVALID_IDENTITY),
			   "principal") != NULL)
		fail("claims error code exposed claim material");

	policy = valid_policy();
	policy.roles_claim = "roles";
	policy.requested_role = "reporting";
	policy.max_roles = 4;
	policy.max_role_size = 63;
	policy.require_requested_role = true;
	payload = parse_payload(
							"{\"iss\":\"https://issuer.example/\","
							"\"aud\":\"postgres://primary\",\"exp\":1100,"
							"\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\","
							"\"roles\":[\"app_reader\",\"reporting\"]}");
	if (pg_oauth_claims_validate(payload, &policy, &claims) != PG_OAUTH_CLAIMS_OK)
		fail("authorized requested role was rejected");
	json_decref(payload);
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"app_reader\"]}",
				 &policy, PG_OAUTH_CLAIMS_UNAUTHORIZED_ROLE,
				 "role absent from delegated claim was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\",\"roles\":\"reporting\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "scalar delegated roles claim was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"reporting\",\"reporting\"]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "duplicate delegated role was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\"}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "missing delegated roles claim was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\",\"roles\":[]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "empty delegated roles array was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\",\"roles\":[\"\"]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "empty delegated role was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"reporting\",7]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "non-string delegated role was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"reporting\\nadmin\"]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "control character in delegated role was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"one\",\"two\",\"three\",\"four\",\"reporting\"]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "oversized delegated roles array was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]}",
				 &policy, PG_OAUTH_CLAIMS_INVALID_ROLES,
				 "oversized delegated role was accepted");
	expect_error(
				 "{\"iss\":\"https://issuer.example/\",\"aud\":\"postgres://primary\","
				 "\"exp\":1100,\"sub\":\"p\","
				 "\"scope\":\"connect:postgres read:metadata\","
				 "\"roles\":[\"Reporting\"]}",
				 &policy, PG_OAUTH_CLAIMS_UNAUTHORIZED_ROLE,
				 "delegated role comparison was not case-sensitive");

	policy.roles_claim = "https://company.example/postgres_roles";
	payload = parse_payload(
							"{\"iss\":\"https://issuer.example/\","
							"\"aud\":\"postgres://primary\",\"exp\":1100,"
							"\"sub\":\"p\",\"scope\":\"connect:postgres read:metadata\","
							"\"https://company.example/postgres_roles\":[\"reporting\"]}");
	if (pg_oauth_claims_validate(payload, &policy, &claims) != PG_OAUTH_CLAIMS_OK)
		fail("custom delegated roles claim was rejected");
	json_decref(payload);
	return EXIT_SUCCESS;
}
