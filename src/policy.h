#ifndef PG_OAUTH_VALIDATOR_POLICY_H
#define PG_OAUTH_VALIDATOR_POLICY_H

#include "postgres.h"

#include "algorithms.h"
#include "hba_policy.h"

typedef struct PgOAuthPolicyConfig
{
	const char *audiences;
	const char *allowed_algorithms;
	const char *required_token_type;
	const char *authn_claim;
	const char *allowed_jwks_hosts;
	const char *ca_file;
	int			clock_skew_ms;
	int			max_token_size;
	int			metadata_default_ttl_ms;
	int			jwks_default_ttl_ms;
	int			cache_max_ttl_ms;
	int			jwks_stale_grace_ms;
	int			unknown_kid_refresh_cooldown_ms;
	bool		allow_insecure_http;
} PgOAuthPolicyConfig;

typedef struct PgOAuthValidationPolicy
{
	const char *name;
	const char *issuer;
	const char *required_scopes;
	const char *audiences;
	const char *required_token_type;
	const char *authn_claim;
	const char *allowed_jwks_hosts;
	const char *ca_file;
	uint32		allowed_algorithms;
	int			clock_skew_ms;
	int			max_token_size;
	int			metadata_default_ttl_ms;
	int			jwks_default_ttl_ms;
	int			cache_max_ttl_ms;
	int			jwks_stale_grace_ms;
	int			unknown_kid_refresh_cooldown_ms;
	bool		allow_insecure_http;
} PgOAuthValidationPolicy;

typedef enum PgOAuthPolicyError
{
	PG_OAUTH_POLICY_OK = 0,
	PG_OAUTH_POLICY_INVALID_ARGUMENT,
	PG_OAUTH_POLICY_MISSING_ISSUER,
	PG_OAUTH_POLICY_MISSING_SCOPES,
	PG_OAUTH_POLICY_MISSING_AUDIENCE,
	PG_OAUTH_POLICY_INVALID_AUDIENCES,
	PG_OAUTH_POLICY_INVALID_ALGORITHMS,
	PG_OAUTH_POLICY_INVALID_TOKEN_TYPE,
	PG_OAUTH_POLICY_INVALID_AUTHN_CLAIM,
	PG_OAUTH_POLICY_INVALID_JWKS_HOSTS,
	PG_OAUTH_POLICY_INVALID_CA_FILE,
	PG_OAUTH_POLICY_INVALID_CLOCK_SKEW,
	PG_OAUTH_POLICY_INVALID_TOKEN_SIZE,
	PG_OAUTH_POLICY_INVALID_CACHE_POLICY,
	PG_OAUTH_POLICY_UNKNOWN_POLICY,
	PG_OAUTH_POLICY_DELEGATED_MAPPING_UNSUPPORTED
} PgOAuthPolicyError;

extern PgOAuthPolicyError pg_oauth_policy_build(
												const PgOAuthPolicyConfig *config, const PgOAuthHbaPolicy *hba,
												PgOAuthValidationPolicy *policy);

#endif							/* PG_OAUTH_VALIDATOR_POLICY_H */
