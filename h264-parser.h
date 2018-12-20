#ifndef H264_PARSER_H
#define H264_PARSER_H

#include <stddef.h>
#include <stdint.h>

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

int h264_sps_parse(struct h264_sps *sps, const void *data, size_t size);
int h264_pps_parse(struct h264_pps *pps, const void *data, size_t size);
int h264_context_parse(struct h264_context *context, const void *data,
		       size_t size);

#endif
