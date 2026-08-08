#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpq-fe.h"

#define MAX_TEST_TOKEN_SIZE (64 * 1024)

static char *test_token;

static void
clear_token(void)
{
	if (test_token != NULL)
	{
		volatile unsigned char *cursor = (volatile unsigned char *) test_token;
		size_t		length = strlen(test_token);

		while (length-- > 0)
			*cursor++ = 0;
		free(test_token);
		test_token = NULL;
	}
}

static char *
read_token_file(const char *path)
{
	char   *buffer;
	FILE   *file;
	size_t	length;

	file = fopen(path, "rb");
	if (file == NULL)
	{
		fprintf(stderr, "could not open token file: %s\n", strerror(errno));
		return NULL;
	}

	buffer = malloc(MAX_TEST_TOKEN_SIZE + 1);
	if (buffer == NULL)
	{
		fclose(file);
		return NULL;
	}

	length = fread(buffer, 1, MAX_TEST_TOKEN_SIZE + 1, file);
	if (ferror(file) || length == 0 || length > MAX_TEST_TOKEN_SIZE)
	{
		fclose(file);
		free(buffer);
		return NULL;
	}
	if (fclose(file) != 0)
	{
		free(buffer);
		return NULL;
	}

	while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
		length--;
	if (length == 0)
	{
		free(buffer);
		return NULL;
	}
	buffer[length] = '\0';

	return buffer;
}

static int
provide_token(PGauthData type, PGconn *connection, void *data)
{
	PGoauthBearerRequest *request = data;

	(void) connection;

	if (type != PQAUTHDATA_OAUTH_BEARER_TOKEN)
		return 0;

	request->token = test_token;
	return 1;
}

int
main(int argc, char **argv)
{
	PGconn *connection;
	int		result;

	if (argc != 3)
	{
		fprintf(stderr, "usage: %s TOKEN_FILE CONNINFO\n", argv[0]);
		return EXIT_FAILURE;
	}

	test_token = read_token_file(argv[1]);
	if (test_token == NULL)
	{
		fprintf(stderr, "could not read a valid token\n");
		return EXIT_FAILURE;
	}

	PQsetAuthDataHook(provide_token);
	connection = PQconnectdb(argv[2]);
	result = PQstatus(connection) == CONNECTION_OK ? EXIT_SUCCESS : EXIT_FAILURE;
	if (result != EXIT_SUCCESS)
		fprintf(stderr, "connection failed: %s", PQerrorMessage(connection));

	PQfinish(connection);
	clear_token();
	return result;
}
