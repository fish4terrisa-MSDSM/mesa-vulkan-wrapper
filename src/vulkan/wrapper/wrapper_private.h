#include "vulkan/runtime/vk_instance.h"
#include "vulkan/runtime/vk_physical_device.h"
#include "vulkan/runtime/vk_device.h"
#include "vulkan/runtime/vk_queue.h"
#include "vulkan/runtime/vk_command_buffer.h"
#include "vulkan/runtime/vk_log.h"
#include "vulkan/util/vk_dispatch_table.h"
#include "vulkan/wsi/wsi_common.h"
#include "util/hash_table.h"

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
   bool robustness2_emulated;
   bool null_descriptors_emulated;
   struct vk_physical_device vk;
   VkPhysicalDeviceProperties2 properties2;
   VkPhysicalDeviceDriverProperties driver_properties;
   VkPhysicalDevice dispatch_handle;
   VkPhysicalDeviceMemoryProperties memory_properties;
   struct wsi_device wsi_device;
   struct wrapper_instance *instance;
   struct vk_features backup_supported_features;
   struct vk_physical_device_dispatch_table dispatch_table;
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
};

VK_DEFINE_HANDLE_CASTS(wrapper_device, vk.base, VkDevice,
                       VK_OBJECT_TYPE_DEVICE)

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
