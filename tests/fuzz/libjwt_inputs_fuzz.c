#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <jwt.h>

#define MAX_FUZZ_INPUT_SIZE (16U * 1024U)

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	jwt_checker_t *checker;
	jwk_set_t  *set;
	char	   *input;

	if (size > MAX_FUZZ_INPUT_SIZE)
		return 0;

	input = malloc(size + 1);
	if (input == NULL)
		return 0;
	memcpy(input, data, size);
	input[size] = '\0';

	checker = jwt_checker_new();
	if (checker != NULL)
	{
		(void) jwt_checker_verify(checker, input);
		jwt_checker_free(checker);
	}

	set = jwks_create_strn(input, size);
	if (set != NULL)
		jwks_free(set);

	free(input);
	return 0;
}
