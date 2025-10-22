#include "wrapper_bc.h"
#include "wrapper_private.h"

/* Define BCDEC_BC4BC5_PRECISE for signed/unsigned BC4/BC5 support */
#define BCDEC_BC4BC5_PRECISE
/* Define BCDEC_IMPLEMENTATION to include implementation */
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

#include "vk_log.h"
#include "util/macros.h"
#include "util/hash_table.h"

#include <string.h>
#include <assert.h>

/* BC format lookup table */
static const struct bc_format_info bc_formats[] = {
   {
      .format = VK_FORMAT_BC1_RGB_UNORM_BLOCK,
      .block_size = BCDEC_BC1_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_UNORM,
      .name = "BC1_RGB_UNORM"
   },
   {
      .format = VK_FORMAT_BC1_RGB_SRGB_BLOCK,
      .block_size = BCDEC_BC1_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_SRGB,
      .name = "BC1_RGB_SRGB"
   },
   {
      .format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
      .block_size = BCDEC_BC1_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_UNORM,
      .name = "BC1_RGBA_UNORM"
   },
   {
      .format = VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
      .block_size = BCDEC_BC1_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_SRGB,
      .name = "BC1_RGBA_SRGB"
   },
   {
      .format = VK_FORMAT_BC2_UNORM_BLOCK,
      .block_size = BCDEC_BC2_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_UNORM,
      .name = "BC2_UNORM"
   },
   {
      .format = VK_FORMAT_BC2_SRGB_BLOCK,
      .block_size = BCDEC_BC2_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_SRGB,
      .name = "BC2_SRGB"
   },
   {
      .format = VK_FORMAT_BC3_UNORM_BLOCK,
      .block_size = BCDEC_BC3_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_UNORM,
      .name = "BC3_UNORM"
   },
   {
      .format = VK_FORMAT_BC3_SRGB_BLOCK,
      .block_size = BCDEC_BC3_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8B8A8_SRGB,
      .name = "BC3_SRGB"
   },
   {
      .format = VK_FORMAT_BC4_UNORM_BLOCK,
      .block_size = BCDEC_BC4_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8_UNORM,
      .name = "BC4_UNORM"
   },
   {
      .format = VK_FORMAT_BC4_SNORM_BLOCK,
      .block_size = BCDEC_BC4_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8_SNORM,
      .name = "BC4_SNORM"
   },
   {
      .format = VK_FORMAT_BC5_UNORM_BLOCK,
      .block_size = BCDEC_BC5_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8_UNORM,
      .name = "BC5_UNORM"
   },
   {
      .format = VK_FORMAT_BC5_SNORM_BLOCK,
      .block_size = BCDEC_BC5_BLOCK_SIZE,
      .block_width = 4,
      .block_height = 4,
      .decompressed_format = VK_FORMAT_R8G8_SNORM,
      .name = "BC5_SNORM"
   },
};

static const uint32_t bc_format_count = ARRAY_SIZE(bc_formats);

bool wrapper_is_bc_format(VkFormat format)
{
   for (uint32_t i = 0; i < bc_format_count; i++) {
      if (bc_formats[i].format == format)
         return true;
   }
   return false;
}

const struct bc_format_info* wrapper_get_bc_format_info(VkFormat format)
{
   for (uint32_t i = 0; i < bc_format_count; i++) {
      if (bc_formats[i].format == format)
         return &bc_formats[i];
   }
   return NULL;
}

VkDeviceSize wrapper_bc_get_compressed_size(VkFormat format, uint32_t width, uint32_t height)
{
   const struct bc_format_info *info = wrapper_get_bc_format_info(format);
   if (!info)
      return 0;

   /* Calculate number of blocks */
   uint32_t blocks_x = (width + info->block_width - 1) / info->block_width;
   uint32_t blocks_y = (height + info->block_height - 1) / info->block_height;
   
   return blocks_x * blocks_y * info->block_size;
}

