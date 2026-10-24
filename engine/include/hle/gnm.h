#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <array>
#include "hle/gnm/command_queue.h"

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
    pm4::Diagnostic recordFrame(VkCommandBuffer cmd, VkImageView imageView,
                               VkExtent2D extent, RecordingTicket& ticket);

    extern "C" GRAVITY_SYSV_ABI int32_t sceGnmSubmitCommandBuffers(
        uint32_t count, const uint32_t* const* dcb_addresses, const uint32_t* dcb_sizes,
        const uint32_t* const* ccb_addresses, const uint32_t* ccb_sizes);

    extern "C" GRAVITY_SYSV_ABI uint32_t
        sceGnmDrawInitDefaultHardwareState200(uint32_t* cmdbuf, uint32_t size);
    extern "C" GRAVITY_SYSV_ABI uint32_t
        sceGnmDispatchInitDefaultHardwareState(uint32_t* cmdbuf, uint32_t size);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmAreSubmitsAllowed();
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceRazorIsLoaded();
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmFlushGarlic();
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmInsertWaitFlipDone(uint32_t* cmdbuf, uint32_t size, int32_t vo_handle, uint32_t buf_idx);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmInsertPushMarker(uint32_t* cmdbuf, uint32_t size, const char* marker);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmInsertPopMarker(uint32_t* cmdbuf, uint32_t size);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmInsertSetMarker(uint32_t* cmdbuf, uint32_t size, const char* marker);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmInsertPushColorMarker(uint32_t* cmdbuf, uint32_t size, const char* marker, uint32_t color);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmInsertSetColorMarker(uint32_t* cmdbuf, uint32_t size, const char* marker, uint32_t color);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetPsShader(uint32_t* cmdbuf, uint32_t size, const void* ps_shader);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmUpdatePsShader(uint32_t* cmdbuf, uint32_t size, const void* ps_shader);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetVsShader(uint32_t* cmdbuf, uint32_t size, const void* vs_shader, uint32_t shader_modifier);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmUpdateVsShader(uint32_t* cmdbuf, uint32_t size, const void* vs_shader, uint32_t shader_modifier);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetEsShader(uint32_t* cmdbuf, uint32_t size, const void* es_shader, uint32_t shader_modifier);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetHsShader(uint32_t* cmdbuf, uint32_t size, const void* hs_shader, uint32_t shader_modifier);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmUpdateHsShader(uint32_t* cmdbuf, uint32_t size, const void* hs_shader, uint32_t shader_modifier);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetLsShader(uint32_t* cmdbuf, uint32_t size, const void* ls_shader, uint32_t shader_modifier);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetCsShader(uint32_t* cmdbuf, uint32_t size, const void* cs_shader);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSetGsShader(uint32_t* cmdbuf, uint32_t size, const void* gs_shader);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmUpdateGsShader(uint32_t* cmdbuf, uint32_t size, const void* gs_shader);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSubmitAndFlipCommandBuffers(
            uint32_t count, const uint32_t* const* dcb_addresses, const uint32_t* dcb_sizes,
            const uint32_t* const* ccb_addresses, const uint32_t* ccb_sizes,
            uint32_t video_out_handle, uint32_t display_buffer_index, uint32_t flip_mode, int64_t flip_arg);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmSubmitDone();
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmIsUserPaEnabled();
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmDriverCaptureInProgress();
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmDriverTriggerCapture();
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmDebugHardwareStatus(void* status, uint32_t size);
    extern "C" GRAVITY_SYSV_ABI int32_t
        sceGnmValidateCommandBuffers(
            uint32_t count, const uint32_t* const* dcb_addresses, const uint32_t* dcb_sizes,
            const uint32_t* const* ccb_addresses, const uint32_t* ccb_sizes);
    extern "C" GRAVITY_SYSV_ABI uint64_t
        sceGnmGetTheTessellationFactorRingBufferBaseAddress();
    extern "C" GRAVITY_SYSV_ABI uint32_t
        sceGnmLogicalCuIndexToPhysicalCuIndex(uint32_t logical_cu_index);
    extern "C" GRAVITY_SYSV_ABI uint32_t
        sceGnmLogicalCuMaskToPhysicalCuMask(uint32_t logical_cu_mask);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmDingDong(uint32_t queue_id, uint32_t ring_offset);
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmDisableMipStatsReport();
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmRequestMipStatsReportAndReset();
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetupMipStatsReport();

    // Diagnostics / Query
    bool gnm_is_initialized() noexcept;
    uint64_t gnm_total_draw_calls() noexcept;

} // namespace gravity::hle::gnm
