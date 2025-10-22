#ifndef WRAPPER_BC_H
#define WRAPPER_BC_H

#include "vulkan/vulkan.h"
#include <stdbool.h>

/* BC texture compression emulation header */

struct wrapper_device;

/* BC format information structure */
struct bc_format_info {
   VkFormat format;
   uint32_t block_size;           /* Size of compressed block in bytes */
   uint32_t block_width;          /* Block width in pixels (typically 4) */
   uint32_t block_height;         /* Block height in pixels (typically 4) */
   VkFormat decompressed_format;  /* Target format for decompression */
   const char* name;              /* Format name for debugging */
};

/* Check if a format is a BC format that we can emulate */
bool wrapper_is_bc_format(VkFormat format);

/* Get BC format information */
const struct bc_format_info* wrapper_get_bc_format_info(VkFormat format);

/* Calculate compressed data size for BC format */
VkDeviceSize wrapper_bc_get_compressed_size(VkFormat format, uint32_t width, uint32_t height);

/* Decompress BC texture data */
VkResult wrapper_bc_decompress_image(struct wrapper_device *device,
                                   VkFormat src_format,
                                   uint32_t width,
                                   uint32_t height,
                                   const void *compressed_data,
                                   VkDeviceSize compressed_size,
                                   void *decompressed_data,
                                   VkDeviceSize decompressed_size);

/* Initialize BC emulation for device */
VkResult wrapper_bc_device_init(struct wrapper_device *device);

/* Cleanup BC emulation for device */
void wrapper_bc_device_finish(struct wrapper_device *device);

#endif /* WRAPPER_BC_H */
