#include "hle/avplayer/video_texture.h"
#include <cstring>
#include <limits>

namespace gravity::hle::avplayer {
namespace {
std::uint32_t memory_type(VkPhysicalDevice physical, std::uint32_t mask, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((mask & (1U << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}
}
bool VideoTexture::initialize(VkDevice device, VkPhysicalDevice physical, std::uint32_t width, std::uint32_t height) {
    reset();
    if (!device || !physical || !width || !height || width > 8192 || height > 8192) return false;
    device_ = device; width_ = width; height_ = height;
    auto fail = [this] { reset(); return false; };
    VkImageCreateInfo image{}; image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {width, height, 1}; image.mipLevels = 1; image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(device_, &image, nullptr, &image_) != VK_SUCCESS) return fail();
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, image_, &requirements);
    VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device_, &allocation, nullptr, &image_memory_) != VK_SUCCESS ||
        vkBindImageMemory(device_, image_, image_memory_, 0) != VK_SUCCESS) return fail();
    VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = image_; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &view, nullptr, &view_) != VK_SUCCESS) return fail();
    VkBufferCreateInfo buffer{}; buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer.size = VkDeviceSize(width) * height * 4; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(device_, &buffer, nullptr, &staging_) != VK_SUCCESS) return fail();
    vkGetBufferMemoryRequirements(device_, staging_, &requirements);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocation.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device_, &allocation, nullptr, &staging_memory_) != VK_SUCCESS ||
        vkBindBufferMemory(device_, staging_, staging_memory_, 0) != VK_SUCCESS ||
        vkMapMemory(device_, staging_memory_, 0, VK_WHOLE_SIZE, 0, &mapped_) != VK_SUCCESS) return fail();
    return true;
}
bool VideoTexture::upload(VkCommandBuffer command, std::span<const std::uint8_t> rgba) {
    if (!command || !mapped_ || rgba.size() != std::size_t(width_) * height_ * 4) return false;
    std::memcpy(mapped_, rgba.data(), rgba.size());
    VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = uploaded_ ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = uploaded_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, uploaded_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width_, height_, 1};
    vkCmdCopyBufferToImage(command, staging_, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    uploaded_ = true; return true;
}
void VideoTexture::reset() {
    if (device_) {
        if (mapped_) vkUnmapMemory(device_, staging_memory_);
        if (staging_) vkDestroyBuffer(device_, staging_, nullptr);
        if (staging_memory_) vkFreeMemory(device_, staging_memory_, nullptr);
        if (view_) vkDestroyImageView(device_, view_, nullptr);
        if (image_) vkDestroyImage(device_, image_, nullptr);
        if (image_memory_) vkFreeMemory(device_, image_memory_, nullptr);
    }
    device_ = {}; image_ = {}; view_ = {}; image_memory_ = {}; staging_memory_ = {}; staging_ = {};
    mapped_ = nullptr; width_ = height_ = 0; uploaded_ = false;
}
}
