#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>

#include <libdrm/tegra.h>
#include <drm_fourcc.h>

#include "drm-utils.h"
#include "h264-parser.h"
#include "image.h"
#include "utils.h"

#include "tegra-vde.h"

static void usage(const char *program, FILE *fp)
{
	fprintf(fp, "usage: %s FILENAME\n", program);
}

struct tegra_vde {
	struct drm_tegra *drm;
	int fd;

	struct drm_tegra_bo *bitstream;
	int bitstream_fd;

	struct drm_tegra_bo *secure;
	int secure_fd;
};

struct tegra_vde_frame {
	struct drm_tegra_bo *buffer;
	int fd;

	unsigned int width;
	unsigned int height;
	uint32_t format;
	uint64_t modifier;

	unsigned int pitch;
	size_t offsets[3];
	size_t size;
};

int tegra_get_block_height(uint64_t modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0):
		return 1;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1):
		return 2;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2):
		return 4;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3):
		return 8;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4):
		return 16;

	case DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5):
		return 32;
	}

	return -EINVAL;
}

int tegra_vde_frame_create(struct tegra_vde_frame **framep,
			   struct tegra_vde *vde, unsigned int width,
			   unsigned int height, uint32_t format,
			   uint64_t modifier)
{
	const struct drm_format_info *info;
	struct tegra_vde_frame *frame;
	unsigned int block_height;
	unsigned int i;
	size_t size;
	void *ptr;
	int err;

	info = drm_format_get_info(format);
	if (!info)
		return -EINVAL;

	frame = calloc(1, sizeof(*frame));
	if (!frame)
		return -ENOMEM;

	err = tegra_get_block_height(modifier);
	if (err < 0)
		return err;

	block_height = err;

	frame->width = width;
	frame->height = height;
	frame->format = format;
	frame->modifier = modifier;

	/* blocks are 64 bytes wide, assuming block-linear */
	frame->pitch = ALIGN(width * info->cpp[0], 64);

	frame->offsets[0] = 0;

	size = frame->pitch * ALIGN(height, 8 * block_height);

	for (i = 1; i < info->num_planes; i++) {
		unsigned int pitch = width * info->cpp[i] / info->hsub;

		frame->offsets[i] = size;

		size += pitch * ALIGN(height / info->vsub, 8 * block_height);
	}

	err = drm_tegra_bo_new(&frame->buffer, vde->drm, 0, size);
	if (err < 0)
		goto free;

	frame->size = size;

	err = drm_tegra_bo_map(frame->buffer, &ptr);
	if (err < 0)
		goto free;

	memset(ptr, 0xaa, size);

	drm_tegra_bo_unmap(frame->buffer);

	err = drm_tegra_bo_export(frame->buffer, 0);
	if (err < 0)
		goto free;

	frame->fd = err;

	*framep = frame;

	return 0;

free:
	free(frame);
	return err;
}

int tegra_vde_frame_detile(struct tegra_vde_frame *frame,
			   struct image **imagep)
{
	unsigned int stride, i, j, k, block_height, gobs;
	const struct drm_format_info *info;
	struct image *image;
	void *ptr;
	int err;

	info = drm_format_get_info(frame->format);
	if (!info)
		return -EINVAL;

	err = tegra_get_block_height(frame->modifier);
	if (err < 0)
		return err;

	block_height = err;

	err = drm_tegra_bo_map(frame->buffer, &ptr);
	if (err < 0)
		return err;

	err = image_create(&image, frame->width, frame->height, frame->format);
	if (err < 0) {
		drm_tegra_bo_unmap(frame->buffer);
		return err;
	}

	for (k = 0; k < info->num_planes; k++) {
		unsigned int width = image->width;
		unsigned int height = image->height;
		unsigned int pitch;

		gobs = DIV_ROUND_UP(frame->pitch, 64);

		if (k > 0) {
			width /= info->hsub;
			height /= info->vsub;
			gobs /= info->hsub;
		}

		pitch = width * info->cpp[k];
		stride = 16 / info->cpp[k];

		for (j = 0; j < height; j++) {
			void *dst = image->data + image->offsets[k] + pitch * j;
			unsigned int y = j;

			for (i = 0; i < width; i += stride) {
				unsigned int x = i * info->cpp[k];
				unsigned int base = (y / (8 * block_height)) * 512 * block_height * gobs +
						    (x / 64) * 512 * block_height +
						    (y % (8 * block_height) / 8) * 512;
				unsigned int offset = ((x % 64) / 32) * 256 +
						      ((y %  8) /  2) *  64 +
						      ((x % 32) / 16) *  32 +
						      ((y %  2) * 16) + (x % 16);
				void *src = ptr + frame->offsets[k] + base + offset;

				memcpy(dst + x, src, 16);
			}
		}
	}

	if (imagep)
		*imagep = image;

	drm_tegra_bo_unmap(frame->buffer);

	return 0;
}

