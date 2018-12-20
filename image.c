#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "drm-utils.h"
#include "image.h"

int image_create(struct image **imagep, unsigned int width,
		 unsigned int height, uint32_t format)
{
	const struct drm_format_info *info;
	struct image *image;
	unsigned int i;
	size_t size;

	info = drm_format_get_info(format);
	if (!info)
		return -EINVAL;

	image = calloc(1, sizeof(*image));
	if (!image)
		return -ENOMEM;

	image->width = width;
	image->height = height;
	image->format = format;

	image->pitch = width * info->cpp[0];
	size = image->pitch * height;

	for (i = 1; i < info->num_planes; i++) {
		unsigned int pitch = width * info->cpp[i] / info->hsub;

		image->offsets[i] = size;

		size += pitch * height / info->vsub;
	}

	image->size = size;

	image->data = calloc(1, image->size);
	if (!image->data) {
		free(image);
		return -ENOMEM;
	}

	if (imagep)
		*imagep = image;

	return 0;
}

void image_free(struct image *image)
{
	if (image)
		free(image->data);

	free(image);
}

void image_dump(struct image *image, FILE *fp)
{
	const struct drm_format_info *info;
	unsigned int i, j, k;

	info = drm_format_get_info(image->format);
	if (!info) {
		fprintf(stderr, "invalid format: %08x\n", image->format);
		return;
	}

	fprintf(fp, "image: %ux%u\n", image->width, image->height);
	fprintf(fp, "  format: %08x\n", image->format);
	fprintf(fp, "  pitch: %u\n", image->pitch);
	fprintf(fp, "  size: %zu\n", image->size);
	fprintf(fp, "  data: %p\n", image->data);

	for (k = 0; k < info->num_planes; k++) {
		unsigned int width = image->width;
		unsigned int height = image->height;
		unsigned int pitch;

		if (k > 0) {
			width /= info->hsub;
			height /= info->vsub;
		}

		pitch = width * info->cpp[k];

		fprintf(fp, "    %u: %ux%u (%u bytes)\n", k, width, height,
			pitch);

		for (j = 0; j < height; j++) {
			unsigned int offset = image->offsets[k] + j * pitch;

			fprintf(fp, "     ");

			for (i = 0; i < pitch; i++)
				fprintf(fp, " %02x", image->data[offset + i]);

			fprintf(fp, "\n");
		}
	}
}
