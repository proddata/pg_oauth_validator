#include "identity.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
pg_oauth_identity_build(const char *subject, size_t subject_length,
						const PgOAuthIdentityPolicy *policy,
						PgOAuthIdentity *identity)
{
	char	   *output;

	if (identity == NULL)
		return PG_OAUTH_IDENTITY_INVALID_ARGUMENT;
	memset(identity, 0, sizeof(*identity));
	if (subject == NULL || policy == NULL || policy->max_subject_size == 0 ||
		policy->max_authn_id_size == 0)
		return PG_OAUTH_IDENTITY_INVALID_ARGUMENT;
	if (subject_length == 0 || subject_length > policy->max_subject_size ||
		subject_length > policy->max_authn_id_size ||
		memchr(subject, '\0', subject_length) != NULL ||
		!valid_utf8(subject, subject_length))
		return PG_OAUTH_IDENTITY_INVALID_SUBJECT;
	if (subject_length == SIZE_MAX)
		return PG_OAUTH_IDENTITY_TOO_LARGE;
	output = malloc(subject_length + 1);
	if (output == NULL)
		return PG_OAUTH_IDENTITY_ALLOCATION_FAILED;
	memcpy(output, subject, subject_length);
	output[subject_length] = '\0';
	identity->value = output;
	identity->length = subject_length;
	return PG_OAUTH_IDENTITY_OK;
}

const char *
pg_oauth_identity_error_code(PgOAuthIdentityError error)
{
	static const char *const codes[] = {
		"ok", "invalid_argument", "invalid_subject",
		"too_large", "allocation_failed"
	};

	if ((size_t) error >= sizeof(codes) / sizeof(codes[0]))
		return "unknown";
	return codes[error];
}
