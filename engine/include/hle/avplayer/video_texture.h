#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <span>

namespace gravity::hle::avplayer {
// One instance per in-flight frame slot. The owner must wait for that slot's
// fence before upload/reset; upload records outside a rendering scope.
class VideoTexture {
public:
    VideoTexture() = default;
    ~VideoTexture() { reset(); }
    VideoTexture(const VideoTexture&) = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;
    bool initialize(VkDevice device, VkPhysicalDevice physical, std::uint32_t width, std::uint32_t height);
    bool upload(VkCommandBuffer command, std::span<const std::uint8_t> rgba);
    void reset();
    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
private:
    VkDevice device_{};
    VkImage image_{};
    VkImageView view_{};
    VkDeviceMemory image_memory_{}, staging_memory_{};
    VkBuffer staging_{};
    void* mapped_{};
    std::uint32_t width_{}, height_{};
    bool uploaded_{};
};
}
