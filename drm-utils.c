#include <stddef.h>

#include <drm_fourcc.h>

#include "drm-utils.h"
#include "utils.h"

static const struct drm_format_info formats[] = {
	{
		.format = DRM_FORMAT_YUV420,
		.num_planes = 3,
		.cpp = { 1, 1, 1 },
		.hsub = 2,
		.vsub = 2,
	},
};

const struct drm_format_info *drm_format_get_info(uint32_t format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].format == format)
			return &formats[i];

	return NULL;
}
