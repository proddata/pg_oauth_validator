#include <stddef.h>
#include <stdint.h>

#include "metadata.h"

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	PgOAuthMetadataPolicy policy = {
		.max_document_size = 65536,
		.max_url_size = 2048,
		.allow_insecure_http = false,
	};
	PgOAuthMetadata metadata;

	if (size > policy.max_document_size)
		return 0;
	if (pg_oauth_metadata_parse((const char *) data, size,
								"https://issuer.example", &policy, &metadata) == PG_OAUTH_METADATA_OK)
		pg_oauth_metadata_clear(&metadata);
	return 0;
}
