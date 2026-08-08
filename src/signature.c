#include "signature.h"

#include <stdlib.h>
#include <string.h>

#include <jwt.h>

static jwt_alg_t
libjwt_algorithm(uint32_t algorithm)
{
	if (algorithm == PG_OAUTH_ALGORITHM_RS256)
		return JWT_ALG_RS256;
	if (algorithm == PG_OAUTH_ALGORITHM_ES256)
		return JWT_ALG_ES256;
	return JWT_ALG_INVAL;
}

PgOAuthSignatureError
pg_oauth_signature_verify(const char *token, size_t token_length,
						  const PgOAuthSelectedJwk *selected_key)
{
	char	   *jwk_json = NULL;
	char	   *token_copy = NULL;
	jwk_set_t  *key_set = NULL;
	const jwk_item_t *key;
	jwt_checker_t *checker = NULL;
	jwt_alg_t	algorithm;
	PgOAuthSignatureError result = PG_OAUTH_SIGNATURE_VERIFIER_FAILED;

	if (token == NULL || token_length == 0 || selected_key == NULL ||
		selected_key->jwks == NULL || selected_key->jwk == NULL)
		return PG_OAUTH_SIGNATURE_INVALID_ARGUMENT;
	algorithm = libjwt_algorithm(selected_key->algorithm);
	if (algorithm == JWT_ALG_INVAL)
		return PG_OAUTH_SIGNATURE_INVALID_ARGUMENT;
	if (memchr(token, '\0', token_length) != NULL || token_length == SIZE_MAX)
		return PG_OAUTH_SIGNATURE_INVALID_ARGUMENT;

	/* Serialize only the key already selected by the strict JWKS boundary. */
	jwk_json = json_dumps(selected_key->jwk, JSON_COMPACT | JSON_SORT_KEYS);
	if (jwk_json == NULL)
		return PG_OAUTH_SIGNATURE_KEY_SERIALIZATION_FAILED;
	key_set = jwks_create_strn(jwk_json, strlen(jwk_json));
	if (key_set == NULL || jwks_error(key_set) != 0 ||
		jwks_item_count(key_set) != 1)
	{
		result = PG_OAUTH_SIGNATURE_KEY_IMPORT_FAILED;
		goto done;
	}
	key = jwks_item_get(key_set, 0);
	checker = jwt_checker_new();
	if (key == NULL || checker == NULL ||
		jwt_checker_setkey(checker, algorithm, key) != 0 ||
		jwt_checker_time_leeway(checker, JWT_CLAIM_EXP, -1) != 0 ||
		jwt_checker_time_leeway(checker, JWT_CLAIM_NBF, -1) != 0)
	{
		result = PG_OAUTH_SIGNATURE_VERIFIER_FAILED;
		goto done;
	}

	token_copy = malloc(token_length + 1);
	if (token_copy == NULL)
		goto done;
	memcpy(token_copy, token, token_length);
	token_copy[token_length] = '\0';
	result = jwt_checker_verify(checker, token_copy) == 0 ?
		PG_OAUTH_SIGNATURE_OK : PG_OAUTH_SIGNATURE_INVALID;

done:
	free(token_copy);
	if (checker != NULL)
		jwt_checker_free(checker);
	if (key_set != NULL)
		jwks_free(key_set);
	free(jwk_json);
	return result;
}

const char *
pg_oauth_signature_error_code(PgOAuthSignatureError error)
{
	static const char *const codes[] = {
		"ok", "invalid_argument", "key_serialization_failed",
		"key_import_failed", "verifier_failed", "invalid_signature"
	};

	if ((size_t) error >= sizeof(codes) / sizeof(codes[0]))
		return "unknown";
	return codes[error];
}
