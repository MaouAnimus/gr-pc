#include "hle/gnm.h"
#include "hle/kernel.h"
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
    ~GnmRenderer() {
        shutdown();
    }

    hle::Status initialize() {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (initialized_) return {hle::Error::invalid_state};

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
        initialized_ = false;
        device_ = VK_NULL_HANDLE;
    }

    void setDevice(VkDevice dev) {
        std::lock_guard<hle::Mutex> lock(mutex_);
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

    void recordFrame(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (cmd == VK_NULL_HANDLE || targetView == VK_NULL_HANDLE) return;

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

        // Dynamic viewport & scissor
        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = static_cast<float>(extent.width);
        vp.height = static_cast<float>(extent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D sc{};
        sc.offset = {0, 0};
        sc.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &sc);

        // Apply Cel-Shading Inverted Hull DepthBias dynamic state
        if (depth_bias_enabled_) {
            vkCmdSetDepthBias(cmd, depth_bias_constant_, depth_bias_clamp_, depth_bias_slope_);
        }

        vkCmdEndRendering(cmd);
    }

    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }
    [[nodiscard]] uint64_t totalDrawCalls() const noexcept { return total_draw_calls_; }

private:
    hle::Mutex mutex_{};
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

void recordFrame(VkCommandBuffer cmd, VkImageView imageView, VkExtent2D extent) {
    g_gnmRenderer.recordFrame(cmd, imageView, extent);
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

} // extern "C"

} // namespace gravity::hle::gnm