/*-------------------------------------------------------------------------
 *
 * pg_oauth_validator.c
 *    OAuth access-token validator for PostgreSQL 18 and 19.
 *
 * The callback begins denied and authorizes only after the complete
 * Milestone 1 validation pipeline succeeds.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <stdlib.h>
#include <time.h>

#include "fmgr.h"
#include "libpq/oauth.h"

#include "config.h"
#include "hba_policy.h"
#include "policy.h"
#include "shared_cache.h"
#include "validator.h"

PG_MODULE_MAGIC;

#if PG_VERSION_NUM < 180000 || PG_VERSION_NUM >= 200000
#error "pg_oauth_validator currently supports PostgreSQL 18 and 19 only"
#endif

#define PG_OAUTH_MAX_URL_SIZE 2048
#define PG_OAUTH_MAX_DOCUMENT_SIZE 65536
#define PG_OAUTH_MAX_IDENTITY_SIZE 1024
#define PG_OAUTH_HTTP_CONNECT_TIMEOUT_MS 2000
#define PG_OAUTH_HTTP_TOTAL_TIMEOUT_MS 5000

static char *pg_oauth_metadata_url(const char *issuer);
static bool pg_oauth_monotonic_ms(int64 *value);

static bool pg_oauth_validator_validate(const ValidatorModuleState *state,
										const char *token, const char *role,
										ValidatorModuleResult *result);
#if PG_VERSION_NUM >= 190000
static void pg_oauth_validator_startup(ValidatorModuleState *state);
#endif

static const OAuthValidatorCallbacks validator_callbacks = {
	.magic = PG_OAUTH_VALIDATOR_MAGIC,
#if PG_VERSION_NUM >= 190000
	.startup_cb = pg_oauth_validator_startup,
#else
	.startup_cb = NULL,
#endif
	.shutdown_cb = NULL,
	.validate_cb = pg_oauth_validator_validate,
};

#if PG_VERSION_NUM >= 190000
static void
pg_oauth_validator_startup(ValidatorModuleState *state)
{
	pg_oauth_hba_policy_register_options(state);
}
#endif

/*
 * Returning true tells PostgreSQL that the module made an authorization
 * decision; result->authorized distinguishes validation rejection from
 * success. Returning false reports an internal validator error. See
 * PostgreSQL 18/19, "OAuth Validator Callbacks", section 50.3.2.
 */
