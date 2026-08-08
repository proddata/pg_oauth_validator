#include "claims.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static bool
json_string_equals(const json_t *value, const char *expected)
{
	size_t		length;

	if (!json_is_string(value) || expected == NULL)
		return false;
	length = strlen(expected);
	return json_string_length(value) == length &&
		memcmp(json_string_value(value), expected, length) == 0;
}

static bool
valid_policy(const PgOAuthClaimsPolicy *policy)
{
	return policy != NULL && policy->issuer != NULL &&
		policy->issuer[0] != '\0' && policy->audiences != NULL &&
		policy->audiences[0] != '\0' && policy->required_scopes != NULL &&
		policy->required_scopes[0] != '\0' && policy->identity_claim != NULL &&
		policy->identity_claim[0] != '\0' && policy->current_time >= 0 &&
		policy->clock_skew_seconds <= 300 && policy->max_identity_size > 0 &&
		policy->max_audiences > 0 && policy->max_audiences <= 64 &&
		policy->max_scopes > 0 && policy->max_scopes <= 256;
}

static bool
configured_audience_matches(const char *configured, const char *token,
							size_t token_length, size_t maximum)
{
	const char *entry = configured;
	size_t		count = 0;

	while (*entry != '\0')
	{
		const char *end = strchr(entry, ',');
		const char *trimmed_end = end != NULL ? end : entry + strlen(entry);

		while (*entry != '\0' && isspace((unsigned char) *entry))
			entry++;
		while (trimmed_end > entry &&
			   isspace((unsigned char) trimmed_end[-1]))
			trimmed_end--;
		if (trimmed_end == entry || ++count > maximum)
			return false;
		if ((size_t) (trimmed_end - entry) == token_length &&
			memcmp(entry, token, token_length) == 0)
			return true;
		if (end == NULL)
			break;
		entry = end + 1;
	}
	return false;
}

static bool
audience_matches(const json_t *audience, const PgOAuthClaimsPolicy *policy)
{
	if (json_is_string(audience))
	{
		return json_string_length(audience) > 0 &&
			configured_audience_matches(policy->audiences,
										json_string_value(audience), json_string_length(audience),
										policy->max_audiences);
	}
	if (!json_is_array(audience) || json_array_size(audience) == 0 ||
		json_array_size(audience) > policy->max_audiences)
		return false;
	for (size_t i = 0; i < json_array_size(audience); i++)
	{
		json_t	   *entry = json_array_get(audience, i);

		if (!json_is_string(entry) || json_string_length(entry) == 0)
			return false;
		for (size_t j = 0; j < i; j++)
		{
			json_t	   *previous = json_array_get(audience, j);

			if (json_string_length(previous) == json_string_length(entry) &&
				memcmp(json_string_value(previous), json_string_value(entry),
					   json_string_length(entry)) == 0)
				return false;
		}
	}
	for (size_t i = 0; i < json_array_size(audience); i++)
	{
		json_t	   *entry = json_array_get(audience, i);

		if (configured_audience_matches(policy->audiences,
										json_string_value(entry), json_string_length(entry),
										policy->max_audiences))
			return true;
	}
	return false;
}

static bool
valid_scope_token(const char *scope, size_t length)
{
	if (length == 0)
		return false;
	for (size_t i = 0; i < length; i++)
	{
		unsigned char byte = (unsigned char) scope[i];

		/* RFC 6749 scope-token: %x21 / %x23-5B / %x5D-7E. */
		if (!(byte == 0x21 || (byte >= 0x23 && byte <= 0x5b) ||
			  (byte >= 0x5d && byte <= 0x7e)))
			return false;
	}
	return true;
}

static bool
scope_string_contains(const char *scopes, size_t scopes_length,
					  const char *required, size_t required_length,
					  size_t maximum, bool *valid)
{
	size_t		starts[256];
	size_t		lengths[256];
	size_t		start = 0;
	size_t		count = 0;
	bool		found = false;

	*valid = false;
	if (scopes_length == 0)
		return false;
	for (size_t i = 0; i <= scopes_length; i++)
	{
		if (i != scopes_length && scopes[i] != ' ')
			continue;
		if (i == start || count >= maximum ||
			!valid_scope_token(scopes + start, i - start))
			return false;
		for (size_t j = 0; j < count; j++)
		{
			if (lengths[j] == i - start &&
				memcmp(scopes + starts[j], scopes + start, i - start) == 0)
				return false;
		}
		starts[count] = start;
		lengths[count] = i - start;
		count++;
		if (i - start == required_length &&
			memcmp(scopes + start, required, required_length) == 0)
			found = true;
		start = i + 1;
	}
	*valid = true;
	return found;
}

static bool
scope_array_contains(const json_t *scopes, const char *required,
					 size_t required_length, size_t maximum, bool *valid)
{
	bool		found = false;

	*valid = false;
	if (!json_is_array(scopes) || json_array_size(scopes) == 0 ||
		json_array_size(scopes) > maximum)
		return false;
	for (size_t i = 0; i < json_array_size(scopes); i++)
	{
		json_t	   *entry = json_array_get(scopes, i);

		if (!json_is_string(entry) ||
			!valid_scope_token(json_string_value(entry),
							   json_string_length(entry)))
			return false;
		for (size_t j = 0; j < i; j++)
		{
			json_t	   *previous = json_array_get(scopes, j);

			if (json_string_length(previous) == json_string_length(entry) &&
				memcmp(json_string_value(previous), json_string_value(entry),
					   json_string_length(entry)) == 0)
				return false;
		}
		if (json_string_length(entry) == required_length &&
			memcmp(json_string_value(entry), required, required_length) == 0)
			found = true;
	}
	*valid = true;
	return found;
}

