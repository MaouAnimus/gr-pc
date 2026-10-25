#pragma once

#include "hle/gnm/command_queue.h"
#include "hle/avplayer/video_texture.h"
#include "hle/gnm/shader_cache.h"
#include <vulkan/vulkan.h>

#include <unordered_map>
#include <memory>

namespace gravity::hle::gnm {

struct CachedTexture {
    uint64_t guest_addr{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t format{0};
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
};

class VulkanShaderPipeline {
public:
    static VulkanShaderPipeline& instance();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat,
                    VkQueue uploadQueue, std::uint32_t queueFamily);
    void shutdown();
    bool is_ready() const noexcept;

    DrawResolver get_resolver();

    VkPipeline get_pipeline() const noexcept { return pipeline_; }
    VkPipelineLayout get_layout() const noexcept { return layout_; }

    VkDescriptorSet get_or_create_texture(uint64_t guest_addr, uint32_t width, uint32_t height, uint32_t format, uint32_t tiling = 0);
    // Call outside rendering, after the fence for frame_slot has completed.
    bool prepare_video_textures(VkCommandBuffer command, std::uint32_t frame_slot, std::uint32_t slot_count);
    // All submissions using these textures must have completed.
    void reset_video_textures();

private:
    struct DrawUpload {
        VkDevice device{VK_NULL_HANDLE};
        VkBuffer vertices{VK_NULL_HANDLE}, indices{VK_NULL_HANDLE};
        VkDeviceMemory vertex_memory{VK_NULL_HANDLE}, index_memory{VK_NULL_HANDLE};
        ~DrawUpload();
    };
    bool upload_draw_buffer(const void* data, VkDeviceSize bytes, VkBufferUsageFlags usage,
                            VkBuffer& buffer, VkDeviceMemory& memory);

    struct DynamicPipeline {
        VkPipeline pipeline{VK_NULL_HANDLE};
        VkPipelineLayout layout{VK_NULL_HANDLE};
        VkShaderModule vs_module{VK_NULL_HANDLE};
        VkShaderModule ps_module{VK_NULL_HANDLE};
    };

    VulkanShaderPipeline() = default;
    ~VulkanShaderPipeline() { shutdown(); }

    VkDevice device_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkFormat color_format_{VK_FORMAT_B8G8R8A8_UNORM};
    VkQueue upload_queue_{VK_NULL_HANDLE};
    VkCommandPool upload_pool_{VK_NULL_HANDLE};
    VkCommandBuffer upload_command_{VK_NULL_HANDLE};
    VkFence upload_fence_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkShaderModule vert_module_{VK_NULL_HANDLE};
    VkShaderModule frag_module_{VK_NULL_HANDLE};

    VkDescriptorSetLayout desc_layout_{VK_NULL_HANDLE};
    VkDescriptorPool desc_pool_{VK_NULL_HANDLE};
    VkSampler default_sampler_{VK_NULL_HANDLE};

    // Default 1x1 white texture
    VkImage default_image_{VK_NULL_HANDLE};
    VkDeviceMemory default_image_memory_{VK_NULL_HANDLE};
    VkImageView default_image_view_{VK_NULL_HANDLE};
    VkDescriptorSet default_descriptor_set_{VK_NULL_HANDLE};

    VkBuffer vertex_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory vertex_memory_{VK_NULL_HANDLE};
    VkDeviceSize vertex_buffer_size_{0};

    VkBuffer index_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory index_memory_{VK_NULL_HANDLE};
    VkDeviceSize index_buffer_size_{0};

    std::unordered_map<uint64_t, CachedTexture> texture_cache_{};
    std::unordered_map<std::string, DynamicPipeline> dynamic_pipelines_{};
    ShaderCache shader_cache_{".spv_cache"};

    struct VideoEntry {
        std::unique_ptr<avplayer::VideoTexture> texture;
        VkDescriptorSet descriptor{};
        std::uint64_t generation{};
    };
    std::vector<std::unordered_map<std::uint64_t, VideoEntry>> video_slots_;
    std::uint32_t video_slot_{};

    bool create_buffer(VkPhysicalDevice phys, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory);
    std::uint32_t find_memory_type(VkPhysicalDevice phys, std::uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool create_linear_texture(uint32_t width, uint32_t height, VkFormat format, const void* pixels, CachedTexture& out);
    bool transition_uploaded_texture(VkImage image);
    VkPipeline create_pipeline_for_shaders(VkShaderModule vs, VkShaderModule ps, VkPipelineLayout layout);
    DynamicPipeline* get_or_create_dynamic_pipeline(uint64_t vs_addr, uint64_t ps_addr);
};

} // namespace gravity::hle::gnm