static bool
pg_oauth_validator_validate(const ValidatorModuleState *state,
							const char *token, const char *role,
							ValidatorModuleResult *result)
{
	PgOAuthPolicyConfig config;
	PgOAuthHbaPolicy hba_policy;
	PgOAuthValidationPolicy policy;
	PgOAuthValidatorPolicy validator_policy;
	PgOAuthIssuerKeyCachePolicy cache_policy;
	PgOAuthValidatorResult validation;
	PgOAuthCacheIo cache_io;
	char	   *metadata_url = NULL;
	char	   *metadata_host = NULL;
	size_t		token_length;
	int64		monotonic_ms;
	time_t		wall_time;
	PgOAuthValidatorError validation_error;

	(void) role;

	/* Initialize every output explicitly before any validation work. */
	result->authorized = false;
	result->authn_id = NULL;
#if PG_VERSION_NUM >= 190000
	result->error_detail = NULL;
#endif

	/*
	 * Acquire policy only from the matched, administrator-controlled HBA
	 * rule. Trust inputs come only from the matched HBA rule and
	 * administrator GUCs.
	 */
	if (!pg_oauth_hba_policy_read(state, &hba_policy))
		return false;
	pg_oauth_config_snapshot(&config);
	if (pg_oauth_policy_build(&config, &hba_policy, &policy) !=
		PG_OAUTH_POLICY_OK)
		return false;
	if (!pg_oauth_shared_cache_configure_capacity(
												  pg_oauth_config_cache_max_entries()) ||
		!pg_oauth_shared_cache_io(&cache_io) ||
		!pg_oauth_monotonic_ms(&monotonic_ms))
		return false;
	metadata_url = pg_oauth_metadata_url(policy.issuer);
	if (metadata_url == NULL ||
		pg_oauth_http_url_host(metadata_url, PG_OAUTH_MAX_URL_SIZE,
							   policy.allow_insecure_http, &metadata_host) != PG_OAUTH_HTTP_OK)
		goto internal_failure;

	memset(&validator_policy, 0, sizeof(validator_policy));
	validator_policy.envelope.max_token_size = (size_t) policy.max_token_size;
	validator_policy.envelope.max_header_size = 4096;
	validator_policy.envelope.max_payload_size = (size_t) policy.max_token_size;
	validator_policy.envelope.allowed_algorithms = policy.allowed_algorithms;
	validator_policy.envelope.required_token_type = policy.required_token_type;
	validator_policy.issuer_key.metadata_http = (PgOAuthHttpPolicy)
	{
		PG_OAUTH_MAX_URL_SIZE, PG_OAUTH_MAX_DOCUMENT_SIZE,
			PG_OAUTH_HTTP_CONNECT_TIMEOUT_MS, PG_OAUTH_HTTP_TOTAL_TIMEOUT_MS,
			metadata_host, NULL,
			policy.ca_file[0] != '\0' ? policy.ca_file : NULL,
			policy.allow_insecure_http
	};
	validator_policy.issuer_key.metadata = (PgOAuthMetadataPolicy)
	{
		PG_OAUTH_MAX_DOCUMENT_SIZE, PG_OAUTH_MAX_URL_SIZE,
			policy.allow_insecure_http
	};
	validator_policy.issuer_key.jwks_http = (PgOAuthHttpPolicy)
	{
		PG_OAUTH_MAX_URL_SIZE, PG_OAUTH_MAX_DOCUMENT_SIZE,
			PG_OAUTH_HTTP_CONNECT_TIMEOUT_MS, PG_OAUTH_HTTP_TOTAL_TIMEOUT_MS,
			metadata_host, policy.allowed_jwks_hosts,
			policy.ca_file[0] != '\0' ? policy.ca_file : NULL,
			policy.allow_insecure_http
	};
	validator_policy.issuer_key.jwks = (PgOAuthJwksPolicy)
	{
		PG_OAUTH_MAX_DOCUMENT_SIZE, 16, 1024, policy.allowed_algorithms,
			2048, 8192
	};
	wall_time = time(NULL);
	if (wall_time < 0)
		goto internal_failure;
	validator_policy.claims = (PgOAuthClaimsPolicy)
	{
		policy.issuer, policy.audiences, policy.required_scopes,
			policy.authn_claim, (int64) wall_time,
			(uint32) (policy.clock_skew_ms / 1000), PG_OAUTH_MAX_IDENTITY_SIZE,
			16, 64
	};
	validator_policy.identity = (PgOAuthIdentityPolicy)
	{
		PG_OAUTH_MAX_URL_SIZE, PG_OAUTH_MAX_IDENTITY_SIZE,
			PG_OAUTH_MAX_IDENTITY_SIZE
	};
	memset(&cache_policy, 0, sizeof(cache_policy));
	cache_policy.io = &cache_io;
	cache_policy.key_policy = (PgOAuthCacheKeyPolicy)
	{
		.policy_name = policy.name,
			.required_scopes = policy.required_scopes,
			.audiences = policy.audiences,
			.required_token_type = policy.required_token_type,
			.authn_claim = policy.authn_claim,
			.allowed_jwks_hosts = policy.allowed_jwks_hosts,
			.ca_file = policy.ca_file,
			.allowed_algorithms = policy.allowed_algorithms,
			.clock_skew_ms = (uint32) policy.clock_skew_ms,
			.max_token_size = (uint32) policy.max_token_size,
			.max_metadata_size = PG_OAUTH_MAX_DOCUMENT_SIZE,
			.max_jwks_size = PG_OAUTH_MAX_DOCUMENT_SIZE,
			.max_jwks_keys = 16,
			.minimum_rsa_bits = 2048,
			.maximum_rsa_bits = 8192,
			.jwks_stale_grace_ms = (uint64) policy.jwks_stale_grace_ms,
			.metadata_default_ttl_ms = (uint64) policy.metadata_default_ttl_ms,
			.jwks_default_ttl_ms = (uint64) policy.jwks_default_ttl_ms,
			.cache_max_ttl_ms = (uint64) policy.cache_max_ttl_ms,
			.unknown_kid_refresh_cooldown_ms =
			(uint64) policy.unknown_kid_refresh_cooldown_ms,
			.allow_insecure_http = policy.allow_insecure_http,
	};
	cache_policy.metadata_freshness = (PgOAuthHttpFreshnessPolicy)
	{
		policy.metadata_default_ttl_ms, policy.cache_max_ttl_ms
	};
	cache_policy.jwks_freshness = (PgOAuthHttpFreshnessPolicy)
	{
		policy.jwks_default_ttl_ms, policy.cache_max_ttl_ms
	};
	cache_policy.jwks_stale_grace_ms = policy.jwks_stale_grace_ms;
	cache_policy.unknown_kid_refresh_cooldown_ms =
		policy.unknown_kid_refresh_cooldown_ms;
	if (token == NULL)
		goto rejected;
	for (token_length = 0;
		 token_length <= (size_t) policy.max_token_size && token[token_length];
		 token_length++)
		;
	if (token_length == 0 || token_length > (size_t) policy.max_token_size)
		goto rejected;
	memset(&validation, 0, sizeof(validation));
	validation_error = pg_oauth_validate_access_token_cached(token,
															 token_length, metadata_url, &validator_policy, &cache_policy,
															 monotonic_ms, (int64) wall_time, &validation);
	if (validation_error != PG_OAUTH_VALIDATOR_OK)
	{
		ereport(DEBUG1,
				(errmsg_internal("OAuth access token rejected"),
				 errdetail_internal("validator=%s issuer_key=%s http=%s metadata=%s jwks=%s signature=%s claims=%s identity=%s",
									pg_oauth_validator_error_code(validation_error),
									pg_oauth_issuer_key_error_code(validation.issuer_key_error),
									pg_oauth_http_error_code(validation.http_error),
									pg_oauth_metadata_error_code(validation.metadata_error),
									pg_oauth_jwks_error_code(validation.jwks_error),
									pg_oauth_signature_error_code(validation.signature_error),
									pg_oauth_claims_error_code(validation.claims_error),
									pg_oauth_identity_error_code(validation.identity_error))));
		pg_oauth_validator_result_clear(&validation);
		goto rejected;
	}
	result->authn_id = pstrdup(validation.identity.value);
	result->authorized = true;
	pg_oauth_validator_result_clear(&validation);
	free(metadata_host);
	pfree(metadata_url);
	return true;

rejected:
#if PG_VERSION_NUM >= 190000
	result->error_detail = pstrdup("OAuth access token validation failed");
#endif
	free(metadata_host);
	if (metadata_url != NULL)
		pfree(metadata_url);
	return true;

internal_failure:
	free(metadata_host);
	if (metadata_url != NULL)
		pfree(metadata_url);
	return false;
}

static char *
pg_oauth_metadata_url(const char *issuer)
{
	size_t		length;
	const char *suffix;

	if (issuer == NULL)
		return NULL;
	length = strlen(issuer);
	if (length == 0 || length > PG_OAUTH_MAX_URL_SIZE)
		return NULL;
	suffix = issuer[length - 1] == '/' ?
		".well-known/openid-configuration" :
		"/.well-known/openid-configuration";
	if (strlen(suffix) > PG_OAUTH_MAX_URL_SIZE - length)
		return NULL;
	return psprintf("%s%s", issuer, suffix);
}

static bool
pg_oauth_monotonic_ms(int64 *value)
{
	struct timespec now;

	if (value == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
		now.tv_sec < 0 || now.tv_sec > INT64_MAX / 1000)
		return false;
	*value = (int64) now.tv_sec * 1000 + now.tv_nsec / 1000000;
	return true;
}

PGDLLEXPORT const OAuthValidatorCallbacks *
_PG_oauth_validator_module_init(void)
{
	return &validator_callbacks;
}
