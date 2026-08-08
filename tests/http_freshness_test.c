#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_freshness.h"

static const int64_t RESPONSE_TIME = 784111787;

static void
fail(const char *message)
{
	fprintf(stderr, "http_freshness_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthHttpFreshness
calculate(const char *cache_control, const char *date, const char *expires,
		  const char *age)
{
	PgOAuthHttpFreshnessPolicy policy = {300000, 3600000};
	PgOAuthHttpFreshness result;

	if (pg_oauth_http_freshness_calculate(cache_control, date, expires, age,
										  RESPONSE_TIME, &policy, &result) != PG_OAUTH_HTTP_FRESHNESS_OK)
		fail("valid freshness headers were rejected");
	return result;
}

static void
expect_invalid(const char *cache_control, const char *date,
			   const char *expires, const char *age, const char *message)
{
	PgOAuthHttpFreshnessPolicy policy = {300000, 3600000};
	PgOAuthHttpFreshness result;

	if (pg_oauth_http_freshness_calculate(cache_control, date, expires, age,
										  RESPONSE_TIME, &policy, &result) !=
		PG_OAUTH_HTTP_FRESHNESS_INVALID_HEADER)
		fail(message);
}

int
main(void)
{
	PgOAuthHttpFreshness result;
	PgOAuthHttpFreshnessPolicy bad_policy = {2000, 1000};

	result = calculate(NULL, NULL, NULL, NULL);
	if (result.ttl_ms != 300000 || result.explicit_freshness ||
		!result.cacheable || result.revalidation_required)
		fail("default freshness was calculated incorrectly");

	result = calculate("public, max-age=120", NULL, NULL, "20");
	if (result.ttl_ms != 100000 || !result.explicit_freshness)
		fail("max-age and Age were not combined");
	result = calculate("MAX-AGE=\"7200\"", NULL, NULL, NULL);
	if (result.ttl_ms != 3600000)
		fail("quoted or capped max-age was handled incorrectly");
	result = calculate("max-age=20", NULL, NULL, "20");
	if (result.ttl_ms != 0)
		fail("age equality extended freshness");

	result = calculate("no-store, max-age=60", NULL, NULL, NULL);
	if (result.cacheable)
		fail("no-store response was cacheable");
	result = calculate("no-cache, max-age=60", NULL, NULL, NULL);
	if (result.ttl_ms != 0 || !result.revalidation_required)
		fail("no-cache did not require immediate revalidation");
	result = calculate("must-revalidate, max-age=60", NULL, NULL, NULL);
	if (result.ttl_ms != 60000 || !result.revalidation_required)
		fail("must-revalidate incorrectly removed fresh use");

	result = calculate(NULL, "Sun, 06 Nov 1994 08:49:37 GMT",
					   "Sun, 06 Nov 1994 08:51:37 GMT", "20");
	if (result.ttl_ms != 100000 || !result.explicit_freshness)
		fail("Expires freshness was calculated incorrectly");
	result = calculate("max-age=60", "Sun, 06 Nov 1994 08:48:37 GMT",
					   NULL, "10");
	if (result.ttl_ms != 0)
		fail("apparent response age was ignored");

	expect_invalid("max-age=1,max-age=2", NULL, NULL, NULL,
				   "duplicate max-age was accepted");
	expect_invalid("max-age=invalid", NULL, NULL, NULL,
				   "malformed max-age was accepted");
	expect_invalid("no-store=value", NULL, NULL, NULL,
				   "valued no-store was accepted");
	expect_invalid("max-age=60", NULL, NULL, "1,2",
				   "ambiguous Age was accepted");
	expect_invalid(NULL, "invalid-date", "also-invalid", NULL,
				   "malformed HTTP dates were accepted");

	if (pg_oauth_http_freshness_calculate(NULL, NULL, NULL, NULL,
										  RESPONSE_TIME, &bad_policy, &result) !=
		PG_OAUTH_HTTP_FRESHNESS_INVALID_ARGUMENT)
		fail("inconsistent local freshness bounds were accepted");
	if (strstr(pg_oauth_http_freshness_error_code(
												  PG_OAUTH_HTTP_FRESHNESS_INVALID_HEADER), "max-age") != NULL)
		fail("stable freshness error exposed header contents");
	return EXIT_SUCCESS;
}
