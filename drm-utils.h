#ifndef DRM_UTILS_H
#define DRM_UTILS_H

#include <stdint.h>

struct drm_format_info {
	uint32_t format;
	unsigned int num_planes;
	unsigned int cpp[3];
	unsigned int hsub;
	unsigned int vsub;
};

const struct drm_format_info *drm_format_get_info(uint32_t format);

#endif
