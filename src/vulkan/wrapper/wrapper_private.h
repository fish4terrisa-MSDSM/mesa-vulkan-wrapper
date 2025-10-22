#include "vulkan/runtime/vk_instance.h"
#include "vulkan/runtime/vk_physical_device.h"
#include "vulkan/runtime/vk_device.h"
#include "vulkan/runtime/vk_queue.h"
#include "vulkan/runtime/vk_command_buffer.h"
#include "vulkan/runtime/vk_log.h"
#include "vulkan/util/vk_dispatch_table.h"
#include "vulkan/wsi/wsi_common.h"
#include "util/simple_mtx.h"
#include "util/hash_table.h"

#define WRAPPER_BC                     (1ull << 1)

extern const struct vk_instance_extension_table wrapper_instance_extensions;
extern const struct vk_device_extension_table wrapper_device_extensions;
extern const struct vk_device_extension_table wrapper_filter_extensions;

struct wrapper_instance {
   struct vk_instance vk;

   VkInstance dispatch_handle;
   struct vk_instance_dispatch_table dispatch_table;
};

VK_DEFINE_HANDLE_CASTS(wrapper_instance, vk.base, VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)

struct wrapper_physical_device {
   struct vk_physical_device vk;
   bool enable_bc;
   VkPhysicalDeviceProperties2 properties2;
   VkPhysicalDeviceDriverProperties driver_properties;
   VkPhysicalDevice dispatch_handle;
   VkPhysicalDeviceMemoryProperties memory_properties;
   struct wsi_device wsi_device;
   struct wrapper_instance *instance;
   struct vk_features backup_supported_features;
   struct vk_physical_device_dispatch_table dispatch_table;
   bool robustness2_emulated;
   bool null_descriptors_emulated;
};

