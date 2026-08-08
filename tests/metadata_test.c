#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metadata.h"

static void
fail(const char *message)
{
	fprintf(stderr, "metadata_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthMetadataPolicy
valid_policy(void)
{
	PgOAuthMetadataPolicy policy = {
		.max_document_size = 65536,
		.max_url_size = 2048,
		.allow_insecure_http = false,
	};

	return policy;
}

static void
expect_error_with_policy(const char *document, const char *issuer,
						 PgOAuthMetadataPolicy *policy,
						 PgOAuthMetadataError expected, const char *message)
{
	PgOAuthMetadata metadata;

	if (pg_oauth_metadata_parse(document, strlen(document), issuer, policy,
								&metadata) != expected)
		fail(message);
	if (metadata.jwks_uri != NULL)
		fail("rejected metadata retained an untrusted URI");
}

static void
expect_error(const char *document, PgOAuthMetadataError expected,
			 const char *message)
{
	PgOAuthMetadataPolicy policy = valid_policy();

	expect_error_with_policy(document, "https://issuer.example", &policy,
							 expected, message);
}

int
main(void)
{
	PgOAuthMetadataPolicy policy = valid_policy();
	PgOAuthMetadata metadata;
	const char *valid =
		"{\"issuer\":\"https://issuer.example\","
		"\"jwks_uri\":\"https://keys.example/oauth/jwks?version=1\"}";

	if (pg_oauth_metadata_parse(valid, strlen(valid),
								"https://issuer.example", &policy, &metadata) != PG_OAUTH_METADATA_OK)
		fail("valid discovery metadata was rejected");
	if (strcmp(metadata.jwks_uri,
			   "https://keys.example/oauth/jwks?version=1") != 0)
		fail("JWKS URI was not preserved exactly");
	pg_oauth_metadata_clear(&metadata);

	expect_error("{}", PG_OAUTH_METADATA_INVALID_ISSUER,
				 "missing issuer was accepted");
	expect_error("{\"issuer\":1,\"jwks_uri\":\"https://keys.example\"}",
				 PG_OAUTH_METADATA_INVALID_ISSUER, "non-string issuer was accepted");
	expect_error("{\"issuer\":\"https://issuer.example/\","
				 "\"jwks_uri\":\"https://keys.example\"}",
				 PG_OAUTH_METADATA_ISSUER_MISMATCH,
				 "issuer was normalized before comparison");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https://keys.example\"}",
				 PG_OAUTH_METADATA_INVALID_JSON, "duplicate issuer was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https://keys.example\","
				 "\"extra\":{\"name\":1,\"name\":2}}",
				 PG_OAUTH_METADATA_INVALID_JSON,
				 "duplicate nested metadata member was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https://keys.example\"} trailing",
				 PG_OAUTH_METADATA_INVALID_JSON, "trailing JSON data was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\"}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI, "missing jwks_uri was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\",\"jwks_uri\":1}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "non-string jwks_uri was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"/jwks\"}", PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "relative jwks_uri was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"HTTPS://keys.example/jwks\"}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "non-canonical HTTPS scheme was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https:///jwks\"}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "jwks_uri without a host was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https://keys.example:invalid/jwks\"}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "jwks_uri with an invalid port was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https://user:secret@keys.example/jwks\"}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "credentials in jwks_uri were accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"https://keys.example/jwks#fragment\"}",
				 PG_OAUTH_METADATA_INVALID_JWKS_URI,
				 "fragment in jwks_uri was accepted");
	expect_error("{\"issuer\":\"https://issuer.example\","
				 "\"jwks_uri\":\"http://keys.example/jwks\"}",
				 PG_OAUTH_METADATA_INSECURE_URI, "HTTP jwks_uri was accepted by default");

	policy.allow_insecure_http = true;
	if (pg_oauth_metadata_parse(
								"{\"issuer\":\"http://localhost:8000\","
								"\"jwks_uri\":\"http://localhost:8000/jwks\"}",
								strlen("{\"issuer\":\"http://localhost:8000\","
									   "\"jwks_uri\":\"http://localhost:8000/jwks\"}"),
								"http://localhost:8000", &policy, &metadata) != PG_OAUTH_METADATA_OK)
		fail("explicit development HTTP policy was ignored");
	pg_oauth_metadata_clear(&metadata);

	policy = valid_policy();
	policy.max_document_size = strlen(valid) - 1;
	expect_error_with_policy(valid, "https://issuer.example", &policy,
							 PG_OAUTH_METADATA_TOO_LARGE, "oversized metadata was accepted");
	policy = valid_policy();
	policy.max_url_size = strlen("https://keys.example/oauth/jwks?version=1") - 1;
	expect_error_with_policy(valid, "https://issuer.example", &policy,
							 PG_OAUTH_METADATA_INVALID_JWKS_URI, "oversized jwks_uri was accepted");
	policy = valid_policy();
	expect_error_with_policy(valid, "https://issuer.example?tenant=other", &policy,
							 PG_OAUTH_METADATA_INVALID_ISSUER,
							 "configured issuer with a query was accepted");

	if (strstr(pg_oauth_metadata_error_code(
											PG_OAUTH_METADATA_ISSUER_MISMATCH), "issuer.example") != NULL)
		fail("stable metadata error exposed input data");
	return EXIT_SUCCESS;
}
