#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <memory>

namespace gravity::hle::video {

    // Forward declaration
    class VideoRenderer;

    // Get the singleton video renderer instance.
    VideoRenderer& get_video_renderer() noexcept;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

    // HLE functions for libSceVideoOut (SysV ABI)
    // These are the most common functions used by Gravity Rush.
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceVideoOutInitialize();

    extern "C" GRAVITY_SYSV_ABI void
        sceVideoOutShutdown();

    // Set the frame buffer address (guest virtual address). In our implementation we
    // ignore the guest address and use our own swapchain images.
    extern "C" GRAVITY_SYSV_ABI void
        sceVideoOutSetFrameBuf(uint64_t addr, uint32_t width, uint32_t height,
                               uint32_t pitch, uint32_t format);

    // Wait for VBlank and then present the current frame.
    extern "C" GRAVITY_SYSV_ABI void
        sceVideoOutWaitVblank();

    // Optional: set display mode (resolution, refresh rate). We'll store but not
    // enforce; the game may rely on it for timing.
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceVideoOutSetMode(uint32_t width, uint32_t height,
                           uint32_t refreshRate);

    // Get the current display mode (if needed).
    extern "C" GRAVITY_SYSV_ABI void
        sceVideoOutGetMode(uint32_t* width, uint32_t* height,
                           uint32_t* refreshRate);

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    // Diagnostics / Query
    bool video_is_initialized() noexcept;
    uint64_t video_presented_frames() noexcept;
    bool video_is_headless() noexcept;
    hle::Status initialize_headless();

} // namespace gravity::hle::video