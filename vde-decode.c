#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "tegra_drm.h"
#include "tegra-vde.h"

struct bitstream {
	const uint8_t *data;
	size_t offset, bit;
	size_t size;
};

static void bitstream_init(struct bitstream *bs, const uint8_t *data,
			   size_t size)
{
	bs->data = data;
	bs->size = size;
	bs->offset = 0;
	bs->bit = 7;
}

static size_t bitstream_available(struct bitstream *bs)
{
	return (bs->size * 8) - ((bs->offset * 8) + (7 - bs->bit));
}

static bool bitstream_more_rbsp_data(struct bitstream *bs)
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

static int bitstream_read(struct bitstream *bs, uint8_t *value)
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

static int bitstream_read_u8(struct bitstream *bs, uint8_t *valuep,
			     size_t length)
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

static int bitstream_read_u32(struct bitstream *bs, uint32_t *valuep,
			      size_t length)
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

static int bitstream_read_ue(struct bitstream *bs, uint32_t *valuep,
			     size_t *lengthp)
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

static int bitstream_read_se(struct bitstream *bs, int32_t *valuep,
			     size_t *lengthp)
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

struct h264_vui_parameters {
	uint8_t aspect_ratio_info_present_flag;
	/* only for aspect_ratio_info_present_flag */
	uint8_t aspect_ratio_idc;
	/* only for aspect_ratio_idc == 255 */
	uint16_t sar_width;
	uint16_t sar_height;
	/* ... */
	/* ... */
	uint8_t overscan_info_present_flag;
	uint8_t overscan_appropriate_flag;
	uint8_t video_signal_type_present_flag;
	/* only for video_signal_type_present_flag == 1 */
	uint8_t video_format;
	uint8_t video_full_range_flag;
	uint8_t colour_description_present_flag;
	uint8_t colour_primaries;
	uint8_t transfer_characteristics;
	uint8_t matrix_coefficients;
	/* ... */
	uint8_t chroma_loc_info_present_flag;
	uint32_t chroma_sample_loc_type_top_field;
	uint32_t choram_sample_loc_type_bottom_field;
	uint8_t timing_info_present_flag;
	uint32_t num_units_in_tick;
	uint32_t time_scale;
	uint8_t fixed_frame_rate_flag;
	uint8_t nal_hrd_parameters_present_flag;
	/* XXX complete this */
};

struct h264_sps {
	uint8_t profile_idc;
#if 0
	uint8_t constraint_set0_flag;
	uint8_t constraint_set1_flag;
	uint8_t constraint_set2_flag;
	uint8_t constraint_set3_flag;
	uint8_t constraint_set4_flag;
	uint8_t constraint_set5_flag;
	uint8_t reserved_zero_2bits;
#else
	uint8_t flags;
#endif
	uint8_t level_idc;
	uint32_t seq_parameter_set_id;
	uint32_t chroma_format_idc;
	uint8_t separate_colour_plane_flag;
	uint32_t bit_depth_luma_minus8;
	uint32_t bit_depth_chroma_minus8;
	uint8_t qpprime_y_zero_transform_bypass_flag;
	uint8_t seq_scaling_matrix_present_flag;
	uint32_t log2_max_frame_num_minus4;
	uint32_t pic_order_cnt_type;
	/* only for pic_order_cnt_type == 0 */
	uint32_t log2_max_pic_order_cnt_lsb_minus4;
	/* only for pic_order_cnt_type == 1 */
	uint8_t delta_pic_order_always_zero_flag;
	int32_t offset_for_non_ref_pic;
	int32_t offset_for_top_to_bottom_field;
	uint32_t num_ref_frames_in_pic_order_cnt_cycle;
	int32_t *offset_for_ref_frame;
	/* ... */
	uint32_t max_num_ref_frames;
	uint8_t gaps_in_frame_num_value_allowed_flag;
	uint32_t pic_width_in_mbs_minus1;
	uint32_t pic_height_in_map_units_minus1;
	uint8_t frame_mbs_only_flag;
	/* only for frame_mbs_only_flag == 0 */
	uint8_t mb_adaptive_frame_field_flag;
	/* ... */
	uint8_t direct_8x8_inference_flag;
	uint8_t frame_cropping_flag;
	/* only for frame_cropping_flag == 1 */
	uint32_t frame_crop_left_offset;
	uint32_t frame_crop_right_offset;
	uint32_t frame_crop_top_offset;
	uint32_t frame_crop_bottom_offset;
	/* ... */
	uint8_t vui_parameters_present_flag;
	struct h264_vui_parameters vui_parameters;
};

