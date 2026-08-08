#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache_state.h"

static void
fail(const char *message)
{
	fprintf(stderr, "cache_state_test: %s\n", message);
	exit(EXIT_FAILURE);
}

static PgOAuthCacheRefresh
begin(PgOAuthCache *cache, const char *key, int64_t now_ms, bool unknown_kid,
	  int64_t cooldown_ms)
{
	PgOAuthCacheRefresh refresh;

	if (pg_oauth_cache_begin_refresh(cache, key, strlen(key), now_ms,
									 unknown_kid, cooldown_ms, &refresh) != PG_OAUTH_CACHE_REFRESH_STARTED)
		fail("refresh did not start");
	return refresh;
}

static void
store(PgOAuthCache *cache, const char *key, int64_t now_ms, int64_t ttl_ms,
	  int64_t stale_ms)
{
	PgOAuthCacheRefresh refresh = begin(cache, key, now_ms, false, 0);
	static const char payload[] = "validated-payload";

	if (!pg_oauth_cache_complete_refresh(cache, &refresh, now_ms, true, true,
										 false, ttl_ms, stale_ms, payload, sizeof(payload) - 1))
		fail("refresh completion failed");
}

int
main(void)
{
	PgOAuthCacheEntry entries[2];
	PgOAuthCache cache;
	PgOAuthCache attached_cache;
	PgOAuthCacheRefresh refresh;
	PgOAuthCacheRefresh scratch_refresh;
	size_t		index;
	char		oversized[PG_OAUTH_CACHE_MAX_KEY_SIZE + 1];
	char		oversized_payload[PG_OAUTH_CACHE_MAX_PAYLOAD_SIZE + 1];
	char		payload[64];
	size_t		payload_length;

	if (!pg_oauth_cache_init(&cache, entries, 2))
		fail("valid cache initialization failed");
	if (pg_oauth_cache_lookup(&cache, "issuer-a", 8, 0, false, &index) !=
		PG_OAUTH_CACHE_MISS || index != SIZE_MAX)
		fail("cold lookup did not miss");

	store(&cache, "issuer-a|policy-v1", 1000, 100, 50);
	if (pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1099, false,
							  &index) != PG_OAUTH_CACHE_FRESH)
		fail("fresh entry was not returned");
	if (pg_oauth_cache_copy_payload(&cache, index, payload, sizeof(payload),
									&payload_length) != PG_OAUTH_CACHE_COPY_OK ||
		payload_length != strlen("validated-payload") ||
		memcmp(payload, "validated-payload", payload_length) != 0)
		fail("validated payload was not copied exactly");
	if (pg_oauth_cache_copy_payload(&cache, index, payload, 1,
									&payload_length) != PG_OAUTH_CACHE_COPY_TOO_SMALL ||
		payload_length != strlen("validated-payload"))
		fail("undersized payload destination was not reported safely");
	refresh = begin(&cache, "issuer-a|policy-v1", 1050, false, 0);
	if (!pg_oauth_cache_complete_refresh(&cache, &refresh, 1051, false, true,
										 false, 100, 50, NULL, 0) ||
		pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1051, false,
							  NULL) != PG_OAUTH_CACHE_FRESH)
		fail("failed refresh destroyed an eligible cached value");
	if (pg_oauth_cache_copy_payload(&cache, index, payload, sizeof(payload),
									&payload_length) != PG_OAUTH_CACHE_COPY_OK ||
		memcmp(payload, "validated-payload", payload_length) != 0)
		fail("failed refresh changed the cached payload");
	if (pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1100, false,
							  NULL) != PG_OAUTH_CACHE_MISS)
		fail("freshness boundary was extended");
	if (pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1100, true,
							  NULL) != PG_OAUTH_CACHE_STALE ||
		pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1150, true,
							  NULL) != PG_OAUTH_CACHE_MISS)
		fail("stale grace boundary was enforced incorrectly");
	if (pg_oauth_cache_lookup(&cache, "issuer-a|policy-v2", 18, 1050, true,
							  NULL) != PG_OAUTH_CACHE_MISS)
		fail("changed policy key reused cached trust");

	refresh = begin(&cache, "issuer-a|policy-v1", 1200, true, 30);
	if (pg_oauth_cache_begin_refresh(&cache, "issuer-a|policy-v1", 18, 1201,
									 true, 30, &scratch_refresh) !=
		PG_OAUTH_CACHE_REFRESH_IN_PROGRESS)
		fail("concurrent refresh was not suppressed");
	if (!pg_oauth_cache_complete_refresh(&cache, &refresh, 1202, false, true,
										 false, 100, 50, NULL, 0))
		fail("failed refresh completion was rejected");
	if (pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1202, true,
							  NULL) != PG_OAUTH_CACHE_MISS)
		fail("entry outside stale grace survived as usable");
	if (pg_oauth_cache_begin_refresh(&cache, "issuer-a|policy-v1", 18, 1229,
									 true, 30, &scratch_refresh) !=
		PG_OAUTH_CACHE_REFRESH_SUPPRESSED)
		fail("unknown-kid cooldown was bypassed");
	refresh = begin(&cache, "issuer-a|policy-v1", 1230, true, 30);
	if (!pg_oauth_cache_complete_refresh(&cache, &refresh, 1230, true, true,
										 true, 100, 1000, "replacement", strlen("replacement")))
		fail("revalidation-required entry was not stored");
	if (pg_oauth_cache_lookup(&cache, "issuer-a|policy-v1", 18, 1330, true,
							  NULL) != PG_OAUTH_CACHE_MISS)
		fail("must-revalidate entry was used stale");

	refresh = begin(&cache, "no-store", 1400, false, 0);
	if (!pg_oauth_cache_complete_refresh(&cache, &refresh, 1400, true, false,
										 false, 100, 100, "transient", strlen("transient")))
		fail("no-store completion failed");
	if (pg_oauth_cache_lookup(&cache, "no-store", 8, 1400, true, NULL) !=
		PG_OAUTH_CACHE_MISS)
		fail("no-store response was retained");

	/* Both slots are now reusable; verify deterministic expired LRU eviction. */
	if (!pg_oauth_cache_init(&cache, entries, 2))
		fail("cache reset before eviction failed");
	store(&cache, "oldest", 2000, 10, 0);
	store(&cache, "newer", 2001, 100, 0);
	refresh = begin(&cache, "replacement", 2011, false, 0);
	if (cache.control->stats.evictions != 1 ||
		pg_oauth_cache_lookup(&cache, "oldest", 6, 2011, true, NULL) !=
		PG_OAUTH_CACHE_MISS)
		fail("expired least-recently-used entry was not evicted");
	if (!pg_oauth_cache_complete_refresh(&cache, &refresh, 2011, true, true,
										 false, 100, 0, "replacement", strlen("replacement")))
		fail("replacement refresh failed");
	if (pg_oauth_cache_begin_refresh(&cache, "third", 5, 2012, false, 0,
									 &scratch_refresh) != PG_OAUTH_CACHE_REFRESH_CAPACITY)
		fail("fresh entries were evicted at capacity");

	memset(oversized, 'x', sizeof(oversized));
	if (pg_oauth_cache_begin_refresh(&cache, oversized, sizeof(oversized), 3000,
									 false, 0, &refresh) != PG_OAUTH_CACHE_REFRESH_INVALID_ARGUMENT)
		fail("oversized cache key was accepted");
	if (pg_oauth_cache_copy_payload(&cache, cache.capacity, payload,
									sizeof(payload), &payload_length) !=
		PG_OAUTH_CACHE_COPY_INVALID_ARGUMENT)
		fail("out-of-range payload copy was accepted");

	/* A stale completion token must not mutate a later refresh owner. */
	if (!pg_oauth_cache_init(&cache, entries, 2))
		fail("cache reset failed");
	refresh = begin(&cache, "serial", 4000, false, 0);
	{
		PgOAuthCacheRefresh stale_refresh = refresh;

		if (!pg_oauth_cache_complete_refresh(&cache, &refresh, 4000, false,
											 true, false, 0, 0, NULL, 0))
			fail("initial failed refresh was rejected");
		refresh = begin(&cache, "serial", 4001, false, 0);
		if (pg_oauth_cache_complete_refresh(&cache, &refresh, 4001, true, true,
											false, 100, 0, oversized_payload, sizeof(oversized_payload)))
			fail("oversized cache payload was accepted");
		if (pg_oauth_cache_complete_refresh(&cache, &stale_refresh, 4001, true,
											true, false, 100, 0, "stale", strlen("stale")))
			fail("stale refresh owner overwrote current state");
	}

	if (cache.control->stats.refreshes == 0 ||
		cache.control->stats.refresh_suppressions != 0)
		fail("cache counters were not maintained deterministically");
	if (!pg_oauth_cache_attach(&attached_cache, entries, 2, cache.control) ||
		pg_oauth_cache_begin_refresh(&attached_cache, "serial", 6, 4002, false,
									 0, &scratch_refresh) !=
		PG_OAUTH_CACHE_REFRESH_IN_PROGRESS)
		fail("attached cache view did not share refresh ownership");
	return EXIT_SUCCESS;
}
