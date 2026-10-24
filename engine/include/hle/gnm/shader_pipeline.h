#pragma once

#include "hle/gnm/command_queue.h"
#include <vulkan/vulkan.h>

namespace gravity::hle::gnm {

class VulkanShaderPipeline {
public:
    static VulkanShaderPipeline& instance();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM);
    void shutdown();
    bool is_ready() const noexcept;

    DrawResolver get_resolver();

    VkPipeline get_pipeline() const noexcept { return pipeline_; }
    VkPipelineLayout get_layout() const noexcept { return layout_; }

private:
    VulkanShaderPipeline() = default;
    ~VulkanShaderPipeline() { shutdown(); }

    VkDevice device_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkShaderModule vert_module_{VK_NULL_HANDLE};
    VkShaderModule frag_module_{VK_NULL_HANDLE};

    VkBuffer vertex_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory vertex_memory_{VK_NULL_HANDLE};
    VkDeviceSize vertex_buffer_size_{0};

    VkBuffer index_buffer_{VK_NULL_HANDLE};
    VkDeviceMemory index_memory_{VK_NULL_HANDLE};
    VkDeviceSize index_buffer_size_{0};

    bool create_buffer(VkPhysicalDevice phys, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory);
    std::uint32_t find_memory_type(VkPhysicalDevice phys, std::uint32_t typeFilter, VkMemoryPropertyFlags properties);
};

} // namespace gravity::hle::gnm