static PgOAuthClaimsError
validate_scopes(const json_t *payload, const PgOAuthClaimsPolicy *policy)
{
	json_t	   *token_scopes = json_object_get(payload, "scope");
	const char *required = policy->required_scopes;
	size_t		start = 0;
	size_t		required_length = strlen(required);
	size_t		count = 0;

	if (!json_is_string(token_scopes) && !json_is_array(token_scopes))
		return PG_OAUTH_CLAIMS_INVALID_SCOPE;
	for (size_t i = 0; i <= required_length; i++)
	{
		bool		valid;
		bool		found;

		if (i != required_length && required[i] != ' ')
			continue;
		if (i == start || ++count > policy->max_scopes ||
			!valid_scope_token(required + start, i - start))
			return PG_OAUTH_CLAIMS_INVALID_ARGUMENT;
		if (json_is_string(token_scopes))
			found = scope_string_contains(json_string_value(token_scopes),
										  json_string_length(token_scopes), required + start, i - start,
										  policy->max_scopes, &valid);
		else
			found = scope_array_contains(token_scopes, required + start,
										 i - start, policy->max_scopes, &valid);
		if (!valid)
			return PG_OAUTH_CLAIMS_INVALID_SCOPE;
		if (!found)
			return PG_OAUTH_CLAIMS_INSUFFICIENT_SCOPE;
		start = i + 1;
	}
	return PG_OAUTH_CLAIMS_OK;
}

PgOAuthClaimsError
pg_oauth_claims_validate(const json_t *verified_payload,
						 const PgOAuthClaimsPolicy *policy,
						 PgOAuthValidatedClaims *claims)
{
	json_t	   *value;
	json_int_t	numeric_date;
	PgOAuthClaimsError scope_error;

	if (claims == NULL)
		return PG_OAUTH_CLAIMS_INVALID_ARGUMENT;
	memset(claims, 0, sizeof(*claims));
	if (!json_is_object(verified_payload) || !valid_policy(policy))
		return PG_OAUTH_CLAIMS_INVALID_ARGUMENT;

	value = json_object_get(verified_payload, "iss");
	if (!json_string_equals(value, policy->issuer))
		return PG_OAUTH_CLAIMS_INVALID_ISSUER;
	value = json_object_get(verified_payload, "aud");
	if (!audience_matches(value, policy))
		return PG_OAUTH_CLAIMS_INVALID_AUDIENCE;

	value = json_object_get(verified_payload, "exp");
	if (value == NULL)
		return PG_OAUTH_CLAIMS_MISSING_EXPIRY;
	if (!json_is_integer(value) || (numeric_date = json_integer_value(value)) <= 0)
		return PG_OAUTH_CLAIMS_INVALID_EXPIRY;
	if (policy->current_time >= numeric_date &&
		(uint64_t) (policy->current_time - numeric_date) >=
		policy->clock_skew_seconds)
		return PG_OAUTH_CLAIMS_EXPIRED;
	claims->expires_at = numeric_date;

	value = json_object_get(verified_payload, "nbf");
	if (value != NULL)
	{
		if (!json_is_integer(value) ||
			(numeric_date = json_integer_value(value)) < 0)
			return PG_OAUTH_CLAIMS_INVALID_NOT_BEFORE;
		if (numeric_date > policy->current_time &&
			(uint64_t) (numeric_date - policy->current_time) >
			policy->clock_skew_seconds)
			return PG_OAUTH_CLAIMS_NOT_YET_VALID;
	}
	value = json_object_get(verified_payload, "iat");
	if (value != NULL)
	{
		if (!json_is_integer(value) ||
			(numeric_date = json_integer_value(value)) < 0)
			return PG_OAUTH_CLAIMS_INVALID_ISSUED_AT;
		if (numeric_date > policy->current_time &&
			(uint64_t) (numeric_date - policy->current_time) >
			policy->clock_skew_seconds)
			return PG_OAUTH_CLAIMS_ISSUED_IN_FUTURE;
	}

	value = json_object_get(verified_payload, policy->identity_claim);
	if (!json_is_string(value) || json_string_length(value) == 0 ||
		json_string_length(value) > policy->max_identity_size)
		return PG_OAUTH_CLAIMS_INVALID_IDENTITY;
	for (size_t i = 0; i < json_string_length(value); i++)
	{
		if (iscntrl((unsigned char) json_string_value(value)[i]))
			return PG_OAUTH_CLAIMS_INVALID_IDENTITY;
	}

	scope_error = validate_scopes(verified_payload, policy);
	if (scope_error != PG_OAUTH_CLAIMS_OK)
		return scope_error;
	claims->identity = json_string_value(value);
	claims->identity_length = json_string_length(value);
	return PG_OAUTH_CLAIMS_OK;
}

const char *
pg_oauth_claims_error_code(PgOAuthClaimsError error)
{
	static const char *const codes[] = {
		"ok", "invalid_argument", "invalid_issuer", "invalid_audience",
		"missing_expiry", "invalid_expiry", "expired", "invalid_not_before",
		"not_yet_valid", "invalid_issued_at", "issued_in_future",
		"invalid_identity", "invalid_scope", "insufficient_scope"
	};

	if ((size_t) error >= sizeof(codes) / sizeof(codes[0]))
		return "unknown";
	return codes[error];
}
