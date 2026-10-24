#include "hle/gnm.h"
#include "hle/kernel.h"
#include "hle/guest_memory.h"
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <mutex>
#include <cstring>
#include <iostream>

namespace gravity::hle::gnm {

namespace {

struct RenderTargetSlot {
    uint64_t addr{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t pitch{0};
    uint32_t format{0};
    bool active{false};
};

struct VertexBufferSlot {
    uint64_t addr{0};
    uint32_t stride{0};
    uint32_t offset{0};
    bool active{false};
};

} // anonymous namespace

class GnmRenderer {
public:
    GnmRenderer() = default;
    ~GnmRenderer() = default;

    hle::Status initialize() {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (initialized_) return {hle::Error::invalid_state};

        command_queue().reset();
        initialized_ = true;
        clear_color_[0] = 0.05f; // Kat's dark cel-shaded comic blue
        clear_color_[1] = 0.05f;
        clear_color_[2] = 0.08f;
        clear_color_[3] = 1.0f;

        viewport_ = {0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f};
        scissor_ = {{0, 0}, {1920, 1080}};

        depth_bias_constant_ = 1.25f; // Calibrated for Inverted Hull cel-shading outlines
        depth_bias_slope_ = 1.75f;
        depth_bias_clamp_ = 0.0f;
        depth_bias_enabled_ = true;

        total_draw_calls_ = 0;
        return {};
    }

    void shutdown() {
        std::lock_guard<hle::Mutex> lock(mutex_);
        command_queue().reset();
        initialized_ = false;
        device_ = VK_NULL_HANDLE;
    }

    void setDevice(VkDevice dev) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (dev != device_) command_queue().reset();
        device_ = dev;
    }

    void setRenderTarget(uint64_t rt_addr, uint32_t width, uint32_t height,
                         uint32_t pitch, uint32_t format, uint32_t rt_index) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (rt_index >= render_targets_.size()) return;

        // Discard writes if slot is unbound (addr == 0) to prevent device loss on NVIDIA/AMD
        if (rt_addr == 0) {
            render_targets_[rt_index].active = false;
            render_targets_[rt_index].addr = 0;
            return;
        }

        render_targets_[rt_index].addr = rt_addr;
        render_targets_[rt_index].width = width;
        render_targets_[rt_index].height = height;
        render_targets_[rt_index].pitch = pitch;
        render_targets_[rt_index].format = format;
        render_targets_[rt_index].active = true;
    }

