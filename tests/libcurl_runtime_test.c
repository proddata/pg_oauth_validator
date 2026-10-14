#include <stdio.h>

#include <curl/curl.h>

int
main(void)
{
	const curl_version_info_data *info;

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
	{
		fputs("libcurl initialization failed\n", stderr);
		return 1;
	}
	info = curl_version_info(CURLVERSION_NOW);
	if (info == NULL || info->version_num < 0x075700 ||
		(info->features & CURL_VERSION_ASYNCHDNS) == 0)
	{
		fputs("runtime libcurl requires 7.87.0 and asynchronous DNS\n", stderr);
		curl_global_cleanup();
		return 1;
	}
	printf("libcurl %s; DNS resolver: %s\n", info->version,
		   info->ares != NULL ? "c-ares" : "threaded");
	curl_global_cleanup();
	return 0;
}
