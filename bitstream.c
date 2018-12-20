#include <errno.h>

#include "bitstream.h"

void bitstream_init(struct bitstream *bs, const uint8_t *data, size_t size)
{
	bs->data = data;
	bs->size = size;
	bs->offset = 0;
	bs->bit = 7;
}

size_t bitstream_available(struct bitstream *bs)
{
	return (bs->size * 8) - ((bs->offset * 8) + (7 - bs->bit));
}

bool bitstream_more_rbsp_data(struct bitstream *bs)
{
	unsigned int i;

	if (bitstream_available(bs) == 0)
		return false;

	for (i = 0; i < 8; i++) {
		if (bs->data[bs->size - 1] & (1 << i))
			break;
	}

	if ((bs->offset == bs->size - 1) && (bs->bit == i))
		return false;

	return true;
}

int bitstream_read(struct bitstream *bs, uint8_t *value)
{
	if (bs->offset >= bs->size)
		return -ENOSPC;

	if (bs->data[bs->offset] & (1 << bs->bit))
		*value = 1;
	else
		*value = 0;

	if (bs->bit-- == 0) {
		bs->offset++;
		bs->bit = 7;
	}

	return 0;
}

int bitstream_read_u8(struct bitstream *bs, uint8_t *valuep, size_t length)
{
	uint32_t value = 0;
	size_t i;

	if (length > 8)
		return -EINVAL;

	for (i = 0; i < length; i++) {
		uint8_t bit;
		int err;

		err = bitstream_read(bs, &bit);
		if (err < 0)
			return err;

		value = (value << 1) | bit;
	}

	if (valuep)
		*valuep = value;

	return 0;
}

int bitstream_read_u16(struct bitstream *bs, uint16_t *valuep, size_t length)
{
	uint16_t value = 0;
	size_t i;

	if (length > 16)
		return -EINVAL;

	for (i = 0; i < length; i++) {
		uint8_t bit;
		int err;

		err = bitstream_read(bs, &bit);
		if (err < 0)
			return err;

		value = (value << 1) | bit;
	}

	if (valuep)
		*valuep = value;

	return 0;
}

int bitstream_read_u32(struct bitstream *bs, uint32_t *valuep, size_t length)
{
	uint32_t value = 0;
	size_t i;

	if (length > 32)
		return -EINVAL;

	for (i = 0; i < length; i++) {
		uint8_t bit;
		int err;

		err = bitstream_read(bs, &bit);
		if (err < 0)
			return err;

		value = (value << 1) | bit;
	}

	if (valuep)
		*valuep = value;

	return 0;
}

int bitstream_read_ue(struct bitstream *bs, uint32_t *valuep, size_t *lengthp)
{
	uint32_t value;
	size_t length;
	int err;

	for (length = 0; length < 32; length++) {
		uint8_t bit;

		err = bitstream_read(bs, &bit);
		if (err < 0)
			return err;

		if (bit != 0)
			break;
	}

	if (length >= 32)
		return -ERANGE;

	err = bitstream_read_u32(bs, &value, length);
	if (err < 0)
		return err;

	if (valuep)
		*valuep = ((1 << length) - 1) + value;

	if (lengthp)
		*lengthp = length * 2 + 1;

	return 0;
}

int bitstream_read_se(struct bitstream *bs, int32_t *valuep, size_t *lengthp)
{
	uint32_t code;
	int err;

	/*
	printf("  bitstream: %zu bits, position: %zu\n", bs->size * 8, bs->offset * 8 + (7 - bs->bit));
	*/

	err = bitstream_read_ue(bs, &code, lengthp);
	if (err < 0)
		return err;

	code = (code + 1) / 2;

	if (code % 2 == 0)
		*valuep = -code;
	else
		*valuep = code;

	return 0;
}
