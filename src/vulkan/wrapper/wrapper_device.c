#include "wrapper_private.h"
#include "wrapper_entrypoints.h"
#include "wrapper_trampolines.h"
#include "wrapper_bc.h"
#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_queue.h"
#include "vk_util.h"
#include "util/simple_mtx.h"
#include "util/list.h"
#include "util/hash_table.h"

const struct vk_device_extension_table wrapper_device_extensions =
{
   .KHR_swapchain = true,
   .EXT_swapchain_maintenance1 = true,
   .KHR_swapchain_mutable_format = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .EXT_display_control = true,
#endif
   .KHR_present_id = true,
   .KHR_present_wait = true,
   .KHR_incremental_present = true,
   .EXT_map_memory_placed = true,
   .KHR_map_memory2 = true,
   .KHR_robustness2 = true,
   .EXT_descriptor_buffer = true,
};

const struct vk_device_extension_table wrapper_filter_extensions =
{
   .EXT_hdr_metadata = true,
   .GOOGLE_display_timing = true,
   .KHR_shader_float_controls = true,
   .KHR_shared_presentable_image = true,
   .EXT_image_compression_control_swapchain = true,
   // The following extensions are listed as broken in eden
   //.EXT_custom_border_color = true,
   .KHR_shader_atomic_int64 = true,
   .KHR_push_descriptor = true, 
   //.EXT_extended_dynamic_state2 = true,
   //.EXT_vertex_input_dynamic_state = true,
};

static void
wrapper_filter_enabled_extensions(const struct vk_device *device,
                                  uint32_t *enable_extension_count,
                                  const char **enable_extensions)
{
   for (int idx = 0; idx < VK_DEVICE_EXTENSION_COUNT; idx++) {
      if (!device->enabled_extensions.extensions[idx])
         continue;

      if (wrapper_device_extensions.extensions[idx])
         continue;

      if (wrapper_filter_extensions.extensions[idx])
         continue;

      enable_extensions[(*enable_extension_count)++] =
         vk_device_extensions[idx].extensionName;
   }
}

static inline void
wrapper_append_required_extensions(const struct vk_device *device,
                                  uint32_t *count,
                                  const char **exts) {
#define REQUIRED_EXTENSION(name) \
   if (!device->enabled_extensions.name && \
       device->physical->supported_extensions.name) { \
      exts[(*count)++] = "VK_" #name; \
   }
   REQUIRED_EXTENSION(KHR_external_fence);
   REQUIRED_EXTENSION(KHR_external_semaphore);
   REQUIRED_EXTENSION(KHR_external_memory);
   REQUIRED_EXTENSION(KHR_external_fence_fd);
   REQUIRED_EXTENSION(KHR_external_semaphore_fd);
   REQUIRED_EXTENSION(KHR_external_memory_fd);
   REQUIRED_EXTENSION(KHR_dedicated_allocation);
   REQUIRED_EXTENSION(EXT_queue_family_foreign);
   REQUIRED_EXTENSION(KHR_maintenance1)
   REQUIRED_EXTENSION(KHR_maintenance2)
   REQUIRED_EXTENSION(KHR_image_format_list)
   REQUIRED_EXTENSION(KHR_timeline_semaphore);
   REQUIRED_EXTENSION(EXT_external_memory_host);
   REQUIRED_EXTENSION(EXT_external_memory_dma_buf);
   REQUIRED_EXTENSION(EXT_image_drm_format_modifier);
//   REQUIRED_EXTENSION(ANDROID_external_memory_android_hardware_buffer);
#undef REQUIRED_EXTENSION
}