VK_DEFINE_HANDLE_CASTS(wrapper_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

struct wrapper_queue {
   struct vk_queue vk;

   struct wrapper_device *device;
   VkQueue dispatch_handle;
};

VK_DEFINE_HANDLE_CASTS(wrapper_queue, vk.base, VkQueue,
                       VK_OBJECT_TYPE_QUEUE)

struct wrapper_device {
   struct vk_device vk;

   VkDevice dispatch_handle;
   simple_mtx_t resource_mutex;
   struct list_head command_buffers;
   struct hash_table *memorys;
   struct wrapper_physical_device *physical;
   struct vk_device_dispatch_table dispatch_table;

   /* Null descriptor emulation */
   bool null_descriptors_enabled;
   VkBuffer dummy_buffer;
   VkDeviceMemory dummy_buffer_memory;
   VkImage dummy_image_1d, dummy_image_2d, dummy_image_3d;
   VkDeviceMemory dummy_image_memory_1d, dummy_image_memory_2d, dummy_image_memory_3d;
   VkImageView dummy_image_view_1d, dummy_image_view_2d, dummy_image_view_3d;
   VkSampler dummy_sampler;
   simple_mtx_t template_cache_mutex;
   struct hash_table *template_cache;

   /* BC texture compression emulation */
   bool bc_emulation_enabled;
   simple_mtx_t bc_image_mutex;
   struct hash_table *bc_image_map; /* Maps VkImage to bc_image_info */
};

VK_DEFINE_HANDLE_CASTS(wrapper_device, vk.base, VkDevice,
                       VK_OBJECT_TYPE_DEVICE)

/* BC emulated image information */
struct bc_image_info {
   VkFormat original_format;    /* Original BC format requested */
   VkFormat emulated_format;    /* Actual format used (e.g., RGBA8) */
   VkImage emulated_image;      /* The actual VkImage created with emulated format */
   uint32_t width, height, depth;
   uint32_t mip_levels;
   uint32_t array_layers;
};
struct wrapper_command_buffer {
   struct vk_command_buffer vk;

   struct wrapper_device *device;
   struct list_head link;
   VkCommandPool pool;
   VkCommandBuffer dispatch_handle;
};

VK_DEFINE_HANDLE_CASTS(wrapper_command_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

struct wrapper_device_memory {
   int dmabuf_fd;
   void *map_address;
   size_t map_size;
   size_t alloc_size;
};

VkResult enumerate_physical_device(struct vk_instance *_instance);
void destroy_physical_device(struct vk_physical_device *pdevice);

void
wrapper_setup_device_features(struct wrapper_physical_device *physical_device);

uint32_t
wrapper_select_device_memory_type(struct wrapper_device *device,
                                  VkMemoryPropertyFlags flags);


/* Null descriptor emulation functions */
VkResult
wrapper_create_dummy_resources(struct wrapper_device *device);

void
wrapper_destroy_dummy_resources(struct wrapper_device *device);

void
wrapper_check_robustness2_emulation(struct wrapper_physical_device *physical_device);

/* Descriptor update function declarations */
VKAPI_ATTR void VKAPI_CALL
wrapper_UpdateDescriptorSets(VkDevice device,
                             uint32_t descriptorWriteCount,
                             const VkWriteDescriptorSet* pDescriptorWrites,
                             uint32_t descriptorCopyCount,
                             const VkCopyDescriptorSet* pDescriptorCopies);

VKAPI_ATTR void VKAPI_CALL
wrapper_UpdateDescriptorSetWithTemplate(VkDevice device,
                                        VkDescriptorSet descriptorSet,
                                        VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                        const void* pData);
/* Descriptor buffer support - stub functions for future implementation */
VKAPI_ATTR void VKAPI_CALL
wrapper_GetDescriptorSetLayoutSizeEXT(VkDevice device,
                                      VkDescriptorSetLayout layout,
                                      VkDeviceSize* pLayoutSizeInBytes);

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDescriptorSetLayoutBindingOffsetEXT(VkDevice device,
                                               VkDescriptorSetLayout layout,
                                               uint32_t binding,
                                               VkDeviceSize* pOffset);

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDescriptorEXT(VkDevice device,
                        const VkDescriptorGetInfoEXT* pDescriptorInfo,
                        size_t dataSize,
                        void* pDescriptor);

/* Template management functions */
VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateDescriptorUpdateTemplate(VkDevice device,
                                      const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate);

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyDescriptorUpdateTemplate(VkDevice device,
                                       VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                       const VkAllocationCallbacks* pAllocator);
/* BC texture compression emulation functions */
VkResult wrapper_bc_device_init(struct wrapper_device *device);
void wrapper_bc_device_finish(struct wrapper_device *device);
/* BC image interception functions */
VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateImage(VkDevice device,
                   const VkImageCreateInfo* pCreateInfo,
                   const VkAllocationCallbacks* pAllocator,
                   VkImage* pImage);

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyImage(VkDevice device,
                    VkImage image,
                    const VkAllocationCallbacks* pAllocator);

VKAPI_ATTR void VKAPI_CALL
wrapper_GetImageMemoryRequirements(VkDevice device,
                                  VkImage image,
                                  VkMemoryRequirements* pMemoryRequirements);

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateImageView(VkDevice device,
                       const VkImageViewCreateInfo* pCreateInfo,
                       const VkAllocationCallbacks* pAllocator,
                       VkImageView* pView);

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_BindImageMemory(VkDevice device,
                       VkImage image,
                       VkDeviceMemory memory,
                       VkDeviceSize memoryOffset);

VKAPI_ATTR void VKAPI_CALL
wrapper_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                            VkBuffer srcBuffer,
                            VkImage dstImage,
                            VkImageLayout dstImageLayout,
                            uint32_t regionCount,
                            const VkBufferImageCopy* pRegions);

/* BC format properties support */
VKAPI_ATTR void VKAPI_CALL
wrapper_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                         VkFormat format,
                                         VkFormatProperties* pFormatProperties);

VKAPI_ATTR void VKAPI_CALL
wrapper_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                          VkFormat format,
                                          VkFormatProperties2* pFormatProperties);
