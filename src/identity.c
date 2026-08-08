#include "identity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "base64url.h"

#define PG_OAUTH_AUTHN_ID_PREFIX "v1."

static bool
valid_utf8(const char *value, size_t length)
{
	size_t		index = 0;

	while (index < length)
	{
		uint8_t		first = (uint8_t) value[index];
		size_t		continuations;
		uint32_t	codepoint;

		if (first <= 0x7f)
		{
			if (first < 0x20 || first == 0x7f)
				return false;
			index++;
			continue;
		}
		if (first >= 0xc2 && first <= 0xdf)
		{
			continuations = 1;
			codepoint = first & 0x1f;
		}
		else if (first >= 0xe0 && first <= 0xef)
		{
			continuations = 2;
			codepoint = first & 0x0f;
		}
		else if (first >= 0xf0 && first <= 0xf4)
		{
			continuations = 3;
			codepoint = first & 0x07;
		}
		else
			return false;
		if (continuations > length - index - 1)
			return false;
		for (size_t i = 1; i <= continuations; i++)
		{
			uint8_t		byte = (uint8_t) value[index + i];

			if ((byte & 0xc0) != 0x80)
				return false;
			codepoint = (codepoint << 6) | (byte & 0x3f);
		}
		if ((continuations == 2 && codepoint < 0x800) ||
			(continuations == 3 && codepoint < 0x10000) ||
			(codepoint >= 0x80 && codepoint <= 0x9f) ||
			(codepoint >= 0xd800 && codepoint <= 0xdfff) ||
			codepoint > 0x10ffff)
			return false;
		index += continuations + 1;
	}
	return true;
}

void
pg_oauth_identity_clear(PgOAuthIdentity *identity)
{
	if (identity == NULL)
		return;
	free(identity->value);
	memset(identity, 0, sizeof(*identity));
}

PgOAuthIdentityError
pg_oauth_identity_build(const char *issuer, size_t issuer_length,
						const char *subject, size_t subject_length,
						const PgOAuthIdentityPolicy *policy,
						PgOAuthIdentity *identity)
{
	size_t		issuer_encoded_length;
	size_t		subject_encoded_length;
	size_t		length;
	char	   *output;

	if (identity == NULL)
		return PG_OAUTH_IDENTITY_INVALID_ARGUMENT;
	memset(identity, 0, sizeof(*identity));
	if (issuer == NULL || subject == NULL || policy == NULL ||
		policy->max_issuer_size == 0 || policy->max_subject_size == 0 ||
		policy->max_authn_id_size < sizeof(PG_OAUTH_AUTHN_ID_PREFIX) + 1)
		return PG_OAUTH_IDENTITY_INVALID_ARGUMENT;
	if (issuer_length == 0 || issuer_length > policy->max_issuer_size ||
		memchr(issuer, '\0', issuer_length) != NULL ||
		!valid_utf8(issuer, issuer_length))
		return PG_OAUTH_IDENTITY_INVALID_ISSUER;
	if (subject_length == 0 || subject_length > policy->max_subject_size ||
		memchr(subject, '\0', subject_length) != NULL ||
		!valid_utf8(subject, subject_length))
		return PG_OAUTH_IDENTITY_INVALID_SUBJECT;
	if (!pg_oauth_base64url_encoded_size(issuer_length,
										 policy->max_authn_id_size, &issuer_encoded_length) ||
		!pg_oauth_base64url_encoded_size(subject_length,
										 policy->max_authn_id_size, &subject_encoded_length) ||
		subject_encoded_length > SIZE_MAX - 4 ||
		issuer_encoded_length > SIZE_MAX - subject_encoded_length - 4)
		return PG_OAUTH_IDENTITY_TOO_LARGE;
	length = issuer_encoded_length + subject_encoded_length + 4;
	if (length > policy->max_authn_id_size || length == SIZE_MAX)
		return PG_OAUTH_IDENTITY_TOO_LARGE;
	output = malloc(length + 1);
	if (output == NULL)
		return PG_OAUTH_IDENTITY_ALLOCATION_FAILED;
	memcpy(output, PG_OAUTH_AUTHN_ID_PREFIX, 3);
	if (!pg_oauth_base64url_encode((const uint8_t *) issuer, issuer_length,
								   output + 3, issuer_encoded_length))
	{
		free(output);
		return PG_OAUTH_IDENTITY_INVALID_ISSUER;
	}
	output[3 + issuer_encoded_length] = '.';
	if (!pg_oauth_base64url_encode((const uint8_t *) subject, subject_length,
								   output + 4 + issuer_encoded_length,
								   subject_encoded_length))
	{
		free(output);
		return PG_OAUTH_IDENTITY_INVALID_SUBJECT;
	}
	output[length] = '\0';
	identity->value = output;
	identity->length = length;
	return PG_OAUTH_IDENTITY_OK;
}

const char *
pg_oauth_identity_error_code(PgOAuthIdentityError error)
{
	static const char *const codes[] = {
		"ok", "invalid_argument", "invalid_issuer", "invalid_subject",
		"too_large", "allocation_failed"
	};

	if ((size_t) error >= sizeof(codes) / sizeof(codes[0]))
		return "unknown";
	return codes[error];
}
