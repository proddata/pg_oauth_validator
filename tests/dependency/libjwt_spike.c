#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jwt.h>

typedef struct ParseObservation
{
	bool callback_called;
} ParseObservation;

static int failures;

static void
expect_true(bool condition, const char *description)
{
	if (!condition)
	{
		fprintf(stderr, "FAIL: %s\n", description);
		failures++;
	}
}

static int
observe_parse(jwt_t *token, jwt_config_t *config)
{
	ParseObservation *observation = config->ctx;

	(void) token;
	observation->callback_called = true;
	return 1;
}

static bool
parser_reaches_callback(const char *token)
{
	jwt_checker_t *checker;
	ParseObservation observation = {false};

	checker = jwt_checker_new();
	if (checker == NULL)
		return false;

	if (jwt_checker_setcb(checker, observe_parse, &observation) == 0)
		(void) jwt_checker_verify(checker, token);

	if (checker != NULL)
		jwt_checker_free(checker);
	return observation.callback_called;
}

static bool
verifies_without_key(const char *token)
{
	jwt_checker_t *checker;
	bool verified = false;

	checker = jwt_checker_new();
	if (checker == NULL)
		return false;

	verified = jwt_checker_verify(checker, token) == 0;
	jwt_checker_free(checker);
	return verified;
}

static bool
verifies_with_jwk(const char *token, const char *jwk, jwt_alg_t algorithm)
{
	jwk_set_t *set;
	jwt_checker_t *checker;
	const jwk_item_t *item;
	bool verified = false;

	set = jwks_create(jwk);
	if (set == NULL)
		return false;
	item = jwks_item_get(set, 0);
	checker = jwt_checker_new();
	if (item != NULL && checker != NULL &&
		jwt_checker_setkey(checker, algorithm, item) == 0)
		verified = jwt_checker_verify(checker, token) == 0;

	if (checker != NULL)
		jwt_checker_free(checker);
	jwks_free(set);
	return verified;
}

static void
check_parser_behavior(void)
{
	static const char duplicate_alg[] =
		"eyJhbGciOiJub25lIiwiYWxnIjoiUlMyNTYiLCJ0eXAiOiJhdCtqd3QifQ."
		"e30.signature";
	static const char duplicate_claim[] =
		"eyJhbGciOiJSUzI1NiIsInR5cCI6ImF0K2p3dCJ9."
		"eyJpc3MiOiJhIiwiaXNzIjoiYiJ9.signature";
	static const char malformed_base64[] = "%.e30.signature";
	static const char padded_header[] =
		"eyJhbGciOiJub25lIn0=.e30.";
	static const char four_segments[] =
		"eyJhbGciOiJSUzI1NiIsInR5cCI6ImF0K2p3dCJ9.e30.signature.extra";

	/* These observations document where a strict wrapper is required. */
	expect_true(parser_reaches_callback(duplicate_alg),
		"libjwt 3.3.3 accepts duplicate protected-header members");
	expect_true(parser_reaches_callback(duplicate_claim),
		"libjwt 3.3.3 accepts duplicate claim members");
	expect_true(!parser_reaches_callback(malformed_base64),
		"libjwt rejects an invalid Base64URL alphabet in the header");
	expect_true(parser_reaches_callback(padded_header),
		"libjwt accepts padded Base64URL and requires canonical pre-validation");
	expect_true(parser_reaches_callback(four_segments),
		"libjwt parser reaches the callback for a fourth compact segment");
}

static void
check_policy_behavior(void)
{
	static const char hmac_jwk[] =
		"{\"kty\":\"oct\","
		"\"k\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
		"\"kid\":\"hmac-test\",\"alg\":\"HS256\"}";
	static const char hmac_token[] =
		"eyJhbGciOiJIUzI1NiIsInR5cCI6ImF0K2p3dCJ9.e30."
		"jFqYS5aSgrAADJ_mCKmu5EiANaT5L18bJDRwS6W31oc";
	static const char none_token[] = "eyJhbGciOiJub25lIn0.e30.";
	static const char wrong_type[] =
		"eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0.e30.";
	static const char token_jku[] =
		"eyJhbGciOiJub25lIiwidHlwIjoiYXQrand0Iiwiamt1IjoiaHR0cHM6Ly9ldmls"
		"LmV4YW1wbGUvandrcyJ9.e30.";
	static const char unknown_crit[] =
		"eyJhbGciOiJub25lIiwidHlwIjoiYXQrand0IiwiY3JpdCI6WyJ1bmtub3duIl0s"
		"InVua25vd24iOnRydWV9.e30.";

	expect_true(verifies_without_key(none_token),
		"libjwt supports unsecured tokens unless local policy rejects them");
	expect_true(verifies_without_key(wrong_type),
		"libjwt does not enforce the OAuth access-token type by default");
	expect_true(verifies_without_key(token_jku),
		"libjwt does not reject a token-provided jku header by default");
	expect_true(verifies_without_key(unknown_crit),
		"libjwt does not reject an unsupported crit header by default");
	expect_true(verifies_with_jwk(hmac_token, hmac_jwk, JWT_ALG_HS256),
		"libjwt supports valid HMAC tokens unless local policy rejects them");
}

static void
check_jwks_behavior(void)
{
	static const char duplicate_kid[] =
		"{\"kty\":\"oct\","
		"\"k\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
		"\"kid\":\"first\",\"kid\":\"second\",\"alg\":\"HS256\"}";
	jwk_set_t *set;
	const jwk_item_t *item;

	set = jwks_create(duplicate_kid);
	expect_true(set != NULL, "libjwt returns a set for duplicate JWK members");
	if (set == NULL)
		return;

	expect_true(jwks_item_count(set) == 1,
		"duplicate-member JWK produces one parsed key");
	item = jwks_item_get(set, 0);
	expect_true(item != NULL, "duplicate-member JWK remains accessible");
	if (item != NULL)
		expect_true(strcmp(jwks_item_kid(item), "second") == 0,
			"Jansson/libjwt retains the last duplicate JWK member");

	jwks_free(set);
}

int
main(void)
{
	check_parser_behavior();
	check_policy_behavior();
	check_jwks_behavior();

	if (failures != 0)
	{
		fprintf(stderr, "%d libjwt spike assertion(s) failed\n", failures);
		return EXIT_FAILURE;
	}

	puts("libjwt spike observations match the reviewed 3.3.3 behavior");
	return EXIT_SUCCESS;
}
