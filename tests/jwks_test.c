#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jwks.h"

static const char alphabet[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static void
fail(const char *message)
{
	fprintf(stderr, "jwks_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static char *
encode_bytes(const unsigned char *input, size_t length)
{
	size_t		encoded_length = (length / 3) * 4 +
		(length % 3 == 0 ? 0 : length % 3 + 1);
	char	   *encoded = malloc(encoded_length + 1);
	size_t		in = 0;
	size_t		out = 0;

	if (encoded == NULL)
		fail("allocation failed");
	while (in + 3 <= length)
	{
		unsigned int bits = ((unsigned int) input[in] << 16) |
			((unsigned int) input[in + 1] << 8) | input[in + 2];

		encoded[out++] = alphabet[(bits >> 18) & 63];
		encoded[out++] = alphabet[(bits >> 12) & 63];
		encoded[out++] = alphabet[(bits >> 6) & 63];
		encoded[out++] = alphabet[bits & 63];
		in += 3;
	}
	if (length - in == 1)
	{
		unsigned int bits = (unsigned int) input[in] << 4;

		encoded[out++] = alphabet[(bits >> 6) & 63];
		encoded[out++] = alphabet[bits & 63];
	}
	else if (length - in == 2)
	{
		unsigned int bits = ((unsigned int) input[in] << 10) |
			((unsigned int) input[in + 1] << 2);

		encoded[out++] = alphabet[(bits >> 12) & 63];
		encoded[out++] = alphabet[(bits >> 6) & 63];
		encoded[out++] = alphabet[bits & 63];
	}
	encoded[out] = '\0';
	return encoded;
}

static PgOAuthJwksPolicy
valid_policy(void)
{
	PgOAuthJwksPolicy policy = {
		.max_jwks_size = 65536,
		.max_keys = 16,
		.max_key_id_size = 1024,
		.allowed_algorithms = PG_OAUTH_ALGORITHM_RS256 |
		PG_OAUTH_ALGORITHM_ES256,
		.minimum_rsa_bits = 2048,
		.maximum_rsa_bits = 8192,
	};

	return policy;
}

static char *
make_rsa_jwks(const char *members, size_t modulus_bytes, unsigned char first)
{
	unsigned char *modulus = malloc(modulus_bytes);
	char	   *encoded;
	char	   *document;
	size_t		capacity;

	if (modulus == NULL)
		fail("allocation failed");
	memset(modulus, 0xa5, modulus_bytes);
	modulus[0] = first;
	encoded = encode_bytes(modulus, modulus_bytes);
	free(modulus);
	capacity = strlen(encoded) + strlen(members) + 128;
	document = malloc(capacity);
	if (document == NULL)
		fail("allocation failed");
	snprintf(document, capacity,
			 "{\"keys\":[{\"kty\":\"RSA\",\"n\":\"%s\",\"e\":\"AQAB\",%s}]}",
			 encoded, members);
	free(encoded);
	return document;
}

static char *
make_ec_jwks(bool valid_point)
{
	static const unsigned char generator_x[32] = {
		0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
		0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
		0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
		0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96
	};
	static const unsigned char generator_y[32] = {
		0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
		0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
		0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
		0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5
	};
	unsigned char y[32];
	char	   *x_encoded = encode_bytes(generator_x, sizeof(generator_x));
	char	   *y_encoded;
	char	   *document;
	size_t		capacity;

	memcpy(y, generator_y, sizeof(y));
	if (!valid_point)
		y[31] ^= 1;
	y_encoded = encode_bytes(y, sizeof(y));
	capacity = strlen(x_encoded) + strlen(y_encoded) + 192;
	document = malloc(capacity);
	if (document == NULL)
		fail("allocation failed");
	snprintf(document, capacity,
			 "{\"keys\":[{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"%s\","
			 "\"y\":\"%s\",\"kid\":\"ec-1\",\"alg\":\"ES256\",\"use\":\"sig\"}]}",
			 x_encoded, y_encoded);
	free(x_encoded);
	free(y_encoded);
	return document;
}

static void
expect_error(const char *document, const char *key_id, uint32_t algorithm,
			 PgOAuthJwksError expected, const char *message)
{
	PgOAuthJwksPolicy policy = valid_policy();
	PgOAuthSelectedJwk selected;

	if (pg_oauth_jwks_select(document, strlen(document), key_id, algorithm,
							 &policy, &selected) != expected)
		fail(message);
	if (selected.jwks != NULL || selected.jwk != NULL)
		fail("rejected JWKS retained untrusted data");
}

int
main(void)
{
	PgOAuthJwksPolicy policy = valid_policy();
	PgOAuthSelectedJwk selected;
	char	   *document = make_rsa_jwks(
										 "\"kid\":\"rsa-1\",\"alg\":\"RS256\",\"use\":\"sig\"", 256, 0x80);

	if (pg_oauth_jwks_select(document, strlen(document), "rsa-1",
							 PG_OAUTH_ALGORITHM_RS256, &policy, &selected) != PG_OAUTH_JWKS_OK)
		fail("valid RSA signing key was rejected");
	if (selected.algorithm != PG_OAUTH_ALGORITHM_RS256 || selected.jwk == NULL)
		fail("valid key was selected incorrectly");
	pg_oauth_selected_jwk_clear(&selected);
	free(document);

	document = make_ec_jwks(true);
	if (pg_oauth_jwks_select(document, strlen(document), "ec-1",
							 PG_OAUTH_ALGORITHM_ES256, &policy, &selected) != PG_OAUTH_JWKS_OK)
		fail("valid P-256 signing key was rejected");
	pg_oauth_selected_jwk_clear(&selected);
	free(document);

	document = make_ec_jwks(false);
	expect_error(document, "ec-1", PG_OAUTH_ALGORITHM_ES256,
				 PG_OAUTH_JWKS_INVALID_KEY, "off-curve EC point was accepted");
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"RS256\",\"key_ops\":[\"verify\"]",
							 256, 0x80);
	if (pg_oauth_jwks_select(document, strlen(document), "rsa-1",
							 PG_OAUTH_ALGORITHM_RS256, &policy, &selected) != PG_OAUTH_JWKS_OK)
		fail("verify key operation was rejected");
	pg_oauth_selected_jwk_clear(&selected);
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"RS256\",\"use\":\"sig\"", 128, 0x80);
	expect_error(document, "rsa-1", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_INVALID_KEY, "undersized RSA modulus was accepted");
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"RS256\",\"use\":\"enc\"", 256, 0x80);
	expect_error(document, "rsa-1", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_KEY_NOT_FOR_SIGNATURE, "encryption key was accepted");
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"RS256\",\"key_ops\":[\"verify\",\"sign\"]",
							 256, 0x80);
	expect_error(document, "rsa-1", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_KEY_NOT_FOR_SIGNATURE,
				 "mixed public-key operations were accepted");
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"RS256\"", 256, 0x80);
	expect_error(document, "rsa-1", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_KEY_NOT_FOR_SIGNATURE,
				 "key without signature intent was accepted");
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"ES256\",\"use\":\"sig\"", 256, 0x80);
	expect_error(document, "rsa-1", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_ALGORITHM_MISMATCH, "key algorithm mismatch was accepted");
	free(document);

	document = make_rsa_jwks(
							 "\"kid\":\"rsa-1\",\"alg\":\"RS256\",\"use\":\"sig\",\"d\":\"AQ\"",
							 256, 0x80);
	expect_error(document, "rsa-1", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_INVALID_KEY, "private key material was accepted");
	free(document);

	expect_error("{\"keys\":[]}", "missing", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_INVALID_KEYS, "empty key set was accepted");
	expect_error("{\"keys\":[1]}", "missing", PG_OAUTH_ALGORITHM_RS256,
				 PG_OAUTH_JWKS_INVALID_KEYS, "non-object key was accepted");
	expect_error("{\"keys\":[],\"keys\":[]}", "missing",
				 PG_OAUTH_ALGORITHM_RS256, PG_OAUTH_JWKS_INVALID_JSON,
				 "duplicate JWKS member was accepted");
	expect_error("{\"keys\":[{\"kid\":\"k\",\"kid\":\"k\"}]}", "k",
				 PG_OAUTH_ALGORITHM_RS256, PG_OAUTH_JWKS_INVALID_JSON,
				 "duplicate JWK member was accepted");
	expect_error("{\"keys\":[{\"kid\":\"same\"},{\"kid\":\"same\"}]}", "same",
				 PG_OAUTH_ALGORITHM_RS256, PG_OAUTH_JWKS_DUPLICATE_KEY_ID,
				 "duplicate key identifier was accepted");
	expect_error("{\"keys\":[{\"kid\":\"other\"}]}", "missing",
				 PG_OAUTH_ALGORITHM_RS256, PG_OAUTH_JWKS_KEY_NOT_FOUND,
				 "unknown key identifier was accepted");
	expect_error("{\"keys\":[{\"kid\":\"line\\nfeed\"}]}", "missing",
				 PG_OAUTH_ALGORITHM_RS256, PG_OAUTH_JWKS_INVALID_KEY_ID,
				 "control character in key identifier was accepted");

	if (strstr(pg_oauth_jwks_error_code(PG_OAUTH_JWKS_INVALID_KEY), "rsa-1") !=
		NULL)
		fail("stable JWKS error exposed key data");
	return EXIT_SUCCESS;
}
