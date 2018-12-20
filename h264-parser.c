#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitstream.h"
#include "h264-parser.h"

int h264_sps_parse(struct h264_sps *sps, const void *data, size_t size)
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

	printf("      profile: %u\n", sps->profile_idc);
	printf("      flags: %02x\n", sps->flags);
	printf("      level: %u\n", sps->level_idc);

	err = bitstream_read_ue(&bs, &sps->seq_parameter_set_id, &len);
	if (err < 0)
		return err;

	printf("      ID: %u (%zu bits)\n", sps->seq_parameter_set_id, len);

	/* currently only supports baseline */
	if (sps->profile_idc != 66) {
		return -ENOTSUP;
	}

	err = bitstream_read_ue(&bs, &sps->log2_max_frame_num_minus4, &len);
	if (err < 0)
		return err;

	printf("      max frames: %u/%u (%zu bits)\n", sps->log2_max_frame_num_minus4, 2 << (sps->log2_max_frame_num_minus4 + 4), len);

	err = bitstream_read_ue(&bs, &sps->pic_order_cnt_type, &len);
	if (err < 0)
		return err;

	printf("      pic_order_cnt_type: %u (%zu bits)\n", sps->pic_order_cnt_type, len);

	if (sps->pic_order_cnt_type == 0) {
		return -ENOTSUP;
	}

	if (sps->pic_order_cnt_type == 1) {
		return -ENOTSUP;
	}

	err = bitstream_read_ue(&bs, &sps->max_num_ref_frames, &len);
	if (err < 0)
		return err;

	printf("      max_num_ref_frames: %u (%zu bits)\n", sps->max_num_ref_frames, len);

	err = bitstream_read_u8(&bs, &sps->gaps_in_frame_num_value_allowed_flag, 1);
	if (err < 0)
		return err;

	printf("      gaps_in_frame_num_value_allowed_flag: %u\n", sps->gaps_in_frame_num_value_allowed_flag);

	err = bitstream_read_ue(&bs, &sps->pic_width_in_mbs_minus1, &len);
	if (err < 0)
		return err;

	printf("      pic_width_in_mbs_minus1: %u (%zu bits)\n", sps->pic_width_in_mbs_minus1, len);

	err = bitstream_read_ue(&bs, &sps->pic_height_in_map_units_minus1, &len);
	if (err < 0)
		return err;

	printf("      pic_height_in_map_units_minus1: %u (%zu bits)\n", sps->pic_height_in_map_units_minus1, len);

	err = bitstream_read_u8(&bs, &sps->frame_mbs_only_flag, 1);
	if (err < 0)
		return err;

	printf("      frame_mbs_only_flag: %u\n", sps->frame_mbs_only_flag);

	if (!sps->frame_mbs_only_flag) {
		err = bitstream_read_u8(&bs, &sps->mb_adaptive_frame_field_flag, 1);
		if (err < 0)
			return err;

		printf("        mb_adaptive_frame_field_flag: %u\n", sps->mb_adaptive_frame_field_flag);
	}

	err = bitstream_read_u8(&bs, &sps->direct_8x8_inference_flag, 1);
	if (err < 0)
		return err;

	printf("      direct_8x8_inference_flag: %u\n", sps->direct_8x8_inference_flag);

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

		printf("        left: %u right: %u top: %u bottom: %u\n", sps->frame_crop_left_offset, sps->frame_crop_right_offset, sps->frame_crop_top_offset, sps->frame_crop_bottom_offset);
	}

	err = bitstream_read_u8(&bs, &sps->vui_parameters_present_flag, 1);
	if (err < 0)
		return err;

	printf("      vui_parameters_present_flag: %u\n", sps->vui_parameters_present_flag);

	if (sps->vui_parameters_present_flag) {
		err = bitstream_read_u8(&bs, &sps->vui_parameters.aspect_ratio_info_present_flag, 1);
		if (err < 0)
			return err;

		printf("        aspect_ratio_info_present_flag: %u\n", sps->vui_parameters.aspect_ratio_info_present_flag);

		if (sps->vui_parameters.aspect_ratio_info_present_flag) {
			err = bitstream_read_u8(&bs, &sps->vui_parameters.aspect_ratio_idc, 8);
			if (err < 0)
				return err;

			printf("          aspect_ratio_idc: %u\n", sps->vui_parameters.aspect_ratio_idc);

			if (sps->vui_parameters.aspect_ratio_idc == 255) {
				err = bitstream_read_u16(&bs, &sps->vui_parameters.sar_width, 16);
				if (err < 0)
					return err;

				err = bitstream_read_u16(&bs, &sps->vui_parameters.sar_height, 16);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

int h264_pps_parse(struct h264_pps *pps, const void *data, size_t size)
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

int h264_context_parse(struct h264_context *context, const void *data,
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
				if (err < 0) {
					fprintf(stderr, "failed to parse SPS: %d\n", err);
					return err;
				}
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
				if (err < 0) {
					fprintf(stderr, "failed to parse PPS: %d\n", err);
					return err;
				}
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
