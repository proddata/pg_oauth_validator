#ifndef PG_OAUTH_VALIDATOR_JWT_ENVELOPE_H
#define PG_OAUTH_VALIDATOR_JWT_ENVELOPE_H

#include <stddef.h>
#include <stdint.h>

#include <jansson.h>

#include "algorithms.h"

typedef enum PgOAuthJwtEnvelopeError
{
	PG_OAUTH_JWT_ENVELOPE_OK = 0,
	PG_OAUTH_JWT_ENVELOPE_INVALID_ARGUMENT,
	PG_OAUTH_JWT_ENVELOPE_EMPTY,
	PG_OAUTH_JWT_ENVELOPE_TOO_LARGE,
	PG_OAUTH_JWT_ENVELOPE_INVALID_COMPACT_JWS,
	PG_OAUTH_JWT_ENVELOPE_INVALID_BASE64URL,
	PG_OAUTH_JWT_ENVELOPE_HEADER_TOO_LARGE,
	PG_OAUTH_JWT_ENVELOPE_PAYLOAD_TOO_LARGE,
	PG_OAUTH_JWT_ENVELOPE_INVALID_HEADER_JSON,
	PG_OAUTH_JWT_ENVELOPE_INVALID_PAYLOAD_JSON,
	PG_OAUTH_JWT_ENVELOPE_UNSUPPORTED_HEADER,
	PG_OAUTH_JWT_ENVELOPE_INVALID_ALGORITHM,
	PG_OAUTH_JWT_ENVELOPE_INVALID_TOKEN_TYPE,
	PG_OAUTH_JWT_ENVELOPE_INVALID_KEY_ID
} PgOAuthJwtEnvelopeError;

typedef struct PgOAuthJwtEnvelopePolicy
{
	size_t		max_token_size;
	size_t		max_header_size;
	size_t		max_payload_size;
	uint32_t	allowed_algorithms;
	const char *required_token_type;
} PgOAuthJwtEnvelopePolicy;

/*
 * Header and payload are syntactically validated but remain untrusted until
 * signature verification succeeds.  Call pg_oauth_jwt_envelope_clear() for
 * every successfully initialized result.
 */
typedef struct PgOAuthJwtEnvelope
{
	json_t	   *untrusted_header;
	json_t	   *untrusted_payload;
	uint32_t	algorithm;
	const char *key_id;
} PgOAuthJwtEnvelope;

extern PgOAuthJwtEnvelopeError pg_oauth_jwt_envelope_parse(
	const char *token, size_t token_length,
	const PgOAuthJwtEnvelopePolicy *policy, PgOAuthJwtEnvelope *envelope);
extern void pg_oauth_jwt_envelope_clear(PgOAuthJwtEnvelope *envelope);
extern const char *pg_oauth_jwt_envelope_error_code(
	PgOAuthJwtEnvelopeError error);

#endif /* PG_OAUTH_VALIDATOR_JWT_ENVELOPE_H */