    void setDepthStencilTarget(uint64_t ds_addr, uint32_t width, uint32_t height,
                               uint32_t pitch, uint32_t format) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        depth_target_.addr = ds_addr;
        depth_target_.width = width;
        depth_target_.height = height;
        depth_target_.pitch = pitch;
        depth_target_.format = format;
        depth_target_.active = (ds_addr != 0);
    }

    void setVertexBuffer(uint32_t stream_id, uint64_t addr, uint32_t stride, uint32_t offset) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (stream_id >= vertex_buffers_.size()) return;
        vertex_buffers_[stream_id].addr = addr;
        vertex_buffers_[stream_id].stride = stride;
        vertex_buffers_[stream_id].offset = offset;
        vertex_buffers_[stream_id].active = (addr != 0);
    }

    void setIndexBuffer(uint64_t addr, uint32_t format, uint32_t offset) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        index_buffer_addr_ = addr;
        index_buffer_format_ = format;
        index_buffer_offset_ = offset;
    }

    void setShader(uint64_t vs_addr, uint64_t ps_addr) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        vs_addr_ = vs_addr;
        ps_addr_ = ps_addr;
    }

    void setPsShader(uint64_t ps_addr) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        ps_addr_ = ps_addr;
    }

    void setVsShader(uint64_t vs_addr) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        vs_addr_ = vs_addr;
    }

    void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        viewport_.x = x;
        viewport_.y = y;
        viewport_.width = width;
        viewport_.height = height;
        viewport_.minDepth = minDepth;
        viewport_.maxDepth = maxDepth;
    }

    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        scissor_.offset = {x, y};
        scissor_.extent = {width, height};
    }

    void setClearColor(float r, float g, float b, float a) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        clear_color_[0] = r;
        clear_color_[1] = g;
        clear_color_[2] = b;
        clear_color_[3] = a;
    }

    void setBlendState(uint32_t enable, uint32_t srcColor, uint32_t dstColor, uint32_t colorOp,
                       uint32_t srcAlpha, uint32_t dstAlpha, uint32_t alphaOp) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        blend_enable_ = (enable != 0);
        src_color_ = srcColor;
        dst_color_ = dstColor;
        color_op_ = colorOp;
        src_alpha_ = srcAlpha;
        dst_alpha_ = dstAlpha;
        alpha_op_ = alphaOp;
    }

    void setDepthStencilState(uint32_t depthEnable, uint32_t depthWrite, uint32_t depthFunc,
                              uint32_t stencilEnable, uint32_t frontFunc, uint32_t backFunc,
                              uint32_t stencilReadMask, uint32_t stencilWriteMask, uint32_t stencilRef) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        depth_enable_ = (depthEnable != 0);
        depth_write_ = (depthWrite != 0);
        depth_func_ = depthFunc;
        stencil_enable_ = (stencilEnable != 0);
        front_func_ = frontFunc;
        back_func_ = backFunc;
        stencil_read_mask_ = stencilReadMask;
        stencil_write_mask_ = stencilWriteMask;
        stencil_ref_ = stencilRef;
    }

    void drawPrimitive(uint32_t primitiveType, uint32_t vertexCount, uint32_t instanceCount,
                       uint32_t startIndex, uint32_t baseVertex, uint32_t startInstance) {
        (void)primitiveType; (void)startIndex; (void)baseVertex; (void)startInstance;
        std::lock_guard<hle::Mutex> lock(mutex_);
        total_draw_calls_++;
        last_vertex_count_ = vertexCount;
        last_instance_count_ = instanceCount;
    }

    void commitCommands() {
        std::lock_guard<hle::Mutex> lock(mutex_);
        // Commands committed to queue
    }

    pm4::Diagnostic recordFrame(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent, RecordingTicket& ticket) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        ticket = {};
        if (cmd == VK_NULL_HANDLE || targetView == VK_NULL_HANDLE || !extent.width || !extent.height)
            return {pm4::Error::state,0,0,0,"Missing Vulkan render target/command buffer"};

        // Dynamic rendering attachment info
        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = targetView;
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue.color.float32[0] = clear_color_[0];
        color_attachment.clearValue.color.float32[1] = clear_color_[1];
        color_attachment.clearValue.color.float32[2] = clear_color_[2];
        color_attachment.clearValue.color.float32[3] = clear_color_[3];

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea = {{0, 0}, extent};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        rendering_info.pDepthAttachment = nullptr;
        rendering_info.pStencilAttachment = nullptr;

        vkCmdBeginRendering(cmd, &rendering_info);

        const auto result = command_queue().record(cmd, extent, ticket);

        vkCmdEndRendering(cmd);
        return result;
    }

    [[nodiscard]] bool isInitialized() const noexcept { std::lock_guard<hle::Mutex> lock(mutex_); return initialized_; }
    [[nodiscard]] uint64_t totalDrawCalls() const noexcept { std::lock_guard<hle::Mutex> lock(mutex_); return total_draw_calls_; }

private:
    mutable hle::Mutex mutex_{};
    bool initialized_{false};
    VkDevice device_{VK_NULL_HANDLE};

    std::array<RenderTargetSlot, 4> render_targets_{};
    RenderTargetSlot depth_target_{};

    std::array<VertexBufferSlot, 16> vertex_buffers_{};
    uint64_t index_buffer_addr_{0};
    uint32_t index_buffer_format_{0};
    uint32_t index_buffer_offset_{0};

    uint64_t vs_addr_{0};
    uint64_t ps_addr_{0};

    std::array<float, 4> clear_color_{0.0f, 0.0f, 0.0f, 1.0f};
    VkViewport viewport_{};
    VkRect2D scissor_{};

    float depth_bias_constant_{0.0f};
    float depth_bias_slope_{0.0f};
    float depth_bias_clamp_{0.0f};
    bool depth_bias_enabled_{false};

    bool blend_enable_{false};
    uint32_t src_color_{0};
    uint32_t dst_color_{0};
    uint32_t color_op_{0};
    uint32_t src_alpha_{0};
    uint32_t dst_alpha_{0};
    uint32_t alpha_op_{0};

    bool depth_enable_{false};
    bool depth_write_{false};
    uint32_t depth_func_{0};
    bool stencil_enable_{false};
    uint32_t front_func_{0};
    uint32_t back_func_{0};
    uint32_t stencil_read_mask_{0};
    uint32_t stencil_write_mask_{0};
    uint32_t stencil_ref_{0};

    uint64_t total_draw_calls_{0};
    uint32_t last_vertex_count_{0};
    uint32_t last_instance_count_{0};
};

