#include "postgres.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "policy.h"

#undef fprintf

static void
fail(const char *message)
{
	fprintf(stderr, "policy_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthPolicyConfig
valid_config(void)
{
	PgOAuthPolicyConfig config = {
		.audiences = "https://postgres.example/",
		.allowed_algorithms = "RS256,ES256",
		.required_token_type = "at+jwt",
		.authn_claim = "sub",
		.allowed_jwks_hosts = "",
		.ca_file = "",
		.clock_skew_ms = 60000,
		.max_token_size = 16384,
		.metadata_default_ttl_ms = 300000,
		.jwks_default_ttl_ms = 300000,
		.cache_max_ttl_ms = 3600000,
		.jwks_stale_grace_ms = 0,
		.unknown_kid_refresh_cooldown_ms = 30000,
	};

	return config;
}

static PgOAuthHbaPolicy
valid_hba(void)
{
	PgOAuthHbaPolicy hba = {
		.issuer = "https://issuer.example/",
		.required_scopes = "connect:postgres",
		.policy_name = "default",
	};

	return hba;
}

static void
expect_error(PgOAuthPolicyConfig *config, PgOAuthHbaPolicy *hba,
			 PgOAuthPolicyError expected, const char *message)
{
	PgOAuthValidationPolicy policy;

	if (pg_oauth_policy_build(config, hba, &policy) != expected)
		fail(message);
}

int
main(void)
{
	PgOAuthPolicyConfig config = valid_config();
	PgOAuthHbaPolicy hba = valid_hba();
	PgOAuthValidationPolicy policy;

	if (pg_oauth_policy_build(&config, &hba, &policy) != PG_OAUTH_POLICY_OK)
		fail("valid default policy was rejected");
	if (strcmp(policy.name, "default") != 0 ||
		strcmp(policy.issuer, hba.issuer) != 0 ||
		strcmp(policy.required_scopes, hba.required_scopes) != 0 ||
		policy.allowed_algorithms !=
		(PG_OAUTH_ALGORITHM_RS256 | PG_OAUTH_ALGORITHM_ES256))
		fail("valid policy was built incorrectly");

	config = valid_config();
	config.audiences = "";
	expect_error(&config, &hba, PG_OAUTH_POLICY_MISSING_AUDIENCE,
				 "empty audiences were accepted");

	config = valid_config();
	config.audiences = "audience-a,,audience-b";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_AUDIENCES,
				 "empty audience entry was accepted");

	config = valid_config();
	config.allowed_algorithms = "none";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_ALGORITHMS,
				 "unsupported algorithm was accepted");

	config = valid_config();
	config.allowed_algorithms = "RS256,RS256";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_ALGORITHMS,
				 "duplicate algorithm was accepted");

	config = valid_config();
	config.required_token_type = "application/at+jwt";
	if (pg_oauth_policy_build(&config, &hba, &policy) != PG_OAUTH_POLICY_OK)
		fail("standard access-token media type was rejected");

	config = valid_config();
	config.required_token_type = "JWT";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_TOKEN_TYPE,
				 "non-access-token type was accepted");

	config = valid_config();
	config.authn_claim = "display name";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_AUTHN_CLAIM,
				 "unsafe identity claim name was accepted");

	config = valid_config();
	config.allowed_jwks_hosts = "keys.example, cdn.example";
	if (pg_oauth_policy_build(&config, &hba, &policy) != PG_OAUTH_POLICY_OK ||
		strcmp(policy.allowed_jwks_hosts, config.allowed_jwks_hosts) != 0)
		fail("valid additional JWKS hosts were rejected");

	config = valid_config();
	config.allowed_jwks_hosts = "keys.example:443";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_JWKS_HOSTS,
				 "JWKS host containing a port was accepted");

	config = valid_config();
	config.allowed_jwks_hosts = "keys.example,,cdn.example";
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_JWKS_HOSTS,
				 "empty JWKS host entry was accepted");

	config = valid_config();
	config.clock_skew_ms = 300001;
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_CLOCK_SKEW,
				 "excessive clock skew was accepted");

	config = valid_config();
	config.max_token_size = 1023;
	expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_TOKEN_SIZE,
				 "undersized token limit was accepted");

#define EXPECT_CACHE_ERROR(field, value, message) \
	do { config = valid_config(); config.field = (value); \
		expect_error(&config, &hba, PG_OAUTH_POLICY_INVALID_CACHE_POLICY, \
			(message)); } while (0)
	EXPECT_CACHE_ERROR(metadata_default_ttl_ms, 999,
		"undersized metadata fallback TTL was accepted");
	EXPECT_CACHE_ERROR(metadata_default_ttl_ms, 3600001,
		"oversized metadata fallback TTL was accepted");
	EXPECT_CACHE_ERROR(jwks_default_ttl_ms, 999,
		"undersized JWKS fallback TTL was accepted");
	EXPECT_CACHE_ERROR(jwks_default_ttl_ms, 3600001,
		"oversized JWKS fallback TTL was accepted");
	EXPECT_CACHE_ERROR(cache_max_ttl_ms, 299999,
		"maximum TTL shorter than a fallback was accepted");
	EXPECT_CACHE_ERROR(cache_max_ttl_ms, 86400001,
		"oversized cache maximum TTL was accepted");
	EXPECT_CACHE_ERROR(jwks_stale_grace_ms, -1,
		"negative JWKS stale grace was accepted");
	EXPECT_CACHE_ERROR(jwks_stale_grace_ms, 3600001,
		"oversized JWKS stale grace was accepted");
	EXPECT_CACHE_ERROR(unknown_kid_refresh_cooldown_ms, 999,
		"undersized unknown-kid cooldown was accepted");
	EXPECT_CACHE_ERROR(unknown_kid_refresh_cooldown_ms, 300001,
		"oversized unknown-kid cooldown was accepted");
#undef EXPECT_CACHE_ERROR

	config = valid_config();
	config.metadata_default_ttl_ms = 1000;
	config.jwks_default_ttl_ms = 1000;
	config.cache_max_ttl_ms = 1000;
	config.jwks_stale_grace_ms = 3600000;
	config.unknown_kid_refresh_cooldown_ms = 1000;
	if (pg_oauth_policy_build(&config, &hba, &policy) != PG_OAUTH_POLICY_OK)
		fail("valid lower and upper cache-policy boundaries were rejected");

	config = valid_config();
	hba = valid_hba();
	hba.issuer = "";
	expect_error(&config, &hba, PG_OAUTH_POLICY_MISSING_ISSUER,
				 "empty issuer was accepted");

	hba = valid_hba();
	hba.required_scopes = "";
	expect_error(&config, &hba, PG_OAUTH_POLICY_MISSING_SCOPES,
				 "empty required scopes were accepted");

	hba = valid_hba();
	hba.policy_name = "unknown";
	expect_error(&config, &hba, PG_OAUTH_POLICY_UNKNOWN_POLICY,
				 "unknown named policy was accepted");

	hba = valid_hba();
	hba.delegate_ident_mapping = true;
	expect_error(&config, &hba,
		PG_OAUTH_POLICY_DELEGATED_MAPPING_UNSUPPORTED,
		"delegated identity mapping was accepted");

	return EXIT_SUCCESS;
}
