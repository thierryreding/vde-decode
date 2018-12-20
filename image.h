#ifndef IMAGE_H
#define IMAGE_H

struct image {
	unsigned int width;
	unsigned int height;
	uint32_t format;

	unsigned int pitch;
	uint8_t *data;
	size_t size;

	unsigned int offsets[3];
};

int image_create(struct image **imagep, unsigned int width,
		 unsigned int height, uint32_t format);
void image_free(struct image *image);
void image_dump(struct image *image, FILE *fp);

#endif
