#include "postgres.h"

#include "fmgr.h"
#include "utils/builtins.h"
#include "varatt.h"

#include "shared_cache.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_oauth_test_cache_refresh);
PG_FUNCTION_INFO_V1(pg_oauth_test_cache_begin);
PG_FUNCTION_INFO_V1(pg_oauth_test_cache_lookup);

static void
text_key(text *value, const void **key, size_t *key_length)
{
	*key = VARDATA_ANY(value);
	*key_length = (size_t) VARSIZE_ANY_EXHDR(value);
}

Datum
pg_oauth_test_cache_refresh(PG_FUNCTION_ARGS)
{
	text *key_text = PG_GETARG_TEXT_PP(0);
	int64 now_ms = PG_GETARG_INT64(1);
	int32 delay_ms = PG_GETARG_INT32(2);
	text *payload_text = PG_GETARG_TEXT_PP(3);
	const void *key;
	size_t key_length;
	PgOAuthCacheRefresh refresh;
	PgOAuthCacheRefreshResult result;

	if (delay_ms < 0 || delay_ms > 10000)
		ereport(ERROR, (errmsg("invalid cache probe delay")));
	text_key(key_text, &key, &key_length);
	result = pg_oauth_shared_cache_begin_refresh(key, key_length, now_ms,
		false, 0, &refresh);
	if (result == PG_OAUTH_CACHE_REFRESH_STARTED)
	{
		pg_usleep((long) delay_ms * 1000L);
		if (!pg_oauth_shared_cache_complete_refresh(&refresh, now_ms, true,
			true, false, 60000, 0, VARDATA_ANY(payload_text),
			(size_t) VARSIZE_ANY_EXHDR(payload_text)))
			ereport(ERROR, (errmsg("cache probe refresh completion failed")));
	}
	PG_RETURN_INT32((int32) result);
}

Datum
pg_oauth_test_cache_begin(PG_FUNCTION_ARGS)
{
	text *key_text = PG_GETARG_TEXT_PP(0);
	int64 now_ms = PG_GETARG_INT64(1);
	const void *key;
	size_t key_length;
	PgOAuthCacheRefresh refresh;
	PgOAuthCacheRefreshResult result;

	text_key(key_text, &key, &key_length);
	result = pg_oauth_shared_cache_begin_refresh(key, key_length, now_ms,
		false, 0, &refresh);
	PG_RETURN_INT32((int32) result);
}

Datum
pg_oauth_test_cache_lookup(PG_FUNCTION_ARGS)
{
	text *key_text = PG_GETARG_TEXT_PP(0);
	int64 now_ms = PG_GETARG_INT64(1);
	const void *key;
	size_t key_length;
	unsigned char *payload;
	PgOAuthSharedCacheLookup lookup;
	text *result;

	text_key(key_text, &key, &key_length);
	payload = palloc(PG_OAUTH_CACHE_MAX_PAYLOAD_SIZE);
	lookup = pg_oauth_shared_cache_lookup(key, key_length, now_ms, false,
		payload, PG_OAUTH_CACHE_MAX_PAYLOAD_SIZE);
	if (lookup.freshness == PG_OAUTH_CACHE_MISS ||
		lookup.copy_result != PG_OAUTH_CACHE_COPY_OK)
		PG_RETURN_NULL();
	result = palloc(VARHDRSZ + lookup.payload_length);
	SET_VARSIZE(result, VARHDRSZ + lookup.payload_length);
	memcpy(VARDATA(result), payload, lookup.payload_length);
	PG_RETURN_TEXT_P(result);
}
