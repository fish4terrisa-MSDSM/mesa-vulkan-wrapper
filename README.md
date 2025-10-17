# MESA Vulkan Wrapper
This is a Vulkan wrapper based on [mesa](tps://gitlab.freedesktop.org/mesa/mesa). Mainly contains the work at here: https://github.com/Trass3r/mesa 
It provides x11 wsi with termux-x11 dri3 patches. 
It's intended to be used with qualcomm glibc driver.

## Current Status
It's enough to get testing programs like `vkmark` and `vkcube` working. Compared with the pathetic performance of qualcomm's original vulkan xcb implemention and the unbearable bugs it's so much faster and less buggy.
However, it's still in very early stage, many things are still broken

## Usage
This wrapper is intended to be used with qualcomm's glibc version of adreno vulkan driver. Keep in mind that you have to use a version of driver compiled **without** x11 support.(It's mainly because I'm not bothered to fix the comflict for now)
Usually a version linked with qualcomm's downstream libgbm library will work fine.
Mainly tested with version `r00026` driver on a adreno 740(sm8550) gpu.

## Bugs
 [] - After `vkmark` end the program will hang. `gdb` tell me it's stuck inside `libllvm-qgl.so`, could be some problem related with pthread.
 [] - `GALLIUM_HUD=fps` with zink will cauae the gpu to crash(recoverable) due to HANGFAULT. Here's (part of) the log:
```
[12890.424239] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12890.529564] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12890.614714] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12890.988919] [    T990] kgsl kgsl-3d0: glmark2[225326]: ctx 33 ctx_type VK ts 6 status 00E70087 dispatch_queue=-1 rb 0743/0764 ib1 0000004001E07000/0000 ib2 0000004001E530B0/0000
[12891.598895] [ T700990] kgsl kgsl-3d0: Can't find a memory entry containing IB1BASE                0
[12891.598962] [ T700990] kgsl kgsl-3d0: GPU snapshot froze 160Kb of GPU buffers
[12891.598965] [ T700990] kgsl kgsl-3d0: GPU snapshot created at pa e8500000++0x3ee000
[12891.598971] [ T700990] kgsl kgsl-3d0: falut=HANGFAULT, pid=-33577728, processname=glmark2
[12891.611963] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12891.619088] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12891.635172] [ T502979] kgsl kgsl-3d0: snapshot: objects released
[12892.023398] [    T990] kgsl kgsl-3d0: glmark2[225326]: ctx 33 ctx_type VK ts 10 status 00E71087 dispatch_queue=-1 rb 00a3/00c4 ib1 0000004001F4D000/0000 ib2 0000004001F990B0/0000
[12892.654924] [ T700990] kgsl kgsl-3d0: Can't find a memory entry containing IB1BASE                0
[12892.655271] [ T700990] kgsl kgsl-3d0: GPU snapshot froze 160Kb of GPU buffers
[12892.655279] [ T700990] kgsl kgsl-3d0: GPU snapshot created at pa e8500000++0x3f03b8
[12892.655299] [ T700990] kgsl kgsl-3d0: falut=HANGFAULT, pid=-33577728, processname=glmark2
[12892.674692] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12892.675173] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12893.058145] [ T100990] kgsl kgsl-3d0: glmark2[225326]: ctx 33 ctx_type VK ts 14 status 00E71087 dispatch_queue=-1 rb 01d4/01f5 ib1 0000004002228000/0000 ib2 00000040022740B0/0000
[12893.079366] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12893.100874] [      C1] kgsl kgsl-3d0: CP: AHB bus error, CP_RL_ERROR_DETAILS_0:0x10008e79 CP_RL_ERROR_DETAILS_1:0x12144
[12893.482446] [ T300990] kgsl kgsl-3d0: glmark2[225326]: ctx 33 ctx_type VK ts 18 status 00E71087 dispatch_queue=-1 rb 0167/0188 ib1 0000004001BBF000/0000 ib2 00000040023520B0/0000
[12893.482492] [ T300990] kgsl kgsl-3d0: glmark2[225326]: gpu fault threshold exceeded 3 faults in 2000 msecs
[12912.693397] [ T502979] kgsl kgsl-3d0: snapshot: objects released
```
 [] - many many artifacts, even chromium is kinda buggy and flicking for now.

## Goal
This project's goal(for now) is to make it possible to play minecraft with qualcomm's adreno driver, make full use of the gpu's potential and have no visual bugs.

## License
[docs/license.rst](docs/license.rst)