static GnmRenderer g_gnmRenderer;

GnmRenderer& get_gnm_renderer() noexcept {
    return g_gnmRenderer;
}

void setDevice(VkDevice device) {
    g_gnmRenderer.setDevice(device);
}

pm4::Diagnostic recordFrame(VkCommandBuffer cmd, VkImageView imageView, VkExtent2D extent, RecordingTicket& ticket) {
    return g_gnmRenderer.recordFrame(cmd, imageView, extent, ticket);
}

bool gnm_is_initialized() noexcept {
    return g_gnmRenderer.isInitialized();
}

uint64_t gnm_total_draw_calls() noexcept {
    return g_gnmRenderer.totalDrawCalls();
}

extern "C" {

GRAVITY_SYSV_ABI gravity::hle::Status sceGnmInitialize() {
    return g_gnmRenderer.initialize();
}

GRAVITY_SYSV_ABI void sceGnmShutdown() {
    g_gnmRenderer.shutdown();
}

GRAVITY_SYSV_ABI void sceGnmSetRenderTarget(uint64_t rt_addr, uint32_t width, uint32_t height,
                                           uint32_t pitch, uint32_t format, uint32_t rt_index) {
    g_gnmRenderer.setRenderTarget(rt_addr, width, height, pitch, format, rt_index);
}

GRAVITY_SYSV_ABI void sceGnmSetDepthStencilTarget(uint64_t ds_addr, uint32_t width, uint32_t height,
                                                 uint32_t pitch, uint32_t format) {
    g_gnmRenderer.setDepthStencilTarget(ds_addr, width, height, pitch, format);
}

GRAVITY_SYSV_ABI void sceGnmSetVertexBuffer(uint32_t stream_id, uint64_t addr,
                                           uint32_t stride, uint32_t offset) {
    g_gnmRenderer.setVertexBuffer(stream_id, addr, stride, offset);
}

GRAVITY_SYSV_ABI void sceGnmSetIndexBuffer(uint64_t addr, uint32_t format, uint32_t offset) {
    g_gnmRenderer.setIndexBuffer(addr, format, offset);
}

GRAVITY_SYSV_ABI void sceGnmSetShader(uint64_t vs_addr, uint64_t ps_addr) {
    g_gnmRenderer.setShader(vs_addr, ps_addr);
}

GRAVITY_SYSV_ABI void sceGnmSetViewport(float x, float y, float width, float height,
                                       float minDepth, float maxDepth) {
    g_gnmRenderer.setViewport(x, y, width, height, minDepth, maxDepth);
}

GRAVITY_SYSV_ABI void sceGnmSetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    g_gnmRenderer.setScissor(x, y, width, height);
}

GRAVITY_SYSV_ABI void sceGnmSetClearColor(float r, float g, float b, float a) {
    g_gnmRenderer.setClearColor(r, g, b, a);
}

GRAVITY_SYSV_ABI void sceGnmSetBlendState(uint32_t enable, uint32_t srcColor, uint32_t dstColor,
                                         uint32_t colorOp, uint32_t srcAlpha, uint32_t dstAlpha,
                                         uint32_t alphaOp) {
    g_gnmRenderer.setBlendState(enable, srcColor, dstColor, colorOp, srcAlpha, dstAlpha, alphaOp);
}

GRAVITY_SYSV_ABI void sceGnmSetDepthStencilState(uint32_t depthEnable, uint32_t depthWrite,
                                                uint32_t depthFunc, uint32_t stencilEnable,
                                                uint32_t frontFunc, uint32_t backFunc,
                                                uint32_t stencilReadMask, uint32_t stencilWriteMask,
                                                uint32_t stencilRef) {
    g_gnmRenderer.setDepthStencilState(depthEnable, depthWrite, depthFunc, stencilEnable,
                                       frontFunc, backFunc, stencilReadMask, stencilWriteMask,
                                       stencilRef);
}