struct h264_pps {
	uint32_t pic_parameter_set_id;
	uint32_t seq_parameter_set_id;
	uint8_t entropy_coding_mode_flag;
	uint8_t bottom_field_pic_order_in_frame_present_flag;
	uint32_t num_slice_groups_minus1;
	uint32_t slice_group_map_type;
	uint32_t *run_length_minus1;
	uint32_t *top_left;
	uint32_t *bottom_right;
	uint8_t slice_group_change_direction_flag;
	uint32_t slice_group_change_rate_minus1;
	uint32_t pic_size_in_map_units_minus1;
	uint32_t *slice_group_id;
	uint32_t num_ref_idx_l0_default_active_minus1;
	uint32_t num_ref_idx_l1_default_active_minus1;
	uint8_t weighted_pred_flag;
	uint8_t weighted_bipred_idc;
	int32_t pic_init_qp_minus26;
	int32_t pic_init_qs_minus26;
	int32_t chroma_qp_index_offset;
	uint8_t deblocking_filter_control_present_flag;
	uint8_t constrained_intra_pred_flag;
	uint8_t redundant_pic_cnt_present_flag;
	uint8_t transform_8x8_mode_flag;
	uint8_t pic_scaling_matrix_present_flag;
	/* XXX complete this with scaling matrix */
	int32_t second_chroma_qp_index_offset;
};

struct h264_context {
	uint8_t profile;
	uint8_t compatibility;
	uint8_t level;
	uint8_t nal_size;
	uint8_t num_sps;
	uint8_t num_pps;

	struct h264_sps *sps;
	struct h264_pps *pps;
};

static void usage(const char *program, FILE *fp)
{
	fprintf(fp, "usage: %s FILENAME\n", program);
}

static void hexdump(const void *data, size_t size, const char *indent,
		    FILE *fp)
{
	const uint8_t *ptr = data;
	size_t i, j;

	for (j = 0; j < size; j += 16) {
		const char *prefix = indent ?: "";

		for (i = 0; (i < 16) && ((i + j) < size); i++) {
			printf("%s%02x", prefix, ptr[j + i]);
			prefix = " ";
		}

		printf("\n");
	}
}

