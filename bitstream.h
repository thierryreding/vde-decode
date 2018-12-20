#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bitstream {
	const uint8_t *data;
	size_t offset, bit;
	size_t size;
};

void bitstream_init(struct bitstream *bs, const uint8_t *data, size_t size);
size_t bitstream_available(struct bitstream *bs);
bool bitstream_more_rbsp_data(struct bitstream *bs);
int bitstream_read(struct bitstream *bs, uint8_t *value);
int bitstream_read_u8(struct bitstream *bs, uint8_t *valuep, size_t length);
int bitstream_read_u16(struct bitstream *bs, uint16_t *valuep, size_t length);
int bitstream_read_u32(struct bitstream *bs, uint32_t *valuep, size_t length);
int bitstream_read_ue(struct bitstream *bs, uint32_t *valuep, size_t *lengthp);
int bitstream_read_se(struct bitstream *bs, int32_t *valuep, size_t *lengthp);

#endif
