#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <array>

namespace gravity::hle::gnm {

    // Forward declaration
    class GnmRenderer;

    // Get the singleton renderer instance.
    GnmRenderer& get_gnm_renderer() noexcept;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

    // HLE functions for libSceGnmDriver_padebug (SysV ABI)
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceGnmInitialize();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    extern "C" GRAVITY_SYSV_ABI void
        sceGnmShutdown();
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetRenderTarget(uint64_t rt_addr, uint32_t width, uint32_t height,
                              uint32_t pitch, uint32_t format, uint32_t rt_index);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetDepthStencilTarget(uint64_t ds_addr, uint32_t width,
                                    uint32_t height, uint32_t pitch,
                                    uint32_t format);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetVertexBuffer(uint32_t stream_id, uint64_t addr,
                              uint32_t stride, uint32_t offset);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetIndexBuffer(uint64_t addr, uint32_t format,
                              uint32_t offset);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetShader(uint64_t vs_addr, uint64_t ps_addr);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetViewport(float x, float y, float width, float height,
                          float minDepth, float maxDepth);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetScissor(int32_t x, int32_t y, uint32_t width,
                         uint32_t height);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetClearColor(float r, float g, float b, float a);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetBlendState(uint32_t enable,
                            uint32_t srcColor,
                            uint32_t dstColor,
                            uint32_t colorOp,
                            uint32_t srcAlpha,
                            uint32_t dstAlpha,
                            uint32_t alphaOp);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetDepthStencilState(uint32_t depthEnable,
                                   uint32_t depthWrite,
                                   uint32_t depthFunc,
                                   uint32_t stencilEnable,
                                   uint32_t frontFunc,
                                   uint32_t backFunc,
                                   uint32_t stencilReadMask,
                                   uint32_t stencilWriteMask,
                                   uint32_t stencilRef);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmDrawPrimitive(uint32_t primitiveType,
                            uint32_t vertexCount,
                            uint32_t instanceCount,
                            uint32_t startIndex,
                            uint32_t baseVertex,
                            uint32_t startInstance);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmCommitCommands();

    // Internal methods (not exposed as HLE)
    void setDevice(VkDevice device);
    void recordFrame(VkCommandBuffer cmd, VkImageView imageView,
                     VkExtent2D extent);

    // Diagnostics / Query
    bool gnm_is_initialized() noexcept;
    uint64_t gnm_total_draw_calls() noexcept;

} // namespace gravity::hle::gnm