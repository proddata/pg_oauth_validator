#define _GNU_SOURCE

#include <dlfcn.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int
getaddrinfo(const char *node, const char *service,
			const struct addrinfo *hints, struct addrinfo **result)
{
	int			(*real_getaddrinfo) (const char *, const char *,
									 const struct addrinfo *, struct addrinfo **);
	const char *marker;
	const char *delay;
	int			fd;

	if (node != NULL && strcmp(node, "resolver-probe.invalid") == 0)
	{
		marker = getenv("DNS_FAILURE_SHIM_MARKER");
		if (marker != NULL)
		{
			fd = open(marker, O_WRONLY | O_CREAT | O_APPEND, 0600);
			if (fd >= 0)
			{
				(void) write(fd, "1", 1);
				(void) close(fd);
			}
		}
		delay = getenv("DNS_FAILURE_SHIM_DELAY");
		if (delay != NULL)
		{
			struct timespec pause = {3, 0};

			(void) nanosleep(&pause, NULL);
		}
		return EAI_AGAIN;
	}
	real_getaddrinfo = dlsym(RTLD_NEXT, "getaddrinfo");
	if (real_getaddrinfo == NULL)
		return EAI_SYSTEM;
	return real_getaddrinfo(node, service, hints, result);
}