static VkResult 
wrapper_bc1_decompress_block(const void *compressed_block, 
                            void *decompressed_block,
                            uint32_t block_pitch)
{
   /* Use bcdec to decompress BC1 block to RGBA8 */
   bcdec_bc1(compressed_block, decompressed_block, block_pitch);
   return VK_SUCCESS;
}

static VkResult 
wrapper_bc2_decompress_block(const void *compressed_block, 
                            void *decompressed_block,
                            uint32_t block_pitch)
{
   /* Use bcdec to decompress BC2 block to RGBA8 */
   bcdec_bc2(compressed_block, decompressed_block, block_pitch);
   return VK_SUCCESS;
}

static VkResult 
wrapper_bc3_decompress_block(const void *compressed_block, 
                            void *decompressed_block,
                            uint32_t block_pitch)
{
   /* Use bcdec to decompress BC3 block to RGBA8 */
   bcdec_bc3(compressed_block, decompressed_block, block_pitch);
   return VK_SUCCESS;
}

static VkResult 
wrapper_bc4_decompress_block(const void *compressed_block, 
                            void *decompressed_block,
                            uint32_t block_pitch,
                            bool is_signed)
{
   /* Use bcdec to decompress BC4 block to R8 */
   bcdec_bc4(compressed_block, decompressed_block, block_pitch, is_signed ? 1 : 0);
   return VK_SUCCESS;
}

static VkResult 
wrapper_bc5_decompress_block(const void *compressed_block, 
                            void *decompressed_block,
                            uint32_t block_pitch,
                            bool is_signed)
{
   /* Use bcdec to decompress BC5 block to RG8 */
   bcdec_bc5(compressed_block, decompressed_block, block_pitch, is_signed ? 1 : 0);
   return VK_SUCCESS;
}

