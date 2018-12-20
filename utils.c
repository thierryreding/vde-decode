#include <stdint.h>

#include "utils.h"

void hexdump(const void *data, size_t size, size_t block_size,
	     const char *indent, FILE *fp)
{
	const uint8_t *ptr = data;
	size_t i, j;

	for (j = 0; j < size; j += block_size) {
		const char *prefix = indent ?: "";

		for (i = 0; (i < block_size) && ((i + j) < size); i++) {
			printf("%s%02x", prefix, ptr[j + i]);
			prefix = " ";
		}

		printf("\n");
	}
}