GRAVITY_SYSV_ABI void sceGnmDrawPrimitive(uint32_t primitiveType, uint32_t vertexCount,
                                         uint32_t instanceCount, uint32_t startIndex,
                                         uint32_t baseVertex, uint32_t startInstance) {
    g_gnmRenderer.drawPrimitive(primitiveType, vertexCount, instanceCount, startIndex,
                                baseVertex, startInstance);
}

GRAVITY_SYSV_ABI void sceGnmCommitCommands() {
    g_gnmRenderer.commitCommands();
}

GRAVITY_SYSV_ABI uint32_t sceGnmDrawInitDefaultHardwareState200(uint32_t* cmdbuf, uint32_t size) {
    {
        static std::atomic<unsigned> init_calls{};
        if (init_calls.fetch_add(1) < 2) {
            std::fprintf(stderr, "[GNM] sceGnmDrawInitDefaultHardwareState200: cmdbuf=%p, size=%u\n", static_cast<void*>(cmdbuf), size);
        }
    }
    if (!cmdbuf || size < 256 || !gravity::hle::guest_memory::accessible(cmdbuf, 256 * sizeof(uint32_t), true)) {
        return 0;
    }

    static constexpr std::uint32_t kClearStateSequence[] = {
        0xc0012800u, 0x80000000u, 0x80000000u, // CONTEXT_CONTROL
        0xc0001200u, 0u,                        // CLEAR_STATE
        0xc0055800u, 0x2ec47fc0u, 0xffffffffu, 0u, 0u, 0u, 10u, // ACQUIRE_MEM
    };
    static constexpr std::size_t kClearStateSize = sizeof(kClearStateSequence) / sizeof(uint32_t); // 12

    static constexpr std::uint32_t kInitSequence200[] = {
        0xc0017600u, 0x216u, 0xffffffffu,
        0xc0017600u, 0x217u, 0xffffffffu,
        0xc0017600u, 0x215u, 0u,
        0xc0016900u, 0x2f9u, 0x2du,
        0xc0016900u, 0x282u, 8u,
        0xc0016900u, 0x280u, 0x80008u,
        0xc0016900u, 0x281u, 0xffff0000u,
        0xc0016900u, 0x204u, 0u,
        0xc0016900u, 0x206u, 0x43fu,
        0xc0016900u, 0x83u,  0xffffu,
        0xc0016900u, 0x317u, 0x10u,
        0xc0016900u, 0x2fau, 0x3f800000u,
        0xc0016900u, 0x2fcu, 0x3f800000u,
        0xc0016900u, 0x2fbu, 0x3f800000u,
        0xc0016900u, 0x2fdu, 0x3f800000u,
        0xc0016900u, 0x202u, 0xcc0010u,
        0xc0016900u, 0x30eu, 0xffffffffu,
        0xc0016900u, 0x30fu, 0xffffffffu,
        0xc0002f00u, 1u,
        0xc0017600u, 7u,     0x1701ffu,
        0xc0017600u, 0x46u,  0x1701fdu,
        0xc0017600u, 0x87u,  0x1701ffu,
        0xc0017600u, 0xc7u,  0x1701fdu,
        0xc0017600u, 0x107u, 0x17u,
        0xc0017600u, 0x147u, 0x1701fdu,
        0xc0017600u, 0x47u,  0x1cu,
        0xc0016900u, 0x1b1u, 2u,
        0xc0016900u, 0x101u, 0u,
        0xc0016900u, 0x100u, 0xffffffffu,
        0xc0016900u, 0x103u, 0u,
        0xc0016900u, 0x284u, 0u,
        0xc0016900u, 0x290u, 0u,
        0xc0016900u, 0x2aeu, 0u,
        0xc0016900u, 0x292u, 0u,
        0xc0016900u, 0x293u, 0x6020000u,
        0xc0016900u, 0x2f8u, 0u,
        0xc0016900u, 0x2deu, 0x1e9u,
        0xc0036900u, 0x295u, 0x100u, 0x100u, 4u,
        0xc0017900u, 0x200u, 0xe0000000u,
    };
    static constexpr std::size_t kInitSequence200Size = sizeof(kInitSequence200) / sizeof(uint32_t); // 118

    std::memcpy(cmdbuf, kClearStateSequence, sizeof(kClearStateSequence));
    std::memcpy(cmdbuf + kClearStateSize, kInitSequence200, sizeof(kInitSequence200));

    constexpr std::size_t written_so_far = kClearStateSize + kInitSequence200Size; // 130
    constexpr std::size_t remaining = 256 - written_so_far; // 126
    constexpr std::size_t nop_payload_size = remaining - 1; // 125

    cmdbuf[written_so_far] = 0xC0000000u | (static_cast<uint32_t>(nop_payload_size - 1) << 16) | 0x1000u;
    std::memset(cmdbuf + written_so_far + 1, 0, nop_payload_size * sizeof(uint32_t));

    return 256;
}