void tegra_vde_frame_dump(struct tegra_vde_frame *frame, FILE *fp)
{
	const struct drm_format_info *info;
	struct image *image;
	unsigned int i, j;
	uint32_t handle;
	void *ptr;
	int err;

	info = drm_format_get_info(frame->format);
	if (!info) {
		fprintf(stderr, "invalid format %08x\n", frame->format);
		return;
	}

	err = drm_tegra_bo_get_handle(frame->buffer, &handle);
	if (err < 0) {
		fprintf(stderr, "failed to get buffer object handle: %d\n", err);
		return;
	}

	err = drm_tegra_bo_map(frame->buffer, &ptr);
	if (err < 0) {
		fprintf(stderr, "failed to map frame buffer: %d\n", err);
		return;
	}

	fprintf(fp, "frame: %ux%u\n", frame->width, frame->height);
	fprintf(fp, "  buffer: %p\n", frame->buffer);
	fprintf(fp, "    handle: %u\n", handle);
	fprintf(fp, "    size: %zu\n", frame->size);
	fprintf(fp, "    ptr: %p\n", ptr);
	fprintf(fp, "    fd: %d\n", frame->fd);

	for (i = 0; i < info->num_planes; i++) {
		unsigned int width = frame->width;
		unsigned int height = frame->height;
		unsigned int stride, pitch;

		if (i > 0) {
			width /= info->hsub;
			height /= info->vsub;
		}

		stride = width * info->cpp[i];
		pitch = ALIGN(stride, 64);

		fprintf(fp, "  %u: %zx\n", i, frame->offsets[i]);

		for (j = 0; j < height; j++) {
			unsigned int offset = j * pitch;

			hexdump(ptr + frame->offsets[i] + offset,
				stride, stride, "    ", fp);
		}
	}

	drm_tegra_bo_unmap(frame->buffer);

	err = tegra_vde_frame_detile(frame, &image);
	if (err < 0) {
		fprintf(stderr, "failed to detile frame: %d\n", err);
		return;
	}

	image_dump(image, fp);
	image_free(image);
}

void tegra_vde_frame_free(struct tegra_vde_frame *frame)
{
	if (frame)
		drm_tegra_bo_unref(frame->buffer);

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

	err = drm_tegra_bo_new(&vde->bitstream, vde->drm, 0, 256 * 1024);
	if (err < 0) {
		fprintf(stderr, "failed to create bitstream buffer: %d\n", err);
		goto close;
	}

	err = drm_tegra_bo_export(vde->bitstream, 0);
	if (err < 0) {
		fprintf(stderr, "failed to export bitstream buffer: %d\n", err);
		goto unref_bitstream;
	}

	vde->bitstream_fd = err;

	err = drm_tegra_bo_new(&vde->secure, vde->drm, 0, 4 * 1024);
	if (err < 0) {
		fprintf(stderr, "failed to create secure buffer: %d\n", err);
		goto close_bitstream;
	}

	err = drm_tegra_bo_export(vde->secure, 0);
	if (err < 0) {
		fprintf(stderr, "failed to export secure buffer: %d\n", err);
		goto unref_secure;
	}

	vde->secure_fd = err;

	*vdep = vde;

	return 0;

unref_secure:
	drm_tegra_bo_unref(vde->secure);
close_bitstream:
	close(vde->bitstream_fd);
unref_bitstream:
	drm_tegra_bo_unref(vde->bitstream);
close:
	close(vde->fd);
free:
	free(vde);
	return err;
}

static void tegra_vde_close(struct tegra_vde *vde)
{
	if (vde) {
		drm_tegra_bo_unref(vde->secure);
		close(vde->secure_fd);

		drm_tegra_bo_unref(vde->bitstream);
		close(vde->bitstream_fd);

		close(vde->fd);
	}

	free(vde);
}

static int tegra_vde_decode(struct tegra_vde *vde,
			    struct tegra_vde_frame **framep,
			    struct h264_context *ctx,
			    const void *data, size_t size)
{
	uint64_t modifier = DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4);
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

	err = drm_tegra_bo_map(vde->bitstream, &ptr);
	if (err < 0)
		return err;

	memcpy(ptr, data, size);

	hexdump(ptr, (size < 256) ? size : 256, 16, NULL, stdout);

	drm_tegra_bo_unmap(vde->bitstream);

	err = tegra_vde_frame_create(&frame, vde, width, height,
				     DRM_FORMAT_YUV420, modifier);
	if (err < 0)
		return err;

	printf("buffer: %d\n", frame->fd);

	memset(&f, 0, sizeof(f));
	f.y_fd = frame->fd;
	f.cb_fd = frame->fd;
	f.cr_fd = frame->fd;
	f.aux_fd = -1;
	f.y_offset = frame->offsets[0];
	f.cb_offset = frame->offsets[1];
	f.cr_offset = frame->offsets[2];
	f.aux_offset = 0;
	f.frame_num = 0;
	f.flags = FLAG_REFERENCE;
	f.modifier = modifier;

	memset(&args, 0, sizeof(args));
	args.bitstream_data_fd = vde->bitstream_fd;
	args.bitstream_data_offset = 0;
	args.secure_fd = vde->secure_fd;
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