VkResult wrapper_bc_decompress_image(struct wrapper_device *device,
                                   VkFormat src_format,
                                   uint32_t width,
                                   uint32_t height,
                                   const void *compressed_data,
                                   VkDeviceSize compressed_size,
                                   void *decompressed_data,
                                   VkDeviceSize decompressed_size)
{
   const struct bc_format_info *info = wrapper_get_bc_format_info(src_format);
   if (!info) {
      vk_loge(VK_LOG_OBJS(&device->vk.base), 
              "Unsupported BC format for emulation: %d", src_format);
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   /* Calculate block dimensions */
   uint32_t blocks_x = (width + info->block_width - 1) / info->block_width;
   uint32_t blocks_y = (height + info->block_height - 1) / info->block_height;
   
   /* Verify size consistency */
   VkDeviceSize expected_compressed_size = blocks_x * blocks_y * info->block_size;
   if (compressed_size < expected_compressed_size) {
      vk_loge(VK_LOG_OBJS(&device->vk.base),
              "BC decompression: insufficient compressed data size");
      return VK_ERROR_UNKNOWN;
   }

   /* Support BC1, BC2, BC3, BC4, and BC5 formats */
   if (src_format != VK_FORMAT_BC1_RGB_UNORM_BLOCK &&
       src_format != VK_FORMAT_BC1_RGB_SRGB_BLOCK &&
       src_format != VK_FORMAT_BC1_RGBA_UNORM_BLOCK &&
       src_format != VK_FORMAT_BC1_RGBA_SRGB_BLOCK &&
       src_format != VK_FORMAT_BC2_UNORM_BLOCK &&
       src_format != VK_FORMAT_BC2_SRGB_BLOCK &&
       src_format != VK_FORMAT_BC3_UNORM_BLOCK &&
       src_format != VK_FORMAT_BC3_SRGB_BLOCK &&
       src_format != VK_FORMAT_BC4_UNORM_BLOCK &&
       src_format != VK_FORMAT_BC4_SNORM_BLOCK &&
       src_format != VK_FORMAT_BC5_UNORM_BLOCK &&
       src_format != VK_FORMAT_BC5_SNORM_BLOCK) {
      vk_loge(VK_LOG_OBJS(&device->vk.base),
              "BC format %s not yet implemented", info->name);
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   /* Decompress each 4x4 block */
   const uint8_t *src_blocks = (const uint8_t *)compressed_data;
   uint8_t *dst_image = (uint8_t *)decompressed_data;
   
   /* Calculate destination pitch based on format */
   uint32_t dst_pitch;
   bool is_bc4_format = (src_format == VK_FORMAT_BC4_UNORM_BLOCK || 
                        src_format == VK_FORMAT_BC4_SNORM_BLOCK);
   bool is_bc5_format = (src_format == VK_FORMAT_BC5_UNORM_BLOCK || 
                        src_format == VK_FORMAT_BC5_SNORM_BLOCK);
   
   if (is_bc4_format) {
      dst_pitch = width * 1; /* BC4 -> R8: 1 byte per pixel */
   } else if (is_bc5_format) {
      dst_pitch = width * 2; /* BC5 -> RG8: 2 bytes per pixel */
   } else {
      dst_pitch = width * 4; /* BC1/BC2/BC3 -> RGBA8: 4 bytes per pixel */
   }
   
   for (uint32_t block_y = 0; block_y < blocks_y; block_y++) {
      for (uint32_t block_x = 0; block_x < blocks_x; block_x++) {
         /* Calculate source block pointer */
         const uint8_t *src_block = src_blocks + 
            (block_y * blocks_x + block_x) * info->block_size;
         
         /* Calculate destination block pointer */
         uint8_t *dst_block = dst_image + 
            (block_y * info->block_height * dst_pitch) + 
            (block_x * info->block_width * (is_bc4_format ? 1 : (is_bc5_format ? 2 : 4)));

         /* Decompress this block based on format */
         VkResult result;
         switch (src_format) {
         case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
         case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
         case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
         case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            result = wrapper_bc1_decompress_block(src_block, dst_block, dst_pitch);
            break;
         case VK_FORMAT_BC2_UNORM_BLOCK:
         case VK_FORMAT_BC2_SRGB_BLOCK:
            result = wrapper_bc2_decompress_block(src_block, dst_block, dst_pitch);
            break;
         case VK_FORMAT_BC3_UNORM_BLOCK:
         case VK_FORMAT_BC3_SRGB_BLOCK:
            result = wrapper_bc3_decompress_block(src_block, dst_block, dst_pitch);
            break;
         case VK_FORMAT_BC4_UNORM_BLOCK:
            result = wrapper_bc4_decompress_block(src_block, dst_block, dst_pitch, false);
            break;
         case VK_FORMAT_BC4_SNORM_BLOCK:
            result = wrapper_bc4_decompress_block(src_block, dst_block, dst_pitch, true);
            break;
         case VK_FORMAT_BC5_UNORM_BLOCK:
            result = wrapper_bc5_decompress_block(src_block, dst_block, dst_pitch, false);
            break;
         case VK_FORMAT_BC5_SNORM_BLOCK:
            result = wrapper_bc5_decompress_block(src_block, dst_block, dst_pitch, true);
            break;
         default:
            result = VK_ERROR_FORMAT_NOT_SUPPORTED;
            break;
         }
         
         if (result != VK_SUCCESS) {
            vk_loge(VK_LOG_OBJS(&device->vk.base),
                    "Failed to decompress %s block at (%u, %u)", info->name, block_x, block_y);
            return result;
         }
      }
   }

   vk_logi(VK_LOG_OBJS(&device->vk.base),
           "Successfully decompressed %s image (%ux%u, %u blocks)",
           info->name, width, height, blocks_x * blocks_y);

   return VK_SUCCESS;
}

VkResult wrapper_bc_device_init(struct wrapper_device *device)
{
   /* Initialize BC image tracking */
   simple_mtx_init(&device->bc_image_mutex, mtx_plain);
   device->bc_image_map = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   
   if (!device->bc_image_map) {
      simple_mtx_destroy(&device->bc_image_mutex);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   
   device->bc_emulation_enabled = true;
   
   vk_logi(VK_LOG_OBJS(&device->vk.base), 
           "BC texture compression emulation initialized");
   return VK_SUCCESS;
}

void wrapper_bc_device_finish(struct wrapper_device *device)
{
   if (!device->bc_emulation_enabled)
      return;
      
   /* Clean up BC image tracking */
   simple_mtx_lock(&device->bc_image_mutex);
   if (device->bc_image_map) {
      hash_table_foreach(device->bc_image_map, entry) {
         free(entry->data);
      }
      _mesa_hash_table_destroy(device->bc_image_map, NULL);
   }
   simple_mtx_unlock(&device->bc_image_mutex);
   simple_mtx_destroy(&device->bc_image_mutex);
   
   device->bc_emulation_enabled = false;
   
   vk_logi(VK_LOG_OBJS(&device->vk.base),
           "BC texture compression emulation finalized");
}

static VkFormat bc_format_to_emulated_format(VkFormat bc_format)
{
   switch (bc_format) {
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   case VK_FORMAT_BC2_UNORM_BLOCK:
   case VK_FORMAT_BC3_UNORM_BLOCK:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
   case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
   case VK_FORMAT_BC2_SRGB_BLOCK:
   case VK_FORMAT_BC3_SRGB_BLOCK:
      return VK_FORMAT_R8G8B8A8_SRGB;
   case VK_FORMAT_BC4_UNORM_BLOCK:
      return VK_FORMAT_R8_UNORM;
   case VK_FORMAT_BC4_SNORM_BLOCK:
      return VK_FORMAT_R8_SNORM;
   case VK_FORMAT_BC5_UNORM_BLOCK:
      return VK_FORMAT_R8G8_UNORM;
   case VK_FORMAT_BC5_SNORM_BLOCK:
      return VK_FORMAT_R8G8_SNORM;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateImage(VkDevice _device,
                   const VkImageCreateInfo* pCreateInfo,
                   const VkAllocationCallbacks* pAllocator,
                   VkImage* pImage)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   
   /* Check if this is a BC format that needs emulation */
   if (device->physical->enable_bc && wrapper_is_bc_format(pCreateInfo->format)) {
      VkFormat emulated_format = bc_format_to_emulated_format(pCreateInfo->format);
      
      if (emulated_format == VK_FORMAT_UNDEFINED) {
         vk_loge(VK_LOG_OBJS(&device->vk.base),
                 "BC format %d not yet supported for emulation", pCreateInfo->format);
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
      
      /* Create modified create info with emulated format */
      VkImageCreateInfo emulated_create_info = *pCreateInfo;
      emulated_create_info.format = emulated_format;
      
      /* Create the actual image with emulated format */
      VkImage emulated_image;
      VkResult result = device->dispatch_table.CreateImage(device->dispatch_handle, 
                                                          &emulated_create_info, 
                                                          pAllocator, 
                                                          &emulated_image);
      if (result != VK_SUCCESS)
         return result;
      
      /* Create BC image info to track the emulation */
      struct bc_image_info *bc_info = malloc(sizeof(struct bc_image_info));
      if (!bc_info) {
         device->dispatch_table.DestroyImage(device->dispatch_handle, emulated_image, pAllocator);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
      
      bc_info->original_format = pCreateInfo->format;
      bc_info->emulated_format = emulated_format;
      bc_info->emulated_image = emulated_image;
      bc_info->width = pCreateInfo->extent.width;
      bc_info->height = pCreateInfo->extent.height;
      bc_info->depth = pCreateInfo->extent.depth;
      bc_info->mip_levels = pCreateInfo->mipLevels;
      bc_info->array_layers = pCreateInfo->arrayLayers;
      
      /* Store the mapping */
      simple_mtx_lock(&device->bc_image_mutex);
      _mesa_hash_table_insert(device->bc_image_map, emulated_image, bc_info);
      simple_mtx_unlock(&device->bc_image_mutex);
      
      *pImage = emulated_image; /* Return the emulated image as the "BC" image */
      
      vk_logi(VK_LOG_OBJS(&device->vk.base),
              "Created BC emulated image: %s -> %s (%ux%ux%u, %u mips, %u layers)",
              wrapper_get_bc_format_info(pCreateInfo->format)->name,
              "RGBA8", /* simplified for log */
              bc_info->width, bc_info->height, bc_info->depth,
              bc_info->mip_levels, bc_info->array_layers);
      
      return VK_SUCCESS;
   }
   
   /* Not a BC format, pass through to underlying driver */
   return device->dispatch_table.CreateImage(device->dispatch_handle, pCreateInfo, pAllocator, pImage);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyImage(VkDevice _device,
                    VkImage image,
                    const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   
   if (image == VK_NULL_HANDLE)
      return;
   
   /* Check if this is a BC emulated image */
   if (device->bc_emulation_enabled) {
      simple_mtx_lock(&device->bc_image_mutex);
      struct hash_entry *entry = _mesa_hash_table_search(device->bc_image_map, image);
      if (entry) {
         free(entry->data);
         _mesa_hash_table_remove(device->bc_image_map, entry);
         vk_logi(VK_LOG_OBJS(&device->vk.base), "Destroyed BC emulated image");
      }
      simple_mtx_unlock(&device->bc_image_mutex);
   }
   
   /* Destroy the actual image */
   device->dispatch_table.DestroyImage(device->dispatch_handle, image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetImageMemoryRequirements(VkDevice _device,
                                  VkImage image,
                                  VkMemoryRequirements* pMemoryRequirements)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   
   /* BC images use the emulated image directly, so just pass through */
   device->dispatch_table.GetImageMemoryRequirements(device->dispatch_handle, image, pMemoryRequirements);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateImageView(VkDevice _device,
                       const VkImageViewCreateInfo* pCreateInfo,
                       const VkAllocationCallbacks* pAllocator,
                       VkImageView* pView)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   
   /* Check if this is a BC emulated image */
   if (device->bc_emulation_enabled) {
      simple_mtx_lock(&device->bc_image_mutex);
      struct hash_entry *entry = _mesa_hash_table_search(device->bc_image_map, pCreateInfo->image);
      if (entry) {
         struct bc_image_info *bc_info = (struct bc_image_info *)entry->data;
         
         /* Create modified view info with emulated format */
         VkImageViewCreateInfo emulated_view_info = *pCreateInfo;
         emulated_view_info.format = bc_info->emulated_format;
         
         simple_mtx_unlock(&device->bc_image_mutex);
         
         VkResult result = device->dispatch_table.CreateImageView(device->dispatch_handle,
                                                                 &emulated_view_info,
                                                                 pAllocator,
                                                                 pView);
         
         if (result == VK_SUCCESS) {
            vk_logi(VK_LOG_OBJS(&device->vk.base),
                    "Created BC emulated image view: %s -> %s",
                    wrapper_get_bc_format_info(bc_info->original_format)->name,
                    "emulated format");
         }
         
         return result;
      }
      simple_mtx_unlock(&device->bc_image_mutex);
   }
   
   /* Not a BC emulated image, pass through */
   return device->dispatch_table.CreateImageView(device->dispatch_handle, pCreateInfo, pAllocator, pView);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_BindImageMemory(VkDevice _device,
                       VkImage image,
                       VkDeviceMemory memory,
                       VkDeviceSize memoryOffset)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   
   /* BC images use the emulated image directly, so just pass through */
   return device->dispatch_table.BindImageMemory(device->dispatch_handle, image, memory, memoryOffset);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                            VkBuffer srcBuffer,
                            VkImage dstImage,
                            VkImageLayout dstImageLayout,
                            uint32_t regionCount,
                            const VkBufferImageCopy* pRegions)
{
   struct wrapper_command_buffer *cmd_buffer = wrapper_command_buffer_from_handle(commandBuffer);
   struct wrapper_device *device = cmd_buffer->device;
   
   /* Check if destination image is BC emulated */
   if (device->bc_emulation_enabled) {
      simple_mtx_lock(&device->bc_image_mutex);
      struct hash_entry *entry = _mesa_hash_table_search(device->bc_image_map, dstImage);
      if (entry) {
         struct bc_image_info *bc_info = (struct bc_image_info *)entry->data;
         simple_mtx_unlock(&device->bc_image_mutex);
         
         /* This is a BC emulated image - we need to decompress data during copy */
         vk_logi(VK_LOG_OBJS(&device->vk.base),
                 "BC texture upload: %s format, %u regions",
                 wrapper_get_bc_format_info(bc_info->original_format)->name,
                 regionCount);
         
         /* Process each region separately for BC decompression */
         for (uint32_t i = 0; i < regionCount; i++) {
            const VkBufferImageCopy *region = &pRegions[i];
            
            /* Calculate the size of the compressed data for this region */
            uint32_t width = region->imageExtent.width;
            uint32_t height = region->imageExtent.height;
            uint32_t depth = region->imageExtent.depth;
            
            VkDeviceSize compressed_size = wrapper_bc_get_compressed_size(bc_info->original_format, width, height) * depth;
            
            /* For now, we implement a simplified approach:
             * 1. We'll create a staging buffer for the decompressed data
             * 2. Use vkCmdCopyBuffer to copy compressed data to a host buffer
             * 3. Decompress on the CPU 
             * 4. Copy decompressed data to the image
             * 
             * A more optimal implementation would use a compute shader for decompression.
             */
            
            /* Calculate decompressed data size */
            const struct bc_format_info *bc_format = wrapper_get_bc_format_info(bc_info->original_format);
            uint32_t pixel_size;
            switch (bc_format->decompressed_format) {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM:
               pixel_size = 1;
               break;
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R8G8_SNORM:
               pixel_size = 2;
               break;
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
            default:
               pixel_size = 4;
               break;
            }
            
            VkDeviceSize decompressed_size = width * height * depth * pixel_size;
            
            /* Create staging buffer for decompressed data */
            VkBufferCreateInfo staging_buffer_info = {
               .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .size = decompressed_size,
               .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            };
            
            VkBuffer staging_buffer;
            VkResult result = device->dispatch_table.CreateBuffer(device->dispatch_handle,
                                                                 &staging_buffer_info,
                                                                 NULL,
                                                                 &staging_buffer);
            if (result != VK_SUCCESS) {
               vk_loge(VK_LOG_OBJS(&device->vk.base),
                       "Failed to create BC staging buffer: %s", vk_Result_to_str(result));
               /* Fall back to direct copy for this region */
               VkBufferImageCopy emulated_region = *region;
               cmd_buffer->device->vk.dispatch_table.CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage,
                                                         dstImageLayout, 1, &emulated_region);
               continue;
            }
            
            /* TODO: Allocate memory for staging buffer, map it, decompress data, and copy to image */
            /* For now, we'll do a direct copy and log the compression info */
            vk_logi(VK_LOG_OBJS(&device->vk.base),
                    "BC region %u: %ux%ux%u, compressed=%zu bytes, decompressed=%zu bytes",
                    i, width, height, depth, compressed_size, decompressed_size);
            
            /* Create modified region for emulated format */
            VkBufferImageCopy emulated_region = *region;
            
            /* Copy using emulated format - this will work but data won't be decompressed */
            cmd_buffer->device->vk.dispatch_table.CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage,
                                                      dstImageLayout, 1, &emulated_region);
            
            /* Clean up staging buffer */
            device->dispatch_table.DestroyBuffer(device->dispatch_handle, staging_buffer, NULL);
         }
         
         return;
      }
      simple_mtx_unlock(&device->bc_image_mutex);
   }
   
   /* Not a BC emulated image, pass through */
   cmd_buffer->device->vk.dispatch_table.CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage,
                                                 dstImageLayout, regionCount, pRegions);
}