static VkResult
wrapper_create_device_queue(struct wrapper_device *device,
                            const VkDeviceCreateInfo* pCreateInfo)
{
   const VkDeviceQueueCreateInfo *create_info;
   struct wrapper_queue *queue;
   VkResult result;

   for (int i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      create_info = &pCreateInfo->pQueueCreateInfos[i];
      for (int j = 0; j < create_info->queueCount; j++) {
         queue = vk_zalloc(&device->vk.alloc, sizeof(*queue), 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!queue)
            return VK_ERROR_OUT_OF_HOST_MEMORY;

         if (create_info->flags) {
            device->dispatch_table.GetDeviceQueue2(
               device->dispatch_handle,
               &(VkDeviceQueueInfo2) {
                  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
                  .flags = create_info->flags,
                  .queueFamilyIndex = create_info->queueFamilyIndex,
                  .queueIndex = j,
               },
               &queue->dispatch_handle);;
         } else {
            device->dispatch_table.GetDeviceQueue(
               device->dispatch_handle, create_info->queueFamilyIndex,
               j, &queue->dispatch_handle);
         }
         queue->device = device;

         result = vk_queue_init(&queue->vk, &device->vk, create_info, j);
         if (result != VK_SUCCESS) {
            vk_free(&device->vk.alloc, queue);
            return result;
         }
      }
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateDevice(VkPhysicalDevice physicalDevice,
                     const VkDeviceCreateInfo* pCreateInfo,
                     const VkAllocationCallbacks* pAllocator,
                     VkDevice* pDevice)
{
   VK_FROM_HANDLE(wrapper_physical_device, physical_device, physicalDevice);
   const char *wrapper_enable_extensions[VK_DEVICE_EXTENSION_COUNT];
   uint32_t wrapper_enable_extension_count = 0;
   VkDeviceCreateInfo wrapper_create_info = *pCreateInfo;
   struct vk_device_dispatch_table dispatch_table;
   struct wrapper_device *device;
   VkPhysicalDeviceFeatures2 *pdf2;
   VkPhysicalDeviceFeatures *pdf;
   VkResult result;

   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator,
                       sizeof(*device), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(physical_device, VK_ERROR_OUT_OF_HOST_MEMORY);

   list_inithead(&device->command_buffers);
   simple_mtx_init(&device->resource_mutex, mtx_plain);
   device->physical = physical_device;
   device->memorys = _mesa_hash_table_create(NULL,
                                             _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
   if (!device->memorys) {
      vk_free2(&physical_device->instance->vk.alloc, pAllocator,
               device);
      return vk_error(physical_device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wrapper_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wrapper_device_trampolines, false);

#define DISABLE_EXT(extension, type, feature) \
   if (!physical_device->backup_supported_features.feature) { \
      VK_STRUCTURE_TYPE_##type##_cast *ext = (VK_STRUCTURE_TYPE_##type##_cast *) vk_find_struct_const(pCreateInfo, type); \
      if (ext) { \
         ext->feature = ext->feature & physical_device->backup_supported_features.feature; \
      } \
   }

   DISABLE_EXT(EXT_transform_feedback, PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, geometryStreams);
   DISABLE_EXT(EXT_transform_feedback, PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, transformFeedback);
   DISABLE_EXT(EXT_host_query_reset, PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT, hostQueryReset);
   DISABLE_EXT(EXT_custom_border_color, PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT, customBorderColors);
   DISABLE_EXT(EXT_custom_border_color, PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT, customBorderColorWithoutFormat);

   result = vk_device_init(&device->vk, &physical_device->vk,
                           &dispatch_table, pCreateInfo, pAllocator);

   if (result != VK_SUCCESS) {
      vk_free2(&physical_device->instance->vk.alloc, pAllocator,
               device);
      return vk_error(physical_device, result);
   }

   wrapper_filter_enabled_extensions(&device->vk,
                                     &wrapper_enable_extension_count,
                                     wrapper_enable_extensions);
   wrapper_append_required_extensions(&device->vk,
                                      &wrapper_enable_extension_count,
                                      wrapper_enable_extensions);

   wrapper_create_info.enabledExtensionCount = wrapper_enable_extension_count;
   wrapper_create_info.ppEnabledExtensionNames = wrapper_enable_extensions;

   pdf = (void *)pCreateInfo->pEnabledFeatures;
   pdf2 = __vk_find_struct((void *)pCreateInfo->pNext,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);

   if (pdf && pdf->textureCompressionBC) {
      pdf->textureCompressionBC &=
         physical_device->backup_supported_features.textureCompressionBC;
   }
   if (pdf2 && pdf2->features.textureCompressionBC) {
      pdf2->features.textureCompressionBC &=
         physical_device->backup_supported_features.textureCompressionBC;
   }

   if (pdf && pdf->multiViewport) {
         pdf->multiViewport &=
            physical_device->backup_supported_features.multiViewport;
   }
   if (pdf2 && pdf2->features.multiViewport) {
         pdf2->features.multiViewport &=
            physical_device->backup_supported_features.multiViewport;
   }
   if (pdf && pdf->depthClamp) {
        pdf->depthClamp &=
            physical_device->backup_supported_features.depthClamp;
   }
   if (pdf2 && pdf2->features.depthClamp) {
        pdf2->features.depthClamp &=
            physical_device->backup_supported_features.depthClamp;
   }
   if (pdf && pdf->depthBiasClamp) {
       pdf->depthBiasClamp &=
            physical_device->backup_supported_features.depthBiasClamp;
   }
   if (pdf2 && pdf2->features.depthBiasClamp) {
       pdf2->features.depthBiasClamp &=
            physical_device->backup_supported_features.depthBiasClamp;
   }
    if (pdf && pdf->fillModeNonSolid) {
       pdf->fillModeNonSolid &=
            physical_device->backup_supported_features.fillModeNonSolid;
   }
   if (pdf2 && pdf2->features.fillModeNonSolid) {
       pdf2->features.fillModeNonSolid &=
            physical_device->backup_supported_features.fillModeNonSolid;
   }
    if (pdf && pdf->shaderClipDistance) {
       pdf->shaderClipDistance &=
            physical_device->backup_supported_features.shaderClipDistance;
   }
   if (pdf2 && pdf2->features.shaderClipDistance) {
       pdf2->features.shaderClipDistance &=
            physical_device->backup_supported_features.shaderClipDistance;
   }
   if (pdf && pdf->shaderCullDistance) {
       pdf->shaderCullDistance &=
            physical_device->backup_supported_features.shaderCullDistance;
   }
   if (pdf2 && pdf2->features.shaderCullDistance) {
       pdf2->features.shaderCullDistance &=
            physical_device->backup_supported_features.shaderCullDistance;
   }

   result = physical_device->dispatch_table.CreateDevice(
      physical_device->dispatch_handle, &wrapper_create_info,
         pAllocator, &device->dispatch_handle);

   if (result != VK_SUCCESS) {
      vk_device_finish(&device->vk);
      vk_free(&device->vk.alloc, device);
      return vk_error(physical_device, result);
   }

   void *gdpa = physical_device->instance->dispatch_table.GetInstanceProcAddr(
      physical_device->instance->dispatch_handle, "vkGetDeviceProcAddr");
   vk_device_dispatch_table_load(&device->dispatch_table, gdpa,
                                 device->dispatch_handle);

   result = wrapper_create_device_queue(device, pCreateInfo);
   if (result != VK_SUCCESS) {
      wrapper_DestroyDevice(wrapper_device_to_handle(device),
                            &device->vk.alloc);
      return vk_error(physical_device, result);
   }

   device->null_descriptors_enabled = physical_device->null_descriptors_emulated;
   if (device->null_descriptors_enabled) {
      /* Initialize template cache */
      simple_mtx_init(&device->template_cache_mutex, mtx_plain);
      device->template_cache = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

      result = wrapper_create_dummy_resources(device);
      if (result != VK_SUCCESS) {
         simple_mtx_destroy(&device->template_cache_mutex);
         _mesa_hash_table_destroy(device->template_cache, NULL);
         wrapper_DestroyDevice(wrapper_device_to_handle(device),
                               &device->vk.alloc);
         return vk_error(physical_device, result);
      }

      /* Intercept descriptor update functions */
      device->vk.dispatch_table.UpdateDescriptorSets =
         wrapper_UpdateDescriptorSets;
      device->vk.dispatch_table.UpdateDescriptorSetWithTemplate =
         wrapper_UpdateDescriptorSetWithTemplate;
      device->vk.dispatch_table.CreateDescriptorUpdateTemplate =
         wrapper_CreateDescriptorUpdateTemplate;
      device->vk.dispatch_table.DestroyDescriptorUpdateTemplate =
         wrapper_DestroyDescriptorUpdateTemplate;
   }

   /* Setup BC texture compression emulation if needed */
   if (physical_device->enable_bc) {
      result = wrapper_bc_device_init(device);
      if (result != VK_SUCCESS) {
         vk_loge(VK_LOG_OBJS(&device->vk.base),
                 "Failed to initialize BC emulation: %s", vk_Result_to_str(result));
         wrapper_DestroyDevice(wrapper_device_to_handle(device),
                               &device->vk.alloc);
         return vk_error(physical_device, result);
      }
      /* Intercept image functions for BC emulation */
      device->vk.dispatch_table.CreateImage = wrapper_CreateImage;
      device->vk.dispatch_table.DestroyImage = wrapper_DestroyImage;
      device->vk.dispatch_table.GetImageMemoryRequirements = wrapper_GetImageMemoryRequirements;
      device->vk.dispatch_table.CreateImageView = wrapper_CreateImageView;
      device->vk.dispatch_table.BindImageMemory = wrapper_BindImageMemory;
      device->vk.dispatch_table.CmdCopyBufferToImage = wrapper_CmdCopyBufferToImage;
   }

   *pDevice = wrapper_device_to_handle(device);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                       uint32_t queueIndex, VkQueue* pQueue) {
   vk_common_GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDeviceQueue2(VkDevice _device, const VkDeviceQueueInfo2* pQueueInfo,
                        VkQueue* pQueue) {
   VK_FROM_HANDLE(vk_device, device, _device);

   struct vk_queue *queue = NULL;
   vk_foreach_queue(iter, device) {
      if (iter->queue_family_index == pQueueInfo->queueFamilyIndex &&
          iter->index_in_family == pQueueInfo->queueIndex &&
          iter->flags == pQueueInfo->flags) {
         queue = iter;
         break;
      }
   }

   *pQueue = queue ? vk_queue_to_handle(queue) : VK_NULL_HANDLE;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
wrapper_GetDeviceProcAddr(VkDevice _device, const char* pName) {
   VK_FROM_HANDLE(wrapper_device, device, _device);
   return vk_device_get_proc_addr(&device->vk, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_QueueSubmit(VkQueue _queue, uint32_t submitCount,
                    const VkSubmitInfo* pSubmits, VkFence fence)
{
   VK_FROM_HANDLE(wrapper_queue, queue, _queue);
   VkSubmitInfo wrapper_submits[submitCount];
   VkCommandBuffer *command_buffers;
   VkResult result;

   for (int i = 0; i < submitCount; i++) {
      const VkSubmitInfo *submit_info = &pSubmits[i];
      command_buffers = malloc(sizeof(VkCommandBuffer) *
         submit_info->commandBufferCount);
      for (int j = 0; j < submit_info->commandBufferCount; j++) {
         VK_FROM_HANDLE(wrapper_command_buffer, wcb,
                        submit_info->pCommandBuffers[j]);
         command_buffers[j] = wcb->dispatch_handle;
      }
      wrapper_submits[i] = pSubmits[i];
      wrapper_submits[i].pCommandBuffers = command_buffers;
   }
   result = queue->device->dispatch_table.QueueSubmit(
      queue->dispatch_handle, submitCount, wrapper_submits, fence);

   for (int i = 0; i < submitCount; i++)
      free((void *)wrapper_submits[i].pCommandBuffers);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_QueueSubmit2(VkQueue _queue, uint32_t submitCount,
                     const VkSubmitInfo2* pSubmits, VkFence fence)
{
   VK_FROM_HANDLE(wrapper_queue, queue, _queue);
   VkSubmitInfo2 wrapper_submits[submitCount];
   VkCommandBufferSubmitInfo *command_buffers;
   VkResult result;

   for (int i = 0; i < submitCount; i++) {
      const VkSubmitInfo2 *submit_info = &pSubmits[i];
      command_buffers = malloc(sizeof(VkCommandBufferSubmitInfo) *
         submit_info->commandBufferInfoCount);
      for (int j = 0; j < submit_info->commandBufferInfoCount; j++) {
         VK_FROM_HANDLE(wrapper_command_buffer, wcb,
                        submit_info->pCommandBufferInfos[j].commandBuffer);
         command_buffers[j] = pSubmits[i].pCommandBufferInfos[j];
         command_buffers[j].commandBuffer = wcb->dispatch_handle;
      }
      wrapper_submits[i] = pSubmits[i];
      wrapper_submits[i].pCommandBufferInfos = command_buffers;
   }
   result = queue->device->dispatch_table.QueueSubmit2(
      queue->dispatch_handle, submitCount, wrapper_submits, fence);

   for (int i = 0; i < submitCount; i++)
      free((void *)wrapper_submits[i].pCommandBufferInfos);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                           uint32_t commandBufferCount,
                           const VkCommandBuffer* pCommandBuffers)
{
   VK_FROM_HANDLE(wrapper_command_buffer, wcb, commandBuffer);
   VkCommandBuffer command_buffers[commandBufferCount];

   for (int i = 0; i < commandBufferCount; i++) {
      command_buffers[i] =
         wrapper_command_buffer_from_handle(pCommandBuffers[i])->dispatch_handle;
   }
   wcb->device->dispatch_table.CmdExecuteCommands(
      wcb->dispatch_handle, commandBufferCount, command_buffers);
}

static VkResult
wrapper_command_buffer_create(struct wrapper_device *device,
                              VkCommandPool pool,
                              VkCommandBuffer dispatch_handle,
                              VkCommandBuffer *pCommandBuffers) {
   struct wrapper_command_buffer *wcb;
   wcb = vk_object_zalloc(&device->vk, &device->vk.alloc,
                          sizeof(struct wrapper_command_buffer),
                          VK_OBJECT_TYPE_COMMAND_BUFFER);
   if (!wcb)
      return vk_error(&device->vk, VK_ERROR_OUT_OF_HOST_MEMORY);

   wcb->device = device;
   wcb->pool = pool;
   wcb->dispatch_handle = dispatch_handle;
   list_add(&wcb->link, &device->command_buffers);

   *pCommandBuffers = wrapper_command_buffer_to_handle(wcb);

   return VK_SUCCESS;
}

static void
wrapper_command_buffer_destroy(struct wrapper_device *device,
                               struct wrapper_command_buffer *wcb) {
   if (wcb == NULL)
      return;

   device->dispatch_table.FreeCommandBuffers(
      device->dispatch_handle, wcb->pool, 1, &wcb->dispatch_handle);

   list_del(&wcb->link);
   vk_object_free(&device->vk, NULL, wcb);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_AllocateCommandBuffers(VkDevice _device,
                               const VkCommandBufferAllocateInfo* pAllocateInfo,
                               VkCommandBuffer* pCommandBuffers)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkResult result;
   uint32_t i;
   
   result = device->dispatch_table.AllocateCommandBuffers(
      device->dispatch_handle, pAllocateInfo, pCommandBuffers);
   if (result != VK_SUCCESS)
      return result;

   simple_mtx_lock(&device->resource_mutex);
   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      result = wrapper_command_buffer_create(
         device, pAllocateInfo->commandPool, pCommandBuffers[i],
         pCommandBuffers + i);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      for (int q = 0; q < i; q++) {
         VK_FROM_HANDLE(wrapper_command_buffer, wcb, pCommandBuffers[q]);
         wrapper_command_buffer_destroy(device, wcb);
      }

      device->dispatch_table.FreeCommandBuffers(
         device->dispatch_handle, pAllocateInfo->commandPool,
         pAllocateInfo->commandBufferCount - i, pCommandBuffers + i);

      for (i = 0; i < pAllocateInfo->commandBufferCount; i++)
         pCommandBuffers[i] = VK_NULL_HANDLE;

   }
   simple_mtx_unlock(&device->resource_mutex);
   return result;

}


VKAPI_ATTR void VKAPI_CALL
wrapper_FreeCommandBuffers(VkDevice _device,
                           VkCommandPool commandPool,
                           uint32_t commandBufferCount,
                           const VkCommandBuffer* pCommandBuffers)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   simple_mtx_lock(&device->resource_mutex);
   for (int i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(wrapper_command_buffer, wcb, pCommandBuffers[i]);
      wrapper_command_buffer_destroy(device, wcb);
   }

   simple_mtx_unlock(&device->resource_mutex);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyCommandPool(VkDevice _device, VkCommandPool commandPool,
                           const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   simple_mtx_lock(&device->resource_mutex);

   list_for_each_entry_safe(struct wrapper_command_buffer, wcb,
                            &device->command_buffers, link) {
      if (wcb->pool == commandPool) {
         wrapper_command_buffer_destroy(device, wcb);
      }
   }
   simple_mtx_unlock(&device->resource_mutex);
   device->dispatch_table.DestroyCommandPool(device->dispatch_handle,
                                             commandPool, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyDevice(VkDevice _device, const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   simple_mtx_lock(&device->resource_mutex);
   list_for_each_entry_safe(struct wrapper_command_buffer, wcb,
                            &device->command_buffers, link) {
      wrapper_command_buffer_destroy(device, wcb);
   }
   if (device->null_descriptors_enabled) {
      wrapper_destroy_dummy_resources(device);
   }
   simple_mtx_unlock(&device->resource_mutex);

   if (device->null_descriptors_enabled) {
      /* Clean up template cache */
      simple_mtx_lock(&device->template_cache_mutex);
      if (device->template_cache) {
         hash_table_foreach(device->template_cache, entry) {
            free(entry->data);
         }
         _mesa_hash_table_destroy(device->template_cache, NULL);
      }
      simple_mtx_unlock(&device->template_cache_mutex);
      simple_mtx_destroy(&device->template_cache_mutex);
      wrapper_destroy_dummy_resources(device);
   }

   /* Clean up BC texture compression emulation */
   if (device->physical->enable_bc) {
      wrapper_bc_device_finish(device);
   }

   list_for_each_entry_safe(struct vk_queue, queue, &device->vk.queues, link) {
      vk_queue_finish(queue);
      vk_free2(&device->vk.alloc, pAllocator, queue);
   }
   if (device->dispatch_handle != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyDevice(device->dispatch_handle, pAllocator);
   }
   simple_mtx_destroy(&device->resource_mutex);
   vk_device_finish(&device->vk);
   vk_free2(&device->vk.alloc, pAllocator, device);
}

static uint64_t
unwrap_device_object(VkObjectType objectType,
                     uint64_t objectHandle)
{
   switch(objectType) {
   case VK_OBJECT_TYPE_DEVICE:
      return (uint64_t)(uintptr_t)wrapper_device_from_handle((VkDevice)(uintptr_t)objectHandle)->dispatch_handle;
   case VK_OBJECT_TYPE_QUEUE:
      return (uint64_t)(uintptr_t)wrapper_queue_from_handle((VkQueue)(uintptr_t)objectHandle)->dispatch_handle;
   case VK_OBJECT_TYPE_COMMAND_BUFFER:
      return (uint64_t)(uintptr_t)wrapper_command_buffer_from_handle((VkCommandBuffer)(uintptr_t)objectHandle)->dispatch_handle;
   default:
      return objectHandle;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_SetPrivateData(VkDevice _device, VkObjectType objectType,
                       uint64_t objectHandle,
                       VkPrivateDataSlot privateDataSlot,
                       uint64_t data) {
   VK_FROM_HANDLE(wrapper_device, device, _device);

   uint64_t object_handle = unwrap_device_object(objectType, objectHandle);
   return device->dispatch_table.SetPrivateData(device->dispatch_handle,
      objectType, object_handle, privateDataSlot, data);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetPrivateData(VkDevice _device, VkObjectType objectType,
                       uint64_t objectHandle,
                       VkPrivateDataSlot privateDataSlot,
                       uint64_t* pData) {
   VK_FROM_HANDLE(wrapper_device, device, _device);

   uint64_t object_handle = unwrap_device_object(objectType, objectHandle);
   return device->dispatch_table.GetPrivateData(device->dispatch_handle,
      objectType, object_handle, privateDataSlot, pData);
}

VkResult
wrapper_create_dummy_resources(struct wrapper_device *device)
{
   VkResult result;
   
   /* Create dummy buffer (1 byte) */
   VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = 1,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   
   result = device->dispatch_table.CreateBuffer(device->dispatch_handle, &buffer_info, NULL, &device->dummy_buffer);
   if (result != VK_SUCCESS)
      return result;

   /* Allocate memory for dummy buffer */
   VkMemoryRequirements buffer_reqs;
   device->dispatch_table.GetBufferMemoryRequirements(device->dispatch_handle, device->dummy_buffer, &buffer_reqs);

   VkMemoryAllocateInfo buffer_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = buffer_reqs.size,
      .memoryTypeIndex = wrapper_select_device_memory_type(device, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   result = device->dispatch_table.AllocateMemory(device->dispatch_handle, &buffer_alloc_info, NULL, &device->dummy_buffer_memory);
   if (result != VK_SUCCESS)
      goto fail_buffer;

   result = device->dispatch_table.BindBufferMemory(device->dispatch_handle, device->dummy_buffer, device->dummy_buffer_memory, 0);
   if (result != VK_SUCCESS)
      goto fail_buffer_memory;

   /* Create dummy 1D image */
   VkImageCreateInfo image_info_1d = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_1D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = { .width = 1, .height = 1, .depth = 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   result = device->dispatch_table.CreateImage(device->dispatch_handle, &image_info_1d, NULL, &device->dummy_image_1d);
   if (result != VK_SUCCESS)
      goto fail_buffer_memory;

   /* Allocate memory for 1D image */
   VkMemoryRequirements image_reqs_1d;
   device->dispatch_table.GetImageMemoryRequirements(device->dispatch_handle, device->dummy_image_1d, &image_reqs_1d);

   VkMemoryAllocateInfo image_alloc_info_1d = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = image_reqs_1d.size,
      .memoryTypeIndex = wrapper_select_device_memory_type(device, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   result = device->dispatch_table.AllocateMemory(device->dispatch_handle, &image_alloc_info_1d, NULL, &device->dummy_image_memory_1d);
   if (result != VK_SUCCESS)
      goto fail_image_1d;

   result = device->dispatch_table.BindImageMemory(device->dispatch_handle, device->dummy_image_1d, device->dummy_image_memory_1d, 0);
   if (result != VK_SUCCESS)
      goto fail_image_memory_1d;

   /* Create dummy 2D image */
   VkImageCreateInfo image_info_2d = image_info_1d;
   image_info_2d.imageType = VK_IMAGE_TYPE_2D;

   result = device->dispatch_table.CreateImage(device->dispatch_handle, &image_info_2d, NULL, &device->dummy_image_2d);
   if (result != VK_SUCCESS)
      goto fail_image_memory_1d;

   /* Allocate memory for 2D image */
   VkMemoryRequirements image_reqs_2d;
   device->dispatch_table.GetImageMemoryRequirements(device->dispatch_handle, device->dummy_image_2d, &image_reqs_2d);

   VkMemoryAllocateInfo image_alloc_info_2d = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = image_reqs_2d.size,
      .memoryTypeIndex = wrapper_select_device_memory_type(device, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   result = device->dispatch_table.AllocateMemory(device->dispatch_handle, &image_alloc_info_2d, NULL, &device->dummy_image_memory_2d);
   if (result != VK_SUCCESS)
      goto fail_image_2d;

   result = device->dispatch_table.BindImageMemory(device->dispatch_handle, device->dummy_image_2d, device->dummy_image_memory_2d, 0);
   if (result != VK_SUCCESS)
      goto fail_image_memory_2d;

   /* Create dummy 3D image */
   VkImageCreateInfo image_info_3d = image_info_1d;
   image_info_3d.imageType = VK_IMAGE_TYPE_3D;

   result = device->dispatch_table.CreateImage(device->dispatch_handle, &image_info_3d, NULL, &device->dummy_image_3d);
   if (result != VK_SUCCESS)
      goto fail_image_memory_2d;

   /* Allocate memory for 3D image */
   VkMemoryRequirements image_reqs_3d;
   device->dispatch_table.GetImageMemoryRequirements(device->dispatch_handle, device->dummy_image_3d, &image_reqs_3d);

   VkMemoryAllocateInfo image_alloc_info_3d = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = image_reqs_3d.size,
      .memoryTypeIndex = wrapper_select_device_memory_type(device, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };

   result = device->dispatch_table.AllocateMemory(device->dispatch_handle, &image_alloc_info_3d, NULL, &device->dummy_image_memory_3d);
   if (result != VK_SUCCESS)
      goto fail_image_3d;

   result = device->dispatch_table.BindImageMemory(device->dispatch_handle, device->dummy_image_3d, device->dummy_image_memory_3d, 0);
   if (result != VK_SUCCESS)
      goto fail_image_memory_3d;

   /* Create image views */
   VkImageViewCreateInfo view_info_1d = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = device->dummy_image_1d,
      .viewType = VK_IMAGE_VIEW_TYPE_1D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };

   result = device->dispatch_table.CreateImageView(device->dispatch_handle, &view_info_1d, NULL, &device->dummy_image_view_1d);
   if (result != VK_SUCCESS)
      goto fail_image_memory_3d;

   VkImageViewCreateInfo view_info_2d = view_info_1d;
   view_info_2d.image = device->dummy_image_2d;
   view_info_2d.viewType = VK_IMAGE_VIEW_TYPE_2D;

   result = device->dispatch_table.CreateImageView(device->dispatch_handle, &view_info_2d, NULL, &device->dummy_image_view_2d);
   if (result != VK_SUCCESS)
      goto fail_image_view_1d;

   VkImageViewCreateInfo view_info_3d = view_info_1d;
   view_info_3d.image = device->dummy_image_3d;
   view_info_3d.viewType = VK_IMAGE_VIEW_TYPE_3D;

   result = device->dispatch_table.CreateImageView(device->dispatch_handle, &view_info_3d, NULL, &device->dummy_image_view_3d);
   if (result != VK_SUCCESS)
      goto fail_image_view_2d;

   /* Create dummy sampler */
   VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
   };

   result = device->dispatch_table.CreateSampler(device->dispatch_handle, &sampler_info, NULL, &device->dummy_sampler);
   if (result != VK_SUCCESS)
      goto fail_image_view_3d;

   return VK_SUCCESS;

fail_image_view_3d:
   device->dispatch_table.DestroyImageView(device->dispatch_handle, device->dummy_image_view_3d, NULL);
fail_image_view_2d:
   device->dispatch_table.DestroyImageView(device->dispatch_handle, device->dummy_image_view_2d, NULL);
fail_image_view_1d:
   device->dispatch_table.DestroyImageView(device->dispatch_handle, device->dummy_image_view_1d, NULL);
fail_image_memory_3d:
   device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_image_memory_3d, NULL);
fail_image_3d:
   device->dispatch_table.DestroyImage(device->dispatch_handle, device->dummy_image_3d, NULL);
fail_image_memory_2d:
   device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_image_memory_2d, NULL);
fail_image_2d:
   device->dispatch_table.DestroyImage(device->dispatch_handle, device->dummy_image_2d, NULL);
fail_image_memory_1d:
   device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_image_memory_1d, NULL);
fail_image_1d:
   device->dispatch_table.DestroyImage(device->dispatch_handle, device->dummy_image_1d, NULL);
fail_buffer_memory:
   device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_buffer_memory, NULL);
fail_buffer:
   device->dispatch_table.DestroyBuffer(device->dispatch_handle, device->dummy_buffer, NULL);
   return result;
}

void
wrapper_destroy_dummy_resources(struct wrapper_device *device)
{
   if (device->dummy_sampler != VK_NULL_HANDLE) {
      device->dispatch_table.DestroySampler(device->dispatch_handle, device->dummy_sampler, NULL);
   }
   if (device->dummy_image_view_3d != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyImageView(device->dispatch_handle, device->dummy_image_view_3d, NULL);
   }
   if (device->dummy_image_view_2d != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyImageView(device->dispatch_handle, device->dummy_image_view_2d, NULL);
   }
   if (device->dummy_image_view_1d != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyImageView(device->dispatch_handle, device->dummy_image_view_1d, NULL);
   }
   if (device->dummy_image_memory_3d != VK_NULL_HANDLE) {
      device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_image_memory_3d, NULL);
   }
   if (device->dummy_image_3d != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyImage(device->dispatch_handle, device->dummy_image_3d, NULL);
   }
   if (device->dummy_image_memory_2d != VK_NULL_HANDLE) {
      device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_image_memory_2d, NULL);
   }
   if (device->dummy_image_2d != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyImage(device->dispatch_handle, device->dummy_image_2d, NULL);
   }
   if (device->dummy_image_memory_1d != VK_NULL_HANDLE) {
      device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_image_memory_1d, NULL);
   }
   if (device->dummy_image_1d != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyImage(device->dispatch_handle, device->dummy_image_1d, NULL);
   }
   if (device->dummy_buffer_memory != VK_NULL_HANDLE) {
      device->dispatch_table.FreeMemory(device->dispatch_handle, device->dummy_buffer_memory, NULL);
   }
   if (device->dummy_buffer != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyBuffer(device->dispatch_handle, device->dummy_buffer, NULL);
   }
}

static void
substitute_null_descriptors(struct wrapper_device *device, uint32_t descriptorWriteCount, VkWriteDescriptorSet* pDescriptorWrites)
{
   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      VkWriteDescriptorSet *write = &pDescriptorWrites[i];
      
      if (write->dstSet == VK_NULL_HANDLE)
         continue;
         
      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         if (write->pBufferInfo) {
            for (uint32_t j = 0; j < write->descriptorCount; j++) {
               VkDescriptorBufferInfo *buf_info = (VkDescriptorBufferInfo*)&write->pBufferInfo[j];
               if (buf_info->buffer == VK_NULL_HANDLE) {
                  buf_info->buffer = device->dummy_buffer;
                  buf_info->offset = 0;
                  buf_info->range = VK_WHOLE_SIZE;
               }
            }
         }
         break;
         
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         if (write->pImageInfo) {
            for (uint32_t j = 0; j < write->descriptorCount; j++) {
               VkDescriptorImageInfo *img_info = (VkDescriptorImageInfo*)&write->pImageInfo[j];
               if (img_info->imageView == VK_NULL_HANDLE) {
                  img_info->imageView = device->dummy_image_view_2d; /* Default to 2D */
                  img_info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
               }
            }
         }
         break;
         
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         if (write->pImageInfo) {
            for (uint32_t j = 0; j < write->descriptorCount; j++) {
               VkDescriptorImageInfo *img_info = (VkDescriptorImageInfo*)&write->pImageInfo[j];
               if (img_info->imageView == VK_NULL_HANDLE) {
                  img_info->imageView = device->dummy_image_view_2d;
                  img_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
               }
               if (img_info->sampler == VK_NULL_HANDLE) {
                  img_info->sampler = device->dummy_sampler;
               }
            }
         }
         break;
         
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         if (write->pImageInfo) {
            for (uint32_t j = 0; j < write->descriptorCount; j++) {
               VkDescriptorImageInfo *img_info = (VkDescriptorImageInfo*)&write->pImageInfo[j];
               if (img_info->sampler == VK_NULL_HANDLE) {
                  img_info->sampler = device->dummy_sampler;
               }
            }
         }
         break;
         
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
wrapper_UpdateDescriptorSets(VkDevice _device,
                             uint32_t descriptorWriteCount,
                             const VkWriteDescriptorSet* pDescriptorWrites,
                             uint32_t descriptorCopyCount,
                             const VkCopyDescriptorSet* pDescriptorCopies)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   
   if (device->null_descriptors_enabled && descriptorWriteCount > 0) {
      /* Make a mutable copy of the descriptor writes */
      VkWriteDescriptorSet *writes = malloc(descriptorWriteCount * sizeof(VkWriteDescriptorSet));
      if (writes) {
         memcpy(writes, pDescriptorWrites, descriptorWriteCount * sizeof(VkWriteDescriptorSet));
         substitute_null_descriptors(device, descriptorWriteCount, writes);
         
         device->dispatch_table.UpdateDescriptorSets(device->dispatch_handle,
                                                     descriptorWriteCount, writes,
                                                     descriptorCopyCount, pDescriptorCopies);
         free(writes);
         return;
      }
   }
   
   /* Fallback to direct call */
   device->dispatch_table.UpdateDescriptorSets(device->dispatch_handle,
                                               descriptorWriteCount, pDescriptorWrites,
                                               descriptorCopyCount, pDescriptorCopies);
}

static void
substitute_null_descriptors_in_template(struct wrapper_device *device,
                                       const VkDescriptorUpdateTemplateCreateInfo *create_info,
                                       void* pData)
{
   if (!device->null_descriptors_enabled || !create_info || !pData)
      return;

   for (uint32_t i = 0; i < create_info->descriptorUpdateEntryCount; i++) {
      const VkDescriptorUpdateTemplateEntry *entry = &create_info->pDescriptorUpdateEntries[i];
      uint8_t *data_ptr = (uint8_t*)pData + entry->offset;

      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         {
            VkDescriptorBufferInfo *buf_info = (VkDescriptorBufferInfo*)data_ptr;
            for (uint32_t j = 0; j < entry->descriptorCount; j++) {
               if (buf_info[j].buffer == VK_NULL_HANDLE) {
                  buf_info[j].buffer = device->dummy_buffer;
                  buf_info[j].offset = 0;
                  buf_info[j].range = VK_WHOLE_SIZE;
               }
               data_ptr += entry->stride;
               buf_info = (VkDescriptorBufferInfo*)data_ptr;
            }
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         {
            VkDescriptorImageInfo *img_info = (VkDescriptorImageInfo*)data_ptr;
            for (uint32_t j = 0; j < entry->descriptorCount; j++) {
               if (img_info[j].imageView == VK_NULL_HANDLE) {
                  img_info[j].imageView = device->dummy_image_view_2d;
                  img_info[j].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
               }
               data_ptr += entry->stride;
               img_info = (VkDescriptorImageInfo*)data_ptr;
            }
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         {
            VkDescriptorImageInfo *img_info = (VkDescriptorImageInfo*)data_ptr;
            for (uint32_t j = 0; j < entry->descriptorCount; j++) {
               if (img_info[j].imageView == VK_NULL_HANDLE) {
                  img_info[j].imageView = device->dummy_image_view_2d;
                  img_info[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
               }
               if (img_info[j].sampler == VK_NULL_HANDLE) {
                  img_info[j].sampler = device->dummy_sampler;
               }
               data_ptr += entry->stride;
               img_info = (VkDescriptorImageInfo*)data_ptr;
            }
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLER:
         {
            VkDescriptorImageInfo *img_info = (VkDescriptorImageInfo*)data_ptr;
            for (uint32_t j = 0; j < entry->descriptorCount; j++) {
               if (img_info[j].sampler == VK_NULL_HANDLE) {
                  img_info[j].sampler = device->dummy_sampler;
               }
               data_ptr += entry->stride;
               img_info = (VkDescriptorImageInfo*)data_ptr;
            }
         }
         break;

      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
wrapper_UpdateDescriptorSetWithTemplate(VkDevice _device,
                                        VkDescriptorSet descriptorSet,
                                        VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                        const void* pData)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   

   if (device->null_descriptors_enabled && pData && descriptorUpdateTemplate != VK_NULL_HANDLE) {
      /* Get cached template info */
      simple_mtx_lock(&device->template_cache_mutex);
      struct hash_entry *entry = _mesa_hash_table_search(device->template_cache, descriptorUpdateTemplate);
      if (entry) {
         VkDescriptorUpdateTemplateCreateInfo *create_info = entry->data;

         /* Create a copy of the data to modify */
         size_t data_size = 0;
         for (uint32_t i = 0; i < create_info->descriptorUpdateEntryCount; i++) {
            const VkDescriptorUpdateTemplateEntry *template_entry = &create_info->pDescriptorUpdateEntries[i];
            size_t entry_end = template_entry->offset + template_entry->stride * template_entry->descriptorCount;
            if (entry_end > data_size) {
               data_size = entry_end;
            }
         }

         void *modified_data = malloc(data_size);
         if (modified_data) {
            memcpy(modified_data, pData, data_size);
            simple_mtx_unlock(&device->template_cache_mutex);

            /* Substitute null descriptors in the copied data */
            substitute_null_descriptors_in_template(device, create_info, modified_data);

            /* Call the driver with modified data */
            device->dispatch_table.UpdateDescriptorSetWithTemplate(device->dispatch_handle,
                                                                   descriptorSet,
                                                                   descriptorUpdateTemplate,
                                                                   modified_data);
            free(modified_data);
            return;
         }
      }
      simple_mtx_unlock(&device->template_cache_mutex);
   }

   /* Fallback: pass through to driver */
   device->dispatch_table.UpdateDescriptorSetWithTemplate(device->dispatch_handle,
                                                          descriptorSet,
                                                          descriptorUpdateTemplate,
                                                          pData);
}

/* Descriptor buffer support - stub implementations for VK_EXT_descriptor_buffer */
VKAPI_ATTR void VKAPI_CALL
wrapper_GetDescriptorSetLayoutSizeEXT(VkDevice _device,
                                      VkDescriptorSetLayout layout,
                                      VkDeviceSize* pLayoutSizeInBytes)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   /* For now, pass through to driver - null descriptor emulation for descriptor buffers
    * would require intercepting descriptor writes into buffer memory */
   device->dispatch_table.GetDescriptorSetLayoutSizeEXT(device->dispatch_handle,
                                                        layout, pLayoutSizeInBytes);
}


VKAPI_ATTR void VKAPI_CALL
wrapper_GetDescriptorSetLayoutBindingOffsetEXT(VkDevice _device,
                                               VkDescriptorSetLayout layout,
                                               uint32_t binding,
                                               VkDeviceSize* pOffset)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   device->dispatch_table.GetDescriptorSetLayoutBindingOffsetEXT(device->dispatch_handle,
                                                                 layout, binding, pOffset);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDescriptorEXT(VkDevice _device,
                        const VkDescriptorGetInfoEXT* pDescriptorInfo,
                        size_t dataSize,
                        void* pDescriptor)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   if (!device->null_descriptors_enabled) {
      device->dispatch_table.GetDescriptorEXT(device->dispatch_handle,
                                             pDescriptorInfo, dataSize, pDescriptor);
      return;
   }

   /* Create a copy of the descriptor info for null handle substitution */
   VkDescriptorGetInfoEXT modified_info = *pDescriptorInfo;
   VkDescriptorImageInfo modified_image_info;
   VkDescriptorAddressInfoEXT modified_address_info;

   bool need_substitution = false;

   /* Check and substitute null handles based on descriptor type */
   switch (pDescriptorInfo->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         if (pDescriptorInfo->data.pSampler && *pDescriptorInfo->data.pSampler == VK_NULL_HANDLE) {
            modified_info.data.pSampler = &device->dummy_sampler;
            need_substitution = true;
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         if (pDescriptorInfo->data.pCombinedImageSampler) {
            modified_image_info = *pDescriptorInfo->data.pCombinedImageSampler;
            if (modified_image_info.imageView == VK_NULL_HANDLE) {
               modified_image_info.imageView = device->dummy_image_view_2d;
               need_substitution = true;
            }
            if (modified_image_info.sampler == VK_NULL_HANDLE) {
               modified_image_info.sampler = device->dummy_sampler;
               need_substitution = true;
            }
            if (need_substitution) {
               modified_info.data.pCombinedImageSampler = &modified_image_info;
            }
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         if (pDescriptorInfo->data.pSampledImage &&
             pDescriptorInfo->data.pSampledImage->imageView == VK_NULL_HANDLE) {
            modified_image_info = *pDescriptorInfo->data.pSampledImage;
            modified_image_info.imageView = device->dummy_image_view_2d;
            modified_info.data.pSampledImage = &modified_image_info;
            need_substitution = true;
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         if (pDescriptorInfo->data.pUniformBuffer &&
             pDescriptorInfo->data.pUniformBuffer->address == 0) {
            modified_address_info = *pDescriptorInfo->data.pUniformBuffer;
            /* Get dummy buffer address for null buffer substitution */
            VkBufferDeviceAddressInfo addr_info = {
               .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
               .buffer = device->dummy_buffer
            };
            modified_address_info.address = device->dispatch_table.GetBufferDeviceAddress(
               device->dispatch_handle, &addr_info);
            modified_address_info.range = VK_WHOLE_SIZE;
            modified_info.data.pUniformBuffer = &modified_address_info;
            need_substitution = true;
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         if (pDescriptorInfo->data.pUniformTexelBuffer &&
             pDescriptorInfo->data.pUniformTexelBuffer->address == 0) {
            modified_address_info = *pDescriptorInfo->data.pUniformTexelBuffer;
            /* Get dummy buffer address for null texel buffer substitution */
            VkBufferDeviceAddressInfo addr_info = {
               .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
               .buffer = device->dummy_buffer
            };
            modified_address_info.address = device->dispatch_table.GetBufferDeviceAddress(
               device->dispatch_handle, &addr_info);
            modified_address_info.range = VK_WHOLE_SIZE;
            modified_info.data.pUniformTexelBuffer = &modified_address_info;
            need_substitution = true;
         }
         break;

      default:
         /* For other descriptor types (e.g., acceleration structures), pass through */
         break;
   }
   device->dispatch_table.GetDescriptorEXT(device->dispatch_handle,
                                          &modified_info, dataSize, pDescriptor);
}

/* Template management functions */
VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateDescriptorUpdateTemplate(VkDevice _device,
                                      const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   VkResult result = device->dispatch_table.CreateDescriptorUpdateTemplate(
      device->dispatch_handle, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);

   if (result == VK_SUCCESS && device->null_descriptors_enabled) {
      /* Cache the template create info for null descriptor substitution */
      VkDescriptorUpdateTemplateCreateInfo *cached_info = malloc(sizeof(VkDescriptorUpdateTemplateCreateInfo));
      if (cached_info) {
         *cached_info = *pCreateInfo;

         /* Deep copy the entries array */
         size_t entries_size = sizeof(VkDescriptorUpdateTemplateEntry) * pCreateInfo->descriptorUpdateEntryCount;
         cached_info->pDescriptorUpdateEntries = malloc(entries_size);
         if (cached_info->pDescriptorUpdateEntries) {
            memcpy((void*)cached_info->pDescriptorUpdateEntries,
                   pCreateInfo->pDescriptorUpdateEntries, entries_size);
         } else {
            free(cached_info);
            cached_info = NULL;
         }

         if (cached_info) {
            simple_mtx_lock(&device->template_cache_mutex);
            _mesa_hash_table_insert(device->template_cache, *pDescriptorUpdateTemplate, cached_info);
            simple_mtx_unlock(&device->template_cache_mutex);
         }
      }
   }

   return result;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyDescriptorUpdateTemplate(VkDevice _device,
                                       VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                       const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   if (device->null_descriptors_enabled && descriptorUpdateTemplate != VK_NULL_HANDLE) {
      /* Remove from template cache */
      simple_mtx_lock(&device->template_cache_mutex);
      struct hash_entry *entry = _mesa_hash_table_search(device->template_cache, descriptorUpdateTemplate);
      if (entry) {
         VkDescriptorUpdateTemplateCreateInfo *cached_info = entry->data;
         free((void*)cached_info->pDescriptorUpdateEntries);
         free(cached_info);
         _mesa_hash_table_remove(device->template_cache, entry);
      }
      simple_mtx_unlock(&device->template_cache_mutex);
   }

   device->dispatch_table.DestroyDescriptorUpdateTemplate(device->dispatch_handle,
                                                          descriptorUpdateTemplate, pAllocator);
}
