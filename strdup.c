#include <stdlib.h>
#include <string.h>

#include "config.h"

#ifndef HAVE_STRDUP
char *strdup(const char *src)
{
	size_t len = strlen(src) + 1;

	char *dest = malloc(len);
	if (dest == NULL) {
		return NULL;
	}

	return memcpy(dest, src, len);
}
#endif