GRAVITY_SYSV_ABI uint32_t sceGnmDispatchInitDefaultHardwareState(uint32_t* cmdbuf, uint32_t size) {
    if (!cmdbuf || size < 256 || !gravity::hle::guest_memory::accessible(cmdbuf, 256 * sizeof(uint32_t), true)) {
        return 0;
    }

    static constexpr std::uint32_t kDispatchInitSequence[] = {
        0xc0016900u, 0x216u, 0xffffffffu, // SetShReg 0x216
        0xc0016900u, 0x217u, 0xffffffffu, // SetShReg 0x217
        0xc0016900u, 0x215u, 0x170u,        // SetShReg 0x215
        0xc0055800u, 0x28000000u, 0u, 0u, 0u, 0u, 0xau, // AcquireMem (6 payload words)
    };
    static constexpr std::size_t kDispatchSeqSize = sizeof(kDispatchInitSequence) / sizeof(uint32_t);

    std::memcpy(cmdbuf, kDispatchInitSequence, sizeof(kDispatchInitSequence));

    constexpr std::size_t written_so_far = kDispatchSeqSize;
    constexpr std::size_t remaining = 256 - written_so_far;
    constexpr std::size_t nop_payload_size = remaining - 1;

    cmdbuf[written_so_far] = 0xC0000000u | (static_cast<uint32_t>(nop_payload_size - 1) << 16) | 0x1000u;
    std::memset(cmdbuf + written_so_far + 1, 0, nop_payload_size * sizeof(uint32_t));

    return 256;
}

GRAVITY_SYSV_ABI int32_t sceGnmAreSubmitsAllowed() {
    return 1;
}

GRAVITY_SYSV_ABI int32_t sceRazorIsLoaded() {
    return 0;
}

GRAVITY_SYSV_ABI void sceGnmFlushGarlic() {
    // Memory cache flush is not required on unified host memory architecture
}

