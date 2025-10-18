# Failed tests from SaschaWillems/Vulkan
## `bloom`
Screen flickering, even caused the Android side to glitch.
## `bufferdeviceaddress`
"VK_KHR_device_group_creation" is not present at instance level.(but turnip works)
## `computeshader`
Screen flickering, even caused the Android side to glitch.
## `conditionalrender`
"VK_EXT_conditional_rendering" is not present at device level.(but turnip works)
## `debugutils`
Screen flickering when `Glow` is enabled, otherwise working just fine.
## `deferred`
Looking up will trigger heavy screen flickering. Other parts working fine.
## `deferredmultisampling`
Looking back will trigger heavy screen flickering. Other parts working fine.
## `deferredshadows`
Very heavy flickering, the screen nearly turned white. Completely unusable.
## `descriptorbuffer`
"VK_EXT_descriptor_buffer" is not present at device level.(but turnip works)
## `displacement`
Got these logs:
```
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
```
No visual impacts spotted tho.
## `distancefieldfonts`
Very small flickering effect spotted. Disable splitcscreen solved the issue.
## `dynamicstate`
"Dynamic State 3" not supported.
## `fragmentshaderbarycentrics`
"VK_KHR_fragment_shader_barycentric" is not present at device level.(turnip also didnt work, share the same error.)
## `graphicspipelinelibrary`
"VK_KHR_pipeline_library" is not present at device level.
"VK_EXT_graphics_pipeline_library" is not present at device level.
(turnip works just fine)
## `hostimagecopy`
"VK_EXT_host_image_copy" is not present at device level.(but turnip works)
## `indirectdraw`
Sometimes will trigger very small flickering when looking at leaves.(but turnip works fine)
## `inlineuniformblocks`
Got this log:
```
Fatal : VkResult is "UNKNOWN_ERROR" in /usr/src/vulkan-example/examples/inlineuniformblocks/inlineuniformblocks.cpp at line 161
inlineuniformblocks: /usr/src/vulkan-example/examples/inlineuniformblocks/inlineuniformblocks.cpp:161: void VulkanExample::setupDescriptors(): Assertion `res == VK_SUCCESS' failed.
```
(same error with turnip)
## `inputattachments`
Will cause screen flickering when "Input attachment" is set to depth.
## `meshshader`
"VK_EXT_mesh_shader" is not present at device level.(same error with turnip)
## `multiview`
Small screen flickering when looking at floor.
## `offscreen`
Looking at floor or enable "Display render target" will cause screen flickering.
## `oit`
Completely usuable. Heavy screen flickering, cannot see anything but white.
## `particlesystem`
Looking from bottom to top will cause screen flickering.
## `pbrtexture`
Got this log:
```
Warning: Estimated wave size mode doesn't match the final wave size mode.
```
No visual bug spotted tho.
## `pipelinestatistics`
Enable "Discard" caused Segfault.
Got this log:
```
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
```
## `radialblur`
Enable "Radial Blur" caused screen flickering.
## `raytracingbasic` `raytracingcallable` `raytracinggltf` `raytracingintersection` `raytracingpositionfetch` `raytracingreflections` `raytracingsbtdata` `raytracingshadows` `raytracingtextures`
"VK_KHR_ray_tracing_pipeline" is not present at device level.(same error as turnip)
## `shaderobjects`
"VK_EXT_shader_object" is not present at device level.(same error as turnip)
## `shadowmapping`
Sometimes will cause screen flickering, enable "Display shadow render target" will cause screen flickering.
## `terraintessellation`
Got these logs:
```
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
```
No visual bugs tho.
## `tessellation`
Got these logs:
```
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
[Warning] Invalid hint set by FE or some other pass, forced with HWRules.
```
No visual bugs tho.
## `texturesparseresidency`
Could not create Vulkan device:
ERROR_FEATURE_NOT_PRESENT
(Turnip: Device does not support sparse residency for 2D images!)
## `trianglevulkan13`
Selected GPU does not support support Vulkan 1.3.
(qualcomm's adreno driver only support up to Vulkan 1.2)

