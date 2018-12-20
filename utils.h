#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define ALIGN(x, a) ALIGN_MASK(x, (typeof(x))(a) - 1)

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

void hexdump(const void *data, size_t size, size_t block_size,
	     const char *indent, FILE *fp);

#endif