void av_frame_dump(AVFrame *frame, FILE *fp)
{
	const AVPixFmtDescriptor *desc;
	unsigned int i, j;

	desc = av_pix_fmt_desc_get(frame->format);
	if (!desc) {
		fprintf(stderr, "invalid pixel format\n");
		return;
	}

	fprintf(fp, "frame decoded:\n");
	fprintf(fp, "  resolution: %dx%d\n", frame->width, frame->height);
	fprintf(fp, "  samples: %d\n", frame->nb_samples);
	fprintf(fp, "  format: %d\n", frame->format);
	fprintf(fp, "  key frame: %s\n", frame->key_frame ? "yes" : "no");
	fprintf(fp, "  channels: %d\n", frame->channels);
	fprintf(fp, "  crop: top %zu bottom %zu left %zu right %zu\n",
	        frame->crop_top, frame->crop_bottom,
	        frame->crop_left, frame->crop_right);
	fprintf(fp, "  components: %d\n", desc->nb_components);
	fprintf(fp, "  data:\n");

	for (i = 0; i < av_pix_fmt_count_planes(frame->format); i++) {
		int width = frame->width;
		int height = frame->width;
		unsigned int pitch;

		if (i > 0) {
			width = AV_CEIL_RSHIFT(width, desc->log2_chroma_w);
			height = AV_CEIL_RSHIFT(height, desc->log2_chroma_h);
		}

		pitch = width * desc->comp[i].depth / 8;

		fprintf(fp, "    %u: %ux%u (%u bytes)\n", i, width, height,
			pitch);

		for (j = 0; j < height; j++) {
			unsigned int offset = j * pitch;

			hexdump(frame->data[i] + offset, pitch, pitch,
				"      ", stdout);
		}
	}
}

int main(int argc, char *argv[])
{
	struct tegra_vde_frame *vf = NULL;
	const AVBitStreamFilter *bsf;
	struct tegra_vde *vde = NULL;
	AVFormatContext *fmt = NULL;
	struct h264_context ctx;
	struct drm_tegra *drm;
	AVCodecContext *codec;
	const char *filename;
	AVBSFContext *bsfc;
	AVCodec *decoder;
	AVStream *video;
	AVFrame *frame;
	AVPacket pkt;
	int err, fd;

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

	bsf = av_bsf_get_by_name("h264_mp4toannexb");
	if (!bsf) {
		fprintf(stderr, "failed to find mp4toannexb filter\n");
		return 1;
	}

	err = av_bsf_alloc(bsf, &bsfc);
	if (err < 0) {
		fprintf(stderr, "failed to allocate bitstream filter\n");
		return 1;
	}

	err = avcodec_parameters_copy(bsfc->par_in, video->codecpar);
	if (err < 0) {
		fprintf(stderr, "failed to copy codec paremeters\n");
		return 1;
	}

	err = av_bsf_init(bsfc);
	if (err < 0) {
		fprintf(stderr, "failed to initialize bitstream filter\n");
		return 1;
	}

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
		16, NULL, stdout);

	memset(&ctx, 0, sizeof(ctx));

	err = h264_context_parse(&ctx, video->codecpar->extradata, video->codecpar->extradata_size);
	if (err < 0) {
		fprintf(stderr, "failed to parse H264 context: %d\n", err);
		return 1;
	}

	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "failed to open Tegra DRM: %d\n", -errno);
		return 1;
	}

	err = drm_tegra_new(&drm, fd);
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
			AVPacket raw;

			if (0) {
				fprintf(stdout, "MP4 data:\n");
				hexdump(pkt.data, pkt.size, 16, NULL, stdout);
			}

			av_packet_ref(&raw, &pkt);

			err = av_bsf_send_packet(bsfc, &raw);
			if (err < 0) {
				fprintf(stderr, "failed to send packet to bitstream filter\n");
				return 1;
			}

			av_packet_unref(&raw);

			err = av_bsf_receive_packet(bsfc, &raw);
			if (err < 0) {
				fprintf(stderr, "failed to receive packet from bitstream filter\n");
				return 1;
			}

			if (0) {
				fprintf(stdout, "raw H.264 data:\n");
				hexdump(raw.data, raw.size, 16, NULL, stdout);
			}

			err = tegra_vde_decode(vde, &vf, &ctx, raw.data, raw.size);
			if (err < 0) {
				fprintf(stderr, "failed to decode frame: %d\n",
					err);
				return 1;
			}

			printf("frame decoded\n");

			tegra_vde_frame_dump(vf, stdout);
			tegra_vde_frame_free(vf);
			av_packet_unref(&raw);

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

			av_frame_dump(frame, stdout);

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
	close(fd);

	av_bsf_free(&bsfc);
	avcodec_close(codec);
	avformat_close_input(&fmt);

	return 0;
}