GRAVITY_SYSV_ABI int32_t sceGnmInsertWaitFlipDone(uint32_t* cmdbuf, uint32_t size, int32_t vo_handle, uint32_t buf_idx) {
    (void)vo_handle;
    if (!cmdbuf || size != 7 || !gravity::hle::guest_memory::accessible(cmdbuf, 7 * sizeof(uint32_t), true)) {
        return -1;
    }

    static uint64_t s_flip_label = 0;
    const uintptr_t label_addr = reinterpret_cast<uintptr_t>(&s_flip_label);
    const uintptr_t poll_addr = (label_addr + buf_idx * sizeof(uintptr_t)) & ~0x3ull;

    cmdbuf[0] = 0xc0053c00u; // PM4 Type 3 header: WaitRegMem, 5 body dwords
    cmdbuf[1] = 0x13u;        // Equal (3) | Memory (0x10)
    cmdbuf[2] = static_cast<uint32_t>(poll_addr & 0xFFFFFFFFu);
    cmdbuf[3] = static_cast<uint32_t>((poll_addr >> 32) & 0xFFFFFFFFu);
    cmdbuf[4] = 0u;           // ref = 0 (matches s_flip_label)
    cmdbuf[5] = 0xFFFFFFFFu;  // mask
    cmdbuf[6] = 10u;          // poll_interval

    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmInsertPushMarker(uint32_t* cmdbuf, uint32_t size, const char* marker) {
    (void)marker;
    if (!cmdbuf || size < 2 || !gravity::hle::guest_memory::accessible(cmdbuf, size * sizeof(uint32_t), true)) {
        return -1;
    }
    cmdbuf[0] = 0xC0001000u | (static_cast<uint32_t>(size - 2) << 16);
    cmdbuf[1] = 0x1u;
    std::memset(cmdbuf + 2, 0, (size - 2) * sizeof(uint32_t));
    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmInsertPopMarker(uint32_t* cmdbuf, uint32_t size) {
    if (!cmdbuf || size < 2 || !gravity::hle::guest_memory::accessible(cmdbuf, size * sizeof(uint32_t), true)) {
        return -1;
    }
    cmdbuf[0] = 0xC0001000u | (static_cast<uint32_t>(size - 2) << 16);
    cmdbuf[1] = 0x2u;
    std::memset(cmdbuf + 2, 0, (size - 2) * sizeof(uint32_t));
    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmInsertSetMarker(uint32_t* cmdbuf, uint32_t size, const char* marker) {
    (void)marker;
    if (!cmdbuf || size < 2 || !gravity::hle::guest_memory::accessible(cmdbuf, size * sizeof(uint32_t), true)) {
        return -1;
    }
    cmdbuf[0] = 0xC0001000u | (static_cast<uint32_t>(size - 2) << 16);
    cmdbuf[1] = 0x3u;
    std::memset(cmdbuf + 2, 0, (size - 2) * sizeof(uint32_t));
    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmInsertPushColorMarker(uint32_t* cmdbuf, uint32_t size, const char* marker, uint32_t color) {
    (void)marker; (void)color;
    if (!cmdbuf || size < 2 || !gravity::hle::guest_memory::accessible(cmdbuf, size * sizeof(uint32_t), true)) {
        return -1;
    }
    cmdbuf[0] = 0xC0001000u | (static_cast<uint32_t>(size - 2) << 16);
    cmdbuf[1] = 0x4u;
    std::memset(cmdbuf + 2, 0, (size - 2) * sizeof(uint32_t));
    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmInsertSetColorMarker(uint32_t* cmdbuf, uint32_t size, const char* marker, uint32_t color) {
    (void)marker; (void)color;
    if (!cmdbuf || size < 2 || !gravity::hle::guest_memory::accessible(cmdbuf, size * sizeof(uint32_t), true)) {
        return -1;
    }
    cmdbuf[0] = 0xC0001000u | (static_cast<uint32_t>(size - 2) << 16);
    cmdbuf[1] = 0x5u;
    std::memset(cmdbuf + 2, 0, (size - 2) * sizeof(uint32_t));
    return 0;
}

static int32_t write_shader_packet(uint32_t* cmdbuf, uint32_t size, uint32_t required_dwords,
                                  uint32_t stage_tag, const void* shader, uint32_t modifier) {
    (void)modifier;
    if (!cmdbuf || size < required_dwords ||
        !gravity::hle::guest_memory::accessible(cmdbuf, required_dwords * sizeof(uint32_t), true)) {
        return -1;
    }
    cmdbuf[0] = 0xC0001000u | (static_cast<uint32_t>(required_dwords - 2) << 16);
    cmdbuf[1] = stage_tag;
    const uint32_t payload_words = required_dwords - 2;
    if (shader && gravity::hle::guest_memory::accessible(shader, payload_words * sizeof(uint32_t), false)) {
        std::memcpy(cmdbuf + 2, shader, payload_words * sizeof(uint32_t));
    } else {
        std::memset(cmdbuf + 2, 0, payload_words * sizeof(uint32_t));
    }
    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmSetPsShader(uint32_t* cmdbuf, uint32_t size, const void* ps_shader) {
    if (ps_shader) {
        g_gnmRenderer.setPsShader(reinterpret_cast<uint64_t>(ps_shader));
    }
    return write_shader_packet(cmdbuf, size, 40, 0x5053u, ps_shader, 0);
}

GRAVITY_SYSV_ABI int32_t sceGnmUpdatePsShader(uint32_t* cmdbuf, uint32_t size, const void* ps_shader) {
    if (ps_shader) {
        g_gnmRenderer.setPsShader(reinterpret_cast<uint64_t>(ps_shader));
    }
    return write_shader_packet(cmdbuf, size, 40, 0x5053u, ps_shader, 0);
}

GRAVITY_SYSV_ABI int32_t sceGnmSetVsShader(uint32_t* cmdbuf, uint32_t size, const void* vs_shader, uint32_t shader_modifier) {
    if (vs_shader) {
        g_gnmRenderer.setVsShader(reinterpret_cast<uint64_t>(vs_shader));
    }
    return write_shader_packet(cmdbuf, size, 29, 0x5653u, vs_shader, shader_modifier);
}

GRAVITY_SYSV_ABI int32_t sceGnmUpdateVsShader(uint32_t* cmdbuf, uint32_t size, const void* vs_shader, uint32_t shader_modifier) {
    if (vs_shader) {
        g_gnmRenderer.setVsShader(reinterpret_cast<uint64_t>(vs_shader));
    }
    return write_shader_packet(cmdbuf, size, 29, 0x5653u, vs_shader, shader_modifier);
}

GRAVITY_SYSV_ABI int32_t sceGnmSetEsShader(uint32_t* cmdbuf, uint32_t size, const void* es_shader, uint32_t shader_modifier) {
    return write_shader_packet(cmdbuf, size, 20, 0x4553u, es_shader, shader_modifier);
}

GRAVITY_SYSV_ABI int32_t sceGnmSetHsShader(uint32_t* cmdbuf, uint32_t size, const void* hs_shader, uint32_t shader_modifier) {
    return write_shader_packet(cmdbuf, size, 30, 0x4853u, hs_shader, shader_modifier);
}

GRAVITY_SYSV_ABI int32_t sceGnmUpdateHsShader(uint32_t* cmdbuf, uint32_t size, const void* hs_shader, uint32_t shader_modifier) {
    return write_shader_packet(cmdbuf, size, 30, 0x4853u, hs_shader, shader_modifier);
}

GRAVITY_SYSV_ABI int32_t sceGnmSetLsShader(uint32_t* cmdbuf, uint32_t size, const void* ls_shader, uint32_t shader_modifier) {
    return write_shader_packet(cmdbuf, size, 23, 0x4C53u, ls_shader, shader_modifier);
}

GRAVITY_SYSV_ABI int32_t sceGnmSetCsShader(uint32_t* cmdbuf, uint32_t size, const void* cs_shader) {
    return write_shader_packet(cmdbuf, size, 25, 0x4353u, cs_shader, 0);
}

GRAVITY_SYSV_ABI int32_t sceGnmSetGsShader(uint32_t* cmdbuf, uint32_t size, const void* gs_shader) {
    return write_shader_packet(cmdbuf, size, 29, 0x4753u, gs_shader, 0);
}

GRAVITY_SYSV_ABI int32_t sceGnmUpdateGsShader(uint32_t* cmdbuf, uint32_t size, const void* gs_shader) {
    return write_shader_packet(cmdbuf, size, 29, 0x4753u, gs_shader, 0);
}

GRAVITY_SYSV_ABI int32_t sceGnmIsUserPaEnabled() {
    return 0;
}

GRAVITY_SYSV_ABI int32_t sceGnmDriverCaptureInProgress() {
    return 0;
}

GRAVITY_SYSV_ABI void sceGnmDriverTriggerCapture() {
}

GRAVITY_SYSV_ABI void sceGnmDebugHardwareStatus(void* status, uint32_t size) {
    if (status && size && gravity::hle::guest_memory::accessible(status, size, true)) {
        std::memset(status, 0, size);
    }
}

GRAVITY_SYSV_ABI int32_t sceGnmValidateCommandBuffers(
    uint32_t count, const uint32_t* const* dcb_addresses, const uint32_t* dcb_sizes,
    const uint32_t* const* ccb_addresses, const uint32_t* ccb_sizes) {
    (void)count; (void)dcb_addresses; (void)dcb_sizes; (void)ccb_addresses; (void)ccb_sizes;
    return 0;
}

GRAVITY_SYSV_ABI uint64_t sceGnmGetTheTessellationFactorRingBufferBaseAddress() {
    return 0;
}

GRAVITY_SYSV_ABI uint32_t sceGnmLogicalCuIndexToPhysicalCuIndex(uint32_t logical_cu_index) {
    return logical_cu_index;
}

GRAVITY_SYSV_ABI uint32_t sceGnmLogicalCuMaskToPhysicalCuMask(uint32_t logical_cu_mask) {
    return logical_cu_mask;
}

GRAVITY_SYSV_ABI void sceGnmDingDong(uint32_t queue_id, uint32_t ring_offset) {
    (void)queue_id; (void)ring_offset;
}

GRAVITY_SYSV_ABI void sceGnmDisableMipStatsReport() {
}

GRAVITY_SYSV_ABI void sceGnmRequestMipStatsReportAndReset() {
}

GRAVITY_SYSV_ABI void sceGnmSetupMipStatsReport() {
}

} // extern "C"

} // namespace gravity::hle::gnm
