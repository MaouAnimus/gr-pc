#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>

namespace gravity::hle::gnm {

    // Forward declaration of the renderer state (singleton-like).
    class GnmRenderer;

    // Obtain the singleton renderer instance.
    GnmRenderer& get_gnm_renderer() noexcept;

    // -----------------------------------------------------------------
    //  GNM HLE entry points (SysV ABI) that the guest will call.
    //  These map roughly to libSceGnmDriver_padebug functions.
    //  The exact set can be expanded as needed.
    // -----------------------------------------------------------------

    // Set a render target (color buffer). In GNM this is sceGnmSetRenderTarget.
    // Parameters (as per typical GNM):
    //   uint64_t rt_addr   – GPU virtual address of the render target surface
    //   uint32_t width     – width in pixels
    //   uint32_t height    – height in pixels
    //   uint32_t pitch     – stride in bytes
    //   uint32_t format    – GNM surface format (we will map to VkFormat)
    //   uint32_t rt_index  – which render target (0..3)
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetRenderTarget(uint64_t rt_addr,
                              uint32_t width,
                              uint32_t height,
                              uint32_t pitch,
                              uint32_t format,
                              uint32_t rt_index);

    // Set depth/stencil target.
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetDepthStencilTarget(uint64_t ds_addr,
                                    uint32_t width,
                                    uint32_t height,
                                    uint32_t pitch,
                                    uint32_t format);

    // Set vertex buffers.
    //   uint32_t stream_id   – vertex stream index (0..3)
    //   uint64_t addr        – GPU virtual address of vertex buffer
    //   uint32_t stride      – stride in bytes
    //   uint32_t offset      – offset within buffer (bytes)
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetVertexBuffer(uint32_t stream_id,
                              uint64_t addr,
                              uint32_t stride,
                              uint32_t offset);

    // Set index buffer.
    //   uint64_t addr        – GPU virtual address
    //   uint32_t format      – index format (16-bit or 32-bit)
    //   uint32_t offset      – offset within buffer
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetIndexBuffer(uint64_t addr,
                             uint32_t format,
                             uint32_t offset);

    // Set shader program (vertex/fragment). In GNM shaders are loaded separately.
    //   uint64_t vs_addr     – GPU virtual address of vertex shader binary (PSSL)
    //   uint64_t ps_addr     – GPU virtual address of fragment shader binary
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetShader(uint64_t vs_addr,
                        uint64_t ps_addr);

    // Set viewport (x, y, width, height, minDepth, maxDepth)
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetViewport(float x,
                          float y,
                          float width,
                          float height,
                          float minDepth,
                          float maxDepth);

    // Set scissor rectangle (x, y, width, height)
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetScissor(int32_t x,
                         int32_t y,
                         uint32_t width,
                         uint32_t height);

    // Set blend state (enable, srcColor, dstColor, colorOp, srcAlpha, dstAlpha, alphaOp)
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmSetBlendState(uint32_t enable,
                            uint32_t srcColor,
                            uint32_t dstColor,
                            uint32_t colorOp,
                            uint32_t srcAlpha,
                            uint32_t dstAlpha,
                            uint32_t alphaOp);

    // Set depth stencil state (depth enable, depth write, depth func, stencil enable, front/back)
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

    // Draw primitives.
    //   uint32_t primitiveType – e.g., TRIANGLES, LINES, POINTS
    //   uint32_t vertexCount   – number of vertices
    //   uint32_t instanceCount – number of instances (usually 1)
    //   uint32_t startIndex    – start index in index buffer (if indexed)
    //   uint32_t baseVertex    – base vertex offset
    //   uint32_t startInstance – start instance
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmDrawPrimitive(uint32_t primitiveType,
                            uint32_t vertexCount,
                            uint32_t instanceCount,
                            uint32_t startIndex,
                            uint32_t baseVertex,
                            uint32_t startInstance);

    // Commit commands / flush (optional, depends on how GNM works)
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmCommitCommands();

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif

    // Initialize the GNM subsystem (called once by the guest).
    extern "C" GRAVITY_SYSV_ABI gravity::hle::Status
        sceGnmInitialize();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    // Shutdown the GNM subsystem.
    extern "C" GRAVITY_SYSV_ABI void
        sceGnmShutdown();

} // namespace gravity::hle::gnm