static int h264_sps_parse(struct h264_sps *sps, const void *data, size_t size)
{
	struct bitstream bs;
	size_t len;
	int err;

	bitstream_init(&bs, data, size);

	err = bitstream_read_u8(&bs, &sps->profile_idc, 8);
	if (err < 0)
		return err;

	err = bitstream_read_u8(&bs, &sps->flags, 8);
	if (err < 0)
		return err;

	err = bitstream_read_u8(&bs, &sps->level_idc, 8);
	if (err < 0)
		return err;

	/*
	printf("      profile: %u\n", profile);
	printf("      flags: %02x\n", flags);
	printf("      level: %u\n", level);
	*/

	err = bitstream_read_ue(&bs, &sps->seq_parameter_set_id, &len);
	if (err < 0)
		return err;

	/*
	printf("      ID: %u (%zu bits)\n", id, len);
	*/

	/* currently only supports baseline */
	if (sps->profile_idc != 66) {
		return -ENOTSUP;
	}

	err = bitstream_read_ue(&bs, &sps->log2_max_frame_num_minus4, &len);
	if (err < 0)
		return err;

	/*
	printf("      max frames: %u/%u (%zu bits)\n", sps->log2_max_frame_num_minus4, 2 << (sps->log2_max_frame_num_minus4 + 4), len);
	*/

	err = bitstream_read_ue(&bs, &sps->pic_order_cnt_type, &len);
	if (err < 0)
		return err;

	/*
	printf("      pic_order_cnt_type: %u (%zu bits)\n", sps->pic_order_cnt_type, len);
	*/

	if (sps->pic_order_cnt_type == 0) {
		return -ENOTSUP;
	}

	if (sps->pic_order_cnt_type == 1) {
		return -ENOTSUP;
	}

	err = bitstream_read_ue(&bs, &sps->max_num_ref_frames, &len);
	if (err < 0)
		return err;

	/*
	printf("      max_num_ref_frames: %u (%zu bits)\n", sps->max_num_ref_frames, len);
	*/

	err = bitstream_read_u8(&bs, &sps->gaps_in_frame_num_value_allowed_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("      gaps_in_frame_num_value_allowed_flag: %u\n", sps->gaps_in_frame_num_value_allowed_flag);
	*/

	err = bitstream_read_ue(&bs, &sps->pic_width_in_mbs_minus1, &len);
	if (err < 0)
		return err;

	/*
	printf("      pic_width_in_mbs_minus1: %u (%zu bits)\n", sps->pic_width_in_mbs_minus1, len);
	*/

	err = bitstream_read_ue(&bs, &sps->pic_height_in_map_units_minus1, &len);
	if (err < 0)
		return err;

	/*
	printf("      pic_height_in_map_units_minus1: %u (%zu bits)\n", sps->pic_height_in_map_units_minus1, len);
	*/

	err = bitstream_read_u8(&bs, &sps->frame_mbs_only_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("      frame_mbs_only_flag: %u\n", sps->frame_mbs_only_flag);
	*/

	if (!sps->frame_mbs_only_flag) {
		err = bitstream_read_u8(&bs, &sps->mb_adaptive_frame_field_flag, 1);
		if (err < 0)
			return err;

		printf("        mb_adaptive_frame_field_flag: %u\n", sps->mb_adaptive_frame_field_flag);
	}

	err = bitstream_read_u8(&bs, &sps->direct_8x8_inference_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("      direct_8x8_inference_flag: %u\n", sps->direct_8x8_inference_flag);
	*/

	err = bitstream_read_u8(&bs, &sps->frame_cropping_flag, 1);
	if (err < 0)
		return err;

	if (sps->frame_cropping_flag) {
		err = bitstream_read_ue(&bs, &sps->frame_crop_left_offset, NULL);
		if (err < 0)
			return err;

		err = bitstream_read_ue(&bs, &sps->frame_crop_right_offset, NULL);
		if (err < 0)
			return err;

		err = bitstream_read_ue(&bs, &sps->frame_crop_top_offset, NULL);
		if (err < 0)
			return err;

		err = bitstream_read_ue(&bs, &sps->frame_crop_bottom_offset, NULL);
		if (err < 0)
			return err;

		/*
		printf("        left: %u right: %u top: %u bottom: %u\n", sps->frame_crop_left_offset, sps->frame_crop_right_offset, sps->frame_crop_top_offset, sps->frame_crop_bottom_offset);
		*/
	}

	err = bitstream_read_u8(&bs, &sps->vui_parameters_present_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("      vui_parameters_present_flag: %u\n", sps->vui_parameters_present_flag);
	*/

	if (sps->vui_parameters_present_flag) {
		err = bitstream_read_u8(&bs, &sps->vui_parameters.aspect_ratio_info_present_flag, 1);
		if (err < 0)
			return err;

		/*
		printf("        aspect_ratio_info_present_flag: %u\n", sps->vui_parameters->aspect_ratio_info_present_flag);
		*/

		if (sps->vui_parameters.aspect_ratio_info_present_flag) {
			err = bitstream_read_u8(&bs, &sps->vui_parameters.aspect_ratio_idc, 8);
			if (err < 0)
				return err;

			/*
			printf("          aspect_ratio_idc: %u\n", sps->vui_parameters.aspect_ratio_idc);
			*/

			if (sps->vui_parameters.aspect_ratio_idc == 255) {
				return -ENOTSUP;
			}
		}
	}

	return 0;
}

static int h264_pps_parse(struct h264_pps *pps, const void *data, size_t size)
{
	struct bitstream bs;
	size_t len;
	int err;

	/*
	printf("  PPS:\n");
	*/

	bitstream_init(&bs, data, size);

	err = bitstream_read_ue(&bs, &pps->pic_parameter_set_id, &len);
	if (err < 0)
		return err;

	/*
	printf("    pic_parameter_set_id: %u (%zu bits)\n", pps->pic_parameter_set_id, len);
	*/

	err = bitstream_read_ue(&bs, &pps->seq_parameter_set_id, &len);
	if (err < 0)
		return err;

	/*
	printf("    seq_parameter_set_id: %u (%zu bits)\n", pps->seq_parameter_set_id, len);
	*/

	err = bitstream_read_u8(&bs, &pps->entropy_coding_mode_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("    entropy_coding_mode_flag: %u\n", pps->entropy_coding_mode_flag);
	*/

	err = bitstream_read_u8(&bs, &pps->bottom_field_pic_order_in_frame_present_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("    bottom_field_pic_order_in_frame_present_flag: %u\n", pps->bottom_field_pic_order_in_frame_present_flag);
	*/

	err = bitstream_read_ue(&bs, &pps->num_slice_groups_minus1, &len);
	if (err < 0)
		return err;

	/*
	printf("    num_slice_groups_minus1: %u (%zu bits)\n", pps->num_slice_groups_minus1, len);
	*/

	if (pps->num_slice_groups_minus1 > 0) {
		return -ENOTSUP;
	}

	err = bitstream_read_ue(&bs, &pps->num_ref_idx_l0_default_active_minus1, &len);
	if (err < 0)
		return err;

	/*
	printf("    num_ref_idx_l0_default_active_minus1: %u (%zu bits)\n", pps->num_ref_idx_l0_default_active_minus1, len);
	*/

	err = bitstream_read_ue(&bs, &pps->num_ref_idx_l1_default_active_minus1, &len);
	if (err < 0)
		return err;

	/*
	printf("    num_ref_idx_l1_default_active_minus1: %u (%zu bits)\n", pps->num_ref_idx_l1_default_active_minus1, len);
	*/

	err = bitstream_read_u8(&bs, &pps->weighted_pred_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("    weighted_pred_flag: %u\n", pps->weighted_pred_flag);
	*/

	err = bitstream_read_u8(&bs, &pps->weighted_bipred_idc, 2);
	if (err < 0)
		return err;

	/*
	printf("    weighted_bipred_idc: %u\n", pps->weighted_bipred_idc);
	*/

	err = bitstream_read_se(&bs, &pps->pic_init_qp_minus26, &len);
	if (err < 0)
		return err;

	/*
	printf("    pic_init_qp_minus26: %d (%zu bits)\n", pps->pic_init_qp_minus26, len);
	*/

	err = bitstream_read_se(&bs, &pps->pic_init_qs_minus26, &len);
	if (err < 0)
		return err;

	/*
	printf("    pic_init_qs_minus26: %d (%zu bits)\n", pps->pic_init_qs_minus26, len);
	*/

	err = bitstream_read_se(&bs, &pps->chroma_qp_index_offset, &len);
	if (err < 0)
		return err;

	/*
	printf("    chroma_qp_index_offset: %d (%zu bits)\n", pps->chroma_qp_index_offset, len);
	*/

	err = bitstream_read_u8(&bs, &pps->deblocking_filter_control_present_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("    deblocking_filter_control_present_flag: %u\n", pps->deblocking_filter_control_present_flag);
	*/

	err = bitstream_read_u8(&bs, &pps->constrained_intra_pred_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("    constrained_intra_pred_flag: %u\n", pps->constrained_intra_pred_flag);
	*/

	err = bitstream_read_u8(&bs, &pps->redundant_pic_cnt_present_flag, 1);
	if (err < 0)
		return err;

	/*
	printf("    redundant_pic_cnt_present_flag: %u\n", pps->redundant_pic_cnt_present_flag);
	*/

	if (bitstream_more_rbsp_data(&bs)) {
		err = bitstream_read_u8(&bs, &pps->transform_8x8_mode_flag, 1);
		if (err < 0)
			return err;

		/*
		printf("    transform_8x8_mode_flag: %u\n", pps->transform_8x8_mode_flag);
		*/

		err = bitstream_read_u8(&bs, &pps->pic_scaling_matrix_present_flag, 1);
		if (err < 0)
			return err;

		/*
		printf("    pic_scaling_matrix_present_flag: %u\n", pps->pic_scaling_matrix_present_flag);
		*/

		if (pps->pic_scaling_matrix_present_flag)
			return -ENOTSUP;

		err = bitstream_read_se(&bs, &pps->second_chroma_qp_index_offset, &len);
		if (err < 0)
			return err;

		/*
		printf("    second_chroma_qp_index_offset: %u (%zu bits)\n", pps->second_chroma_qp_index_offset, len);
		*/
	}

	return 0;
}

static int h264_context_parse(struct h264_context *context, const void *data,
			      size_t size)
{
	const uint8_t *ptr = data;
	unsigned int i;

	printf("extra data: %zu bytes\n", size);

	if (ptr[0] == 1) {
		context->profile = ptr[1];
		context->compatibility = ptr[2];
		context->level = ptr[3];

		context->nal_size = (ptr[4] & 0x3) + 1;
		context->num_sps = ptr[5] & 0x1f;

		printf("profile: %u compatibility: %u level: %u\n", context->profile, context->compatibility, context->level);
		printf("NAL size: %u\n", context->nal_size);
		printf("SPS: %u\n", context->num_sps);

		context->sps = calloc(context->num_sps, sizeof(*context->sps));
		if (!context->sps)
			return -ENOMEM;

		ptr = data + 6;

		for (i = 0; i < context->num_sps; i++) {
			uint16_t length = (ptr[0] << 8) | (ptr[1] << 0);
			uint8_t ref_idc, unit_type;
			int err;

			ptr += 2;

			/*
			printf("  %u: %u bytes\n", i, length);
			hexdump(ptr, length, "    ", stdout);
			*/

			ref_idc = (ptr[0] >> 5) & 0x3;
			unit_type = ptr[0] & 0x1f;

			if (0) {
				printf("    NAL:\n");
				printf("      ref_idc: %u\n", ref_idc);
				printf("      type: %u\n", unit_type);
			}

			/* SPS */
			if (unit_type == 7) {
				err = h264_sps_parse(&context->sps[i], &ptr[1],
						     length - 1);
				if (err < 0)
					return err;
			} else {
				fprintf(stderr, "non-SPS NAL found\n");
			}

			ptr += length;
		}

		printf("ptr: %p (%lu)\n", ptr, (unsigned long)ptr - (unsigned long)data);
		context->num_pps = ptr[0];

		printf("PPS: %u\n", context->num_pps);

		context->pps = calloc(context->num_pps, sizeof(*context->pps));
		if (!context->pps)
			return -ENOMEM;

		ptr++;

		for (i = 0; i < context->num_pps; i++) {
			uint16_t length = (ptr[0] << 8) | (ptr[1] << 0);
			uint8_t ref_idc, unit_type;
			int err;

			ptr += 2;

			/*
			printf("  %u: %u bytes\n", i, length);
			hexdump(ptr, length, "    ", stdout);
			*/

			ref_idc = (ptr[0] >> 5) & 0x3;
			unit_type = ptr[0] & 0x1f;

			if (0) {
				printf("    NAL:\n");
				printf("      ref_idc: %u\n", ref_idc);
				printf("      type: %u\n", unit_type);
			}

			if (unit_type == 8) {
				err = h264_pps_parse(&context->pps[i], &ptr[1],
						     length - 1);
				if (err < 0)
					return err;
			} else {
				fprintf(stderr, "non-PPS NAL unit found\n");
			}

			ptr += length;
		}
	} else {
		/* XXX */
	}

	return 0;
}

struct drm_tegra {
	int fd;
};

int drm_tegra_open(struct drm_tegra **drmp, const char *path)
{
	struct drm_tegra *drm;
	int err;

	drm = calloc(1, sizeof(*drm));
	if (!drm)
		return -ENOMEM;

	drm->fd = open(path, O_RDWR);
	if (drm->fd < 0) {
		err = -errno;
		goto free;
	}

	*drmp = drm;

	return 0;

free:
	free(drm);
	return err;
}

void drm_tegra_close(struct drm_tegra *drm)
{
	if (drm)
		close(drm->fd);

	free(drm);
}

struct tegra_vde_buffer;

struct tegra_vde {
	struct drm_tegra *drm;
	int fd;

	struct tegra_vde_buffer *bitstream;
	struct tegra_vde_buffer *secure;
};

struct tegra_vde_buffer {
	struct tegra_vde *vde;
	uint32_t handle;
	size_t size;
	void *ptr;
	int fd;
};

int tegra_vde_buffer_create(struct tegra_vde_buffer **bufferp,
			    struct tegra_vde *vde,
			    size_t size)
{
	struct drm_tegra_gem_create args;
	struct tegra_vde_buffer *buffer;
	int err;

	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return -ENOMEM;

	buffer->size = size;
	buffer->vde = vde;
	buffer->fd = -1;

	memset(&args, 0, sizeof(args));
	args.size = size;
	args.flags = 0;

repeat:
	err = ioctl(vde->drm->fd, DRM_IOCTL_TEGRA_GEM_CREATE, &args);
	if (err < 0) {
		if (errno == EINTR || errno == EAGAIN)
			goto repeat;

		err = -errno;
		goto free;
	}

	buffer->handle = args.handle;

	*bufferp = buffer;

	return 0;

free:
	free(buffer);
	return err;
}

void tegra_vde_buffer_free(struct tegra_vde_buffer *buffer)
{
	struct drm_tegra *drm = buffer->vde->drm;
	struct drm_gem_close args;
	int err;

	if (buffer->fd >= 0)
		close(buffer->fd);

	memset(&args, 0, sizeof(args));
	args.handle = buffer->handle;

repeat:
	err = ioctl(drm->fd, DRM_IOCTL_GEM_CLOSE, &args);
	if (err < 0) {
		if (errno == EINTR || errno == EAGAIN)
			goto repeat;
	}

	free(buffer);
}

int tegra_vde_buffer_export(struct tegra_vde_buffer *buffer, int flags)
{
	struct drm_tegra *drm = buffer->vde->drm;
	struct drm_prime_handle args;
	int err;

	memset(&args, 0, sizeof(args));
	args.handle = buffer->handle;
	args.flags = flags;

repeat:
	err = ioctl(drm->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
	if (err < 0) {
		if (errno == EINTR || errno == EAGAIN)
			goto repeat;

		return -errno;
	}

	return args.fd;
}

int tegra_vde_buffer_map(struct tegra_vde_buffer *buffer, void **ptrp)
{
	struct drm_tegra *drm = buffer->vde->drm;

	if (!buffer->ptr) {
		struct drm_tegra_gem_mmap args;
		int err;

		memset(&args, 0, sizeof(args));
		args.handle = buffer->handle;

repeat:
		err = ioctl(drm->fd, DRM_IOCTL_TEGRA_GEM_MMAP, &args);
		if (err < 0) {
			if (errno == EINTR || errno == EAGAIN)
				goto repeat;

			return -errno;
		}

		buffer->ptr = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
				   MAP_SHARED, drm->fd, args.offset);
		if (buffer->ptr == MAP_FAILED) {
			buffer->ptr = NULL;
			return -errno;
		}
	}

	if (ptrp)
		*ptrp = buffer->ptr;

	return 0;
}

struct tegra_vde_frame {
	struct tegra_vde_buffer *buffer;

	size_t y_offset;
	size_t u_offset;
	size_t v_offset;

	unsigned int width;
	unsigned int height;
};

int tegra_vde_frame_create(struct tegra_vde_frame **framep,
			   struct tegra_vde *vde, unsigned int width,
			   unsigned int height)
{
	struct tegra_vde_frame *frame;
	size_t size;
	int err;

	frame = calloc(1, sizeof(*frame));
	if (!frame)
		return -ENOMEM;

	frame->width = width;
	frame->height = height;

	size = width * height * 2;

	err = tegra_vde_buffer_create(&frame->buffer, vde, size);
	if (err < 0)
		goto free;

	err = tegra_vde_buffer_export(frame->buffer, 0);
	if (err < 0)
		goto free;

	frame->buffer->fd = err;

	frame->y_offset = 0;
	frame->u_offset = width * height;
	frame->v_offset = width * height + (width * height) / 2;

	*framep = frame;

	return 0;

free:
	free(frame);
	return err;
}

void tegra_vde_frame_free(struct tegra_vde_frame *frame)
{
	if (frame)
		tegra_vde_buffer_free(frame->buffer);

	free(frame);
}

static int tegra_vde_open(struct tegra_vde **vdep, struct drm_tegra *drm)
{
	struct tegra_vde *vde;
	int err;

	vde = calloc(1, sizeof(*vde));
	if (!vde)
		return -ENOMEM;

	vde->drm = drm;

	vde->fd = open("/dev/tegra_vde", O_RDWR);
	if (vde->fd < 0) {
		err = -errno;
		goto free;
	}

	err = tegra_vde_buffer_create(&vde->bitstream, vde, 256 * 1024);
	if (err < 0) {
		fprintf(stderr, "failed to create bitstream buffer: %d\n", err);
		goto close;
	}

	err = tegra_vde_buffer_export(vde->bitstream, 0);
	if (err < 0) {
		fprintf(stderr, "failed to export bitstream buffer: %d\n", err);
		goto close;
	}

	vde->bitstream->fd = err;

	err = tegra_vde_buffer_create(&vde->secure, vde, 4 * 1024);
	if (err < 0) {
		fprintf(stderr, "failed to create secure buffer: %d\n", err);
		goto close;
	}

	err = tegra_vde_buffer_export(vde->secure, 0);
	if (err < 0) {
		fprintf(stderr, "failed to export secure buffer: %d\n", err);
		goto close;
	}

	vde->secure->fd = err;

	*vdep = vde;

	return 0;

close:
	close(vde->fd);
free:
	free(vde);
	return err;
}

static void tegra_vde_close(struct tegra_vde *vde)
{
	if (vde) {
		tegra_vde_buffer_free(vde->secure);
		tegra_vde_buffer_free(vde->bitstream);
		close(vde->fd);
	}

	free(vde);
}

static int tegra_vde_decode(struct tegra_vde *vde,
			    struct tegra_vde_frame **framep,
			    struct h264_context *ctx,
			    const void *data, size_t size)
{
	struct tegra_vde_h264_decoder_ctx args;
	struct h264_sps *sps = &ctx->sps[0];
	struct h264_pps *pps = &ctx->pps[0];
	struct tegra_vde_h264_frame f;
	struct tegra_vde_frame *frame;
	unsigned int width, height;
	void *ptr;
	int err;

	width = (sps->pic_width_in_mbs_minus1 + 1) * 16;
	height = (sps->pic_height_in_map_units_minus1 + 1) * 16;

	printf("picture: %ux%u\n", width, height);

	err = tegra_vde_buffer_map(vde->bitstream, &ptr);
	if (err < 0)
		return err;

	memcpy(ptr, data, size);

	hexdump(ptr, (size < 256) ? size : 256, NULL, stdout);

	//tegra_vde_buffer_unmap(vde->bitstream);

	err = tegra_vde_frame_create(&frame, vde, width, height);
	if (err < 0)
		return err;

	printf("buffer: %d\n", frame->buffer->fd);

	memset(&f, 0, sizeof(f));
	f.y_fd = frame->buffer->fd;
	f.cb_fd = frame->buffer->fd;
	f.cr_fd = frame->buffer->fd;
	f.aux_fd = -1;
	f.y_offset = frame->y_offset;
	f.cb_offset = frame->u_offset;
	f.cr_offset = frame->v_offset;
	f.aux_offset = 0;
	f.frame_num = 0;
	f.flags = FLAG_REFERENCE;
	f.modifier = 0x0300000000000014;

	memset(&args, 0, sizeof(args));
	args.bitstream_data_fd = vde->bitstream->fd;
	args.bitstream_data_offset = 0;
	args.secure_fd = vde->secure->fd;
	args.secure_offset = 0;
	args.dpb_frames_ptr = (uintptr_t)&f;
	args.dpb_frames_nb = 1;
	args.dpb_ref_frames_with_earlier_poc_nb = 0;

	/* SPS */
	args.baseline_profile = 1;
	args.level_idc = 11; //sps->level_idc;
	args.log2_max_pic_order_cnt_lsb = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
	args.log2_max_frame_num = sps->log2_max_frame_num_minus4 + 4;
	args.pic_order_cnt_type = sps->pic_order_cnt_type;
	args.direct_8x8_inference_flag = sps->direct_8x8_inference_flag;
	args.pic_width_in_mbs = width / 16;
	args.pic_height_in_mbs = height / 16;

	/* PPS */
	args.pic_init_qp = pps->pic_init_qp_minus26 + 26;
	args.deblocking_filter_control_present_flag = pps->deblocking_filter_control_present_flag;
	args.constrained_intra_pred_flag = pps->constrained_intra_pred_flag;
	args.chroma_qp_index_offset = pps->chroma_qp_index_offset & 0x1f;
	args.pic_order_present_flag = 0; //pps->pic_order_present_flag;

	/* slice header */
	args.num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
	args.num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;

repeat:
	err = ioctl(vde->fd, TEGRA_VDE_IOCTL_DECODE_H264, &args);
	if (err < 0) {
		if (errno == EINTR || errno == EAGAIN)
			goto repeat;

		return -errno;
	}

	*framep = frame;

	return 0;
}

int main(int argc, char *argv[])
{
	struct tegra_vde_frame *vf = NULL;
	struct tegra_vde *vde = NULL;
	AVFormatContext *fmt = NULL;
	struct h264_context ctx;
	struct drm_tegra *drm;
	AVCodecContext *codec;
	const char *filename;
	AVCodec *decoder;
	AVStream *video;
	AVFrame *frame;
	AVPacket pkt;
	int err;

	if (argc < 2) {
		usage(argv[0], stderr);
		return 1;
	}

	filename = argv[1];

	err = avformat_open_input(&fmt, filename, NULL, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to open '%s': %d\n", filename, err);
		return 1;
	}

	err = avformat_find_stream_info(fmt, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to find stream info: %d\n", err);
		return 1;
	}

	av_dump_format(fmt, 0, filename, 0);

	err = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (err < 0) {
		fprintf(stderr, "failed to find video stream: %d\n", err);
		return 1;
	}

	video = fmt->streams[err];

	decoder = avcodec_find_decoder(video->codecpar->codec_id);
	if (!decoder) {
		fprintf(stderr, "failed to find decoder\n");
		return 1;
	}

	codec = avcodec_alloc_context3(decoder);
	if (!codec) {
		fprintf(stderr, "failed to allocate codec\n");
		return 1;
	}

	printf("extra data: %d bytes\n", video->codecpar->extradata_size);

	hexdump(video->codecpar->extradata, video->codecpar->extradata_size,
		NULL, stdout);

	memset(&ctx, 0, sizeof(ctx));

	err = h264_context_parse(&ctx, video->codecpar->extradata, video->codecpar->extradata_size);
	if (err < 0) {
		fprintf(stderr, "failed to parse H264 context: %d\n", err);
		return 1;
	}

	err = drm_tegra_open(&drm, "/dev/dri/card0");
	if (err < 0) {
		fprintf(stderr, "failed to open Tegra DRM: %d\n", err);
		return 1;
	}

	err = tegra_vde_open(&vde, drm);
	if (err < 0) {
		fprintf(stderr, "failed to open VDE: %d\n", err);
		return 1;
	}

	err = avcodec_parameters_to_context(codec, video->codecpar);
	if (err < 0) {
		fprintf(stderr, "failed to copy codec parameters: %d\n", err);
		return 1;
	}

	err = avcodec_open2(codec, decoder, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to open codec: %d\n", err);
		return 1;
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "failed to allocate frame\n");
		return 1;
	}

	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;

	while (av_read_frame(fmt, &pkt) >= 0) {
		if (pkt.stream_index == video->index) {
			unsigned int i;

			if (0)
				hexdump(pkt.data, pkt.size, NULL, stdout);

			err = tegra_vde_decode(vde, &vf, &ctx, pkt.data, pkt.size);
			if (err < 0) {
				fprintf(stderr, "failed to decode frame: %d\n",
					err);
				return 1;
			}

			tegra_vde_frame_free(vf);

			err = avcodec_send_packet(codec, &pkt);
			if (err < 0) {
				fprintf(stderr, "failed to decode frame: %d\n",
					err);
				return 1;
			}

			err = avcodec_receive_frame(codec, frame);
			if (err < 0) {
				fprintf(stderr, "failed to receive frame: %d\n", err);
				return 1;
			}

			printf("frame decoded:\n");
			printf("  resolution: %dx%d\n", frame->width, frame->height);
			printf("  samples: %d\n", frame->nb_samples);
			printf("  format: %d\n", frame->format);
			printf("  key frame: %s\n", frame->key_frame ? "yes" : "no");
			printf("  channels: %d\n", frame->channels);
			printf("  crop: top %zu bottom %zu left %zu right %zu\n",
			       frame->crop_top, frame->crop_bottom,
			       frame->crop_left, frame->crop_right);
			printf("  data:\n");

			for (i = 0; i < AV_NUM_DATA_POINTERS; i++)
				printf("   %u: %p (%d bytes)\n", i, frame->data[i], frame->linesize[i]);

			if (0) {
				FILE *fp = fopen("packet.h264", "wb");
				if (!fp)
					return 1;

				fwrite(pkt.data, 1, pkt.size, fp);

				fclose(fp);
			}
		}

		av_packet_unref(&pkt);
	}

	tegra_vde_close(vde);
	drm_tegra_close(drm);

	avcodec_close(codec);
	avformat_close_input(&fmt);

	return 0;
}
