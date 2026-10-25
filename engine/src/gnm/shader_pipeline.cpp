#include "hle/gnm/shader_pipeline.h"
#include "hle/guest_memory.h"
#include "hle/avplayer/avplayer.h"
#include "hle/gnm/gcn_decoder.h"
#include "hle/gnm/gcn_translator.h"
#include "hle/gnm/spirv_validation.h"
#include "hle/gnm/texture_decoder.h"
#include <atomic>
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace gravity::hle::gnm {

namespace {

#include "spv_arrays.inc"

struct DefaultVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

const DefaultVertex kDefaultQuad[4] = {
    { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f },
    {  1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f },
    { -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f },
    {  1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f }
};

} // anonymous namespace

VulkanShaderPipeline& VulkanShaderPipeline::instance() {
    static VulkanShaderPipeline inst;
    return inst;
}

std::uint32_t VulkanShaderPipeline::find_memory_type(VkPhysicalDevice phys, std::uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(phys, &memProperties);

    for (std::uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VulkanShaderPipeline::create_buffer(VkPhysicalDevice phys, VkDeviceSize size, VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory) {
    if (device_ == VK_NULL_HANDLE || size == 0) return false;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = find_memory_type(phys, memReq.memoryTypeBits, props);

    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device_, buffer, nullptr); buffer = VK_NULL_HANDLE; return false;
    }

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindBufferMemory(device_, buffer, memory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr); vkFreeMemory(device_, memory, nullptr);
        buffer = VK_NULL_HANDLE; memory = VK_NULL_HANDLE; return false;
    }
    return true;
}

bool VulkanShaderPipeline::transition_uploaded_texture(VkImage image) {
    if (!upload_queue_ || !upload_command_ || !upload_fence_) return false;
    if (vkResetCommandBuffer(upload_command_, 0) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(upload_command_, &begin) != VK_SUCCESS) return false;
    VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(upload_command_, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    if (vkEndCommandBuffer(upload_command_) != VK_SUCCESS || vkResetFences(device_, 1, &upload_fence_) != VK_SUCCESS) return false;
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &upload_command_;
    if (vkQueueSubmit(upload_queue_, 1, &submit, upload_fence_) != VK_SUCCESS) return false;
    // Static textures upload once. Serialize with the renderer's queue access;
    // dynamic video and per-draw resources follow their own frame fences.
    return vkWaitForFences(device_, 1, &upload_fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

bool VulkanShaderPipeline::create_linear_texture(uint32_t width, uint32_t height, VkFormat format,
                                                const void* pixels, CachedTexture& out) {
    if (!device_ || !physical_device_ || !desc_pool_ || !default_sampler_ || !width || !height ||
        width > 16384 || height > 16384) return false;
    VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(physical_device_, format, &properties);
    if (!(properties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return false;

    VkImageCreateInfo img_ci{};
    img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = format;
    img_ci.extent = { width, height, 1 };
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_LINEAR;
    img_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    if (vkCreateImage(device_, &img_ci, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, out.image, &req);

    VkMemoryAllocateInfo alloc_ci{};
    alloc_ci.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_ci.allocationSize = req.size;
    alloc_ci.memoryTypeIndex = find_memory_type(physical_device_, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (alloc_ci.memoryTypeIndex == UINT32_MAX) {
        vkDestroyImage(device_, out.image, nullptr); out.image = VK_NULL_HANDLE; return false;
    }

    if (vkAllocateMemory(device_, &alloc_ci, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyImage(device_, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(device_, out.image, out.memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device_, out.memory, nullptr);
        vkDestroyImage(device_, out.image, nullptr);
        out.memory = VK_NULL_HANDLE;
        out.image = VK_NULL_HANDLE;
        return false;
    }

    size_t bpp = 4;
    if (format == VK_FORMAT_R8_UNORM) bpp = 1;
    else if (format == VK_FORMAT_R8G8_UNORM || format == VK_FORMAT_R16_UNORM) bpp = 2;
    else if (format == VK_FORMAT_R16G16B16A16_SFLOAT) bpp = 8;
    else if (format == VK_FORMAT_R32G32B32A32_SFLOAT) bpp = 16;
    const size_t row_bytes = static_cast<size_t>(width) * bpp;
    VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout{}; vkGetImageSubresourceLayout(device_, out.image, &subresource, &layout);
    void* mapped = nullptr;
    if (layout.rowPitch < row_bytes || layout.offset > req.size ||
        layout.rowPitch * (height - 1) + row_bytes > req.size - layout.offset ||
        vkMapMemory(device_, out.memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        vkDestroyImage(device_, out.image, nullptr); vkFreeMemory(device_, out.memory, nullptr);
        out.image = VK_NULL_HANDLE; out.memory = VK_NULL_HANDLE; return false;
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        auto* target = static_cast<std::uint8_t*>(mapped) + layout.offset + y * layout.rowPitch;
        if (pixels) std::memcpy(target, static_cast<const std::uint8_t*>(pixels) + y * row_bytes, row_bytes);
        else std::memset(target, 0xFF, row_bytes);
    }
    vkUnmapMemory(device_, out.memory);
    if (!transition_uploaded_texture(out.image)) {
        vkDestroyImage(device_, out.image, nullptr); vkFreeMemory(device_, out.memory, nullptr);
        out.image = VK_NULL_HANDLE; out.memory = VK_NULL_HANDLE; return false;
    }

    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = out.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(device_, &view_ci, nullptr, &out.view) != VK_SUCCESS) {
        vkDestroyImage(device_, out.image, nullptr);
        vkFreeMemory(device_, out.memory, nullptr);
        out.memory = VK_NULL_HANDLE;
        out.image = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorSetAllocateInfo ds_ai{};
    ds_ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ds_ai.descriptorPool = desc_pool_;
    ds_ai.descriptorSetCount = 1;
    ds_ai.pSetLayouts = &desc_layout_;

    if (vkAllocateDescriptorSets(device_, &ds_ai, &out.descriptor_set) != VK_SUCCESS) {
        vkDestroyImageView(device_, out.view, nullptr);
        vkDestroyImage(device_, out.image, nullptr);
        vkFreeMemory(device_, out.memory, nullptr);
        out.view = VK_NULL_HANDLE;
        out.memory = VK_NULL_HANDLE;
        out.image = VK_NULL_HANDLE;
        return false;
    }

    VkDescriptorImageInfo image_info{};
    image_info.sampler = default_sampler_;
    image_info.imageView = out.view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = out.descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    out.width = width;
    out.height = height;
    return true;
}

VkDescriptorSet VulkanShaderPipeline::get_or_create_texture(uint64_t guest_addr, uint32_t width, uint32_t height, uint32_t format, uint32_t tiling) {
    if (!guest_addr || !width || !height) return default_descriptor_set_;
    if (width > 16384 || height > 16384) return VK_NULL_HANDLE;

    if (video_slot_ < video_slots_.size()) {
        const auto video = video_slots_[video_slot_].find(guest_addr);
        if (video != video_slots_[video_slot_].end()) {
            const auto& entry = video->second;
            return entry.texture && entry.texture->width() == width && entry.texture->height() == height
                ? entry.descriptor : default_descriptor_set_;
        }
    }

    auto it = texture_cache_.find(guest_addr);
    if (it != texture_cache_.end()) {
        if (it->second.width != width || it->second.height != height || it->second.format != format)
            return VK_NULL_HANDLE; // old descriptor may still be referenced by an in-flight frame
        return it->second.descriptor_set;
    }

    const auto fmt_info = decode_gcn_format(format);
    const size_t raw_byte_count = static_cast<size_t>((width + fmt_info.block_dim - 1) / fmt_info.block_dim) *
                                  ((height + fmt_info.block_dim - 1) / fmt_info.block_dim) * fmt_info.bytes_per_block;

    if (!guest_memory::accessible(reinterpret_cast<const void*>(guest_addr), raw_byte_count, false)) {
        return default_descriptor_set_;
    }

    std::vector<uint8_t> linear_data;
    const void* upload_pixels = reinterpret_cast<const void*>(guest_addr);
    const TextureTiling tile_mode = (tiling == 0) ? TextureTiling::Linear : TextureTiling::Tiled2D;

    if (tile_mode != TextureTiling::Linear) {
        linear_data.resize(raw_byte_count);
        if (!detile_surface(upload_pixels, linear_data.data(), width, height, fmt_info, tile_mode)) return VK_NULL_HANDLE;
        upload_pixels = linear_data.data();
    }

    VkFormat upload_format = fmt_info.vk_format;
    std::vector<uint8_t> decompressed_data;

    if (fmt_info.is_block_compressed) {
        const size_t rgba_bytes = static_cast<size_t>(width) * height * 4;
        decompressed_data.resize(rgba_bytes);
        if (!decompress_bc_to_rgba8(upload_pixels, decompressed_data.data(), width, height, fmt_info.vk_format)) return VK_NULL_HANDLE;
        upload_pixels = decompressed_data.data(); upload_format = VK_FORMAT_R8G8B8A8_UNORM;
    }

    CachedTexture tex{};
    tex.guest_addr = guest_addr;
    tex.format = format;

    if (!create_linear_texture(width, height, upload_format, upload_pixels, tex)) {
        return default_descriptor_set_;
    }

    VkDescriptorSet ds = tex.descriptor_set;
    texture_cache_.emplace(guest_addr, std::move(tex));
    return ds;
}

bool VulkanShaderPipeline::prepare_video_textures(VkCommandBuffer command, std::uint32_t slot, std::uint32_t count) {
    if (!command || !device_ || !count || slot >= count) return false;
    if (video_slots_.empty()) video_slots_.resize(count);
    if (video_slots_.size() != count) return false; // resize requires an idle device
    video_slot_ = slot;
    auto& entries = video_slots_[slot];
    const auto frames = avplayer::video_texture_frames();
    for (auto it = entries.begin(); it != entries.end();) {
        if (std::none_of(frames.begin(), frames.end(), [&](const auto& frame) { return frame.address == it->first; })) {
            if (it->second.descriptor) vkFreeDescriptorSets(device_, desc_pool_, 1, &it->second.descriptor);
            it = entries.erase(it);
        } else ++it;
    }
    for (const auto& frame : frames) {
        auto& entry = entries[frame.address];
        if (!entry.texture || entry.texture->width() != frame.width || entry.texture->height() != frame.height) {
            if (entry.descriptor) vkFreeDescriptorSets(device_, desc_pool_, 1, &entry.descriptor);
            entry = {};
            entry.texture = std::make_unique<avplayer::VideoTexture>();
            if (!entry.texture->initialize(device_, physical_device_, frame.width, frame.height)) return false;
            VkDescriptorSetAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocation.descriptorPool = desc_pool_; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &desc_layout_;
            if (vkAllocateDescriptorSets(device_, &allocation, &entry.descriptor) != VK_SUCCESS) return false;
            VkDescriptorImageInfo image{default_sampler_, entry.texture->view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet write{}; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = entry.descriptor; write.dstBinding = 0; write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
        if (entry.generation != frame.generation) {
            if (!frame.rgba || !entry.texture->upload(command, *frame.rgba)) return false;
            entry.generation = frame.generation;
        }
    }
    return true;
}

void VulkanShaderPipeline::reset_video_textures() {
    for (auto& slot : video_slots_) for (auto& [address, entry] : slot) {
        (void)address;
        if (entry.descriptor && device_ && desc_pool_) vkFreeDescriptorSets(device_, desc_pool_, 1, &entry.descriptor);
    }
    video_slots_.clear(); video_slot_ = 0;
}

bool VulkanShaderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat,
                                      VkQueue uploadQueue, std::uint32_t queueFamily) {
    if (!device || !physicalDevice || !uploadQueue || queueFamily == UINT32_MAX) return false;
    shutdown();
    device_ = device;
    physical_device_ = physicalDevice;
    upload_queue_ = uploadQueue;
    VkCommandPoolCreateInfo upload_pool{}; upload_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    upload_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; upload_pool.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device_, &upload_pool, nullptr, &upload_pool_) != VK_SUCCESS) { shutdown(); return false; }
    VkCommandBufferAllocateInfo upload_command{}; upload_command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    upload_command.commandPool = upload_pool_; upload_command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    upload_command.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &upload_command, &upload_command_) != VK_SUCCESS) { shutdown(); return false; }
    VkFenceCreateInfo upload_fence{}; upload_fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_, &upload_fence, nullptr, &upload_fence_) != VK_SUCCESS) { shutdown(); return false; }

    // 1. Create Vertex Shader Module
    VkShaderModuleCreateInfo vert_ci{};
    vert_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vert_ci.codeSize = sizeof(kVertexShaderSpirv);
    vert_ci.pCode = kVertexShaderSpirv;
    if (vkCreateShaderModule(device_, &vert_ci, nullptr, &vert_module_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 2. Create Fragment Shader Module
    VkShaderModuleCreateInfo frag_ci{};
    frag_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    frag_ci.codeSize = sizeof(kFragmentShaderSpirv);
    frag_ci.pCode = kFragmentShaderSpirv;
    if (vkCreateShaderModule(device_, &frag_ci, nullptr, &frag_module_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 3. Create Descriptor Set Layout
    VkDescriptorSetLayoutBinding dsl_binding{};
    dsl_binding.binding = 0;
    dsl_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dsl_binding.descriptorCount = 1;
    dsl_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dsl_ci{};
    dsl_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl_ci.bindingCount = 1;
    dsl_ci.pBindings = &dsl_binding;
    if (vkCreateDescriptorSetLayout(device_, &dsl_ci, nullptr, &desc_layout_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 4. Create Descriptor Pool
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 128;

    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes = &pool_size;
    pool_ci.maxSets = 128;
    if (vkCreateDescriptorPool(device_, &pool_ci, nullptr, &desc_pool_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 5. Create Default Sampler
    VkSamplerCreateInfo sampler_ci{};
    sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.maxAnisotropy = 1.0f;
    if (vkCreateSampler(device_, &sampler_ci, nullptr, &default_sampler_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 6. Create Default 1x1 White Texture
    CachedTexture white_tex{};
    const uint32_t white_pixel = 0xFFFFFFFF;
    if (!create_linear_texture(1, 1, VK_FORMAT_R8G8B8A8_UNORM, &white_pixel, white_tex)) {
        shutdown();
        return false;
    }
    default_image_ = white_tex.image;
    default_image_memory_ = white_tex.memory;
    default_image_view_ = white_tex.view;
    default_descriptor_set_ = white_tex.descriptor_set;

    // 7. Create Pipeline Layout
    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &desc_layout_;
    layout_ci.pushConstantRangeCount = 0;
    if (vkCreatePipelineLayout(device_, &layout_ci, nullptr, &layout_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 8. Create Buffers (256KB default scratch vertex & index buffers)
    vertex_buffer_size_ = 262144;
    index_buffer_size_ = 262144;
    if (!create_buffer(physicalDevice, vertex_buffer_size_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       vertex_buffer_, vertex_memory_)) {
        shutdown();
        return false;
    }

    if (!create_buffer(physicalDevice, index_buffer_size_, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       index_buffer_, index_memory_)) {
        shutdown();
        return false;
    }

    // Initialize vertex buffer with standard quad
    void* vmapped = nullptr;
    if (vkMapMemory(device_, vertex_memory_, 0, sizeof(kDefaultQuad), 0, &vmapped) == VK_SUCCESS) {
        std::memcpy(vmapped, kDefaultQuad, sizeof(kDefaultQuad));
        vkUnmapMemory(device_, vertex_memory_);
    }

    // 9. Build Graphics Pipeline with Dynamic Rendering
    color_format_ = colorFormat;
    pipeline_ = create_pipeline_for_shaders(vert_module_, frag_module_, layout_);
    if (pipeline_ == VK_NULL_HANDLE) {
        shutdown();
        return false;
    }

    return true;
}

VkPipeline VulkanShaderPipeline::create_pipeline_for_shaders(VkShaderModule vs, VkShaderModule ps, VkPipelineLayout layout) {
    if (!device_ || vs == VK_NULL_HANDLE || ps == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName = "main";

    // Vertex input description
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 36; // vec3 pos (12) + vec2 uv (8) + vec4 col (16)
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;

    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = 12;

    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset = 20;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    ia.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = nullptr; // Dynamic
    vp.scissorCount = 1;
    vp.pScissors = nullptr;   // Dynamic

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.sampleShadingEnable = VK_FALSE;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cb_attach{};
    cb_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb_attach.blendEnable = VK_TRUE;
    cb_attach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb_attach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb_attach.colorBlendOp = VK_BLEND_OP_ADD;
    cb_attach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb_attach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cb_attach.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cb_attach;

    VkPipelineDepthStencilStateCreateInfo dss{};
    dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dss.depthTestEnable = VK_FALSE;
    dss.depthWriteEnable = VK_FALSE;
    dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    dss.depthBoundsTestEnable = VK_FALSE;
    dss.stencilTestEnable = VK_FALSE;

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP
    };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 6;
    ds.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering_ci{};
    rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_ci.colorAttachmentCount = 1;
    rendering_ci.pColorAttachmentFormats = &color_format_;

    VkGraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_ci.pNext = &rendering_ci;
    pipeline_ci.stageCount = 2;
    pipeline_ci.pStages = stages;
    pipeline_ci.pVertexInputState = &vi;
    pipeline_ci.pInputAssemblyState = &ia;
    pipeline_ci.pViewportState = &vp;
    pipeline_ci.pRasterizationState = &rs;
    pipeline_ci.pMultisampleState = &ms;
    pipeline_ci.pDepthStencilState = &dss;
    pipeline_ci.pColorBlendState = &cb;
    pipeline_ci.pDynamicState = &ds;
    pipeline_ci.layout = layout;
    pipeline_ci.renderPass = VK_NULL_HANDLE;

    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipe) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipe;
}

VulkanShaderPipeline::DynamicPipeline* VulkanShaderPipeline::get_or_create_dynamic_pipeline(uint64_t vs_addr, uint64_t ps_addr) {
    if (!device_ || !vs_addr || !ps_addr) return nullptr;

    auto fetch_or_translate = [this](uint64_t addr, gcn::Stage stage) -> std::vector<uint32_t> {
        std::vector<uint32_t> gcn_words;
        constexpr size_t max_words = 4096;
        for (size_t i = 0; i < max_words; ++i) {
            uint64_t ptr = addr + i * sizeof(uint32_t);
            if (!guest_memory::accessible(reinterpret_cast<const void*>(ptr), sizeof(uint32_t), false)) {
                break;
            }
            uint32_t w = *reinterpret_cast<const uint32_t*>(ptr);
            gcn_words.push_back(w);
            if (w == 0xbf810000u) {
                const auto decoded = gcn::decode(gcn_words);
                // A literal/second word can equal END. Only stop at a decoded
                // instruction boundary, never inside an operand.
                if (decoded.diagnostic && decoded.word_count == gcn_words.size()) break;
            }
        }
        if (gcn_words.empty() || gcn_words.back() != 0xbf810000u) {
            return {};
        }

        gcn::TranslateConfig cfg;
        cfg.stage = stage;
        cfg.num_sgprs = 32;
        cfg.num_vgprs = 32;
        cfg.num_textures = stage == gcn::Stage::Fragment ? 1 : 0;
        std::span<const uint8_t> byte_span(reinterpret_cast<const uint8_t*>(gcn_words.data()), gcn_words.size() * sizeof(uint32_t));
        const std::string cache_key = ShaderCache::key_for(byte_span, gcn::cache_identity(cfg));
        if (!cache_key.empty()) {
            auto cached = shader_cache_.load(cache_key);
            std::string error;
            if (cached && validate_spirv(*cached, error)) {
                return std::move(*cached);
            }
        }

        auto program = gcn::decode(gcn_words);
        if (!program.diagnostic) {
            return {};
        }

        auto result = gcn::translate(program, cfg);
        if (!result.success || result.spirv.empty()) {
            static std::atomic<unsigned> rejected{};
            if (rejected.fetch_add(1) < 8) std::fprintf(stderr, "[GCN] Rejected shader 0x%llx: %s\n",
                static_cast<unsigned long long>(addr), result.error.c_str());
            return {};
        }
        std::string error;
        if (!validate_spirv(result.spirv, error)) return {};

        if (!cache_key.empty()) {
            (void)shader_cache_.store(cache_key, result.spirv);
        }
        return result.spirv;
    };

    auto vs_spv = fetch_or_translate(vs_addr, gcn::Stage::Vertex);
    auto ps_spv = fetch_or_translate(ps_addr, gcn::Stage::Fragment);
    if (vs_spv.empty() || ps_spv.empty()) {
        return nullptr;
    }

    std::vector<std::uint32_t> pipeline_code(vs_spv);
    pipeline_code.insert(pipeline_code.end(), ps_spv.begin(), ps_spv.end());
    const auto key = ShaderCache::key_for(std::as_bytes(std::span(pipeline_code)),
        "pipeline-v2:vswords=" + std::to_string(vs_spv.size()) + ":color=" + std::to_string(color_format_));
    if (key.empty()) return nullptr;
    const auto existing = dynamic_pipelines_.find(key);
    if (existing != dynamic_pipelines_.end()) return &existing->second;

    VkShaderModuleCreateInfo vs_ci{};
    vs_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vs_ci.codeSize = vs_spv.size() * sizeof(uint32_t);
    vs_ci.pCode = vs_spv.data();
    VkShaderModule vs_mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &vs_ci, nullptr, &vs_mod) != VK_SUCCESS) {
        return nullptr;
    }

    VkShaderModuleCreateInfo ps_ci{};
    ps_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ps_ci.codeSize = ps_spv.size() * sizeof(uint32_t);
    ps_ci.pCode = ps_spv.data();
    VkShaderModule ps_mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &ps_ci, nullptr, &ps_mod) != VK_SUCCESS) {
        vkDestroyShaderModule(device_, vs_mod, nullptr);
        return nullptr;
    }

    VkPipeline pipe = create_pipeline_for_shaders(vs_mod, ps_mod, layout_);
    if (pipe == VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, ps_mod, nullptr);
        vkDestroyShaderModule(device_, vs_mod, nullptr);
        return nullptr;
    }

    DynamicPipeline dyn;
    dyn.pipeline = pipe;
    dyn.layout = layout_;
    dyn.vs_module = vs_mod;
    dyn.ps_module = ps_mod;

    auto [emplaced_it, _] = dynamic_pipelines_.emplace(key, dyn);
    return &emplaced_it->second;
}


void VulkanShaderPipeline::shutdown() {
    reset_video_textures();
    if (device_ != VK_NULL_HANDLE) {
        for (auto& [key, pipe] : dynamic_pipelines_) {
            (void)key;
            if (pipe.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipe.pipeline, nullptr);
            if (pipe.vs_module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, pipe.vs_module, nullptr);
            if (pipe.ps_module != VK_NULL_HANDLE) vkDestroyShaderModule(device_, pipe.ps_module, nullptr);
        }
        dynamic_pipelines_.clear();

        for (auto& [addr, tex] : texture_cache_) {
            (void)addr;
            if (tex.view != VK_NULL_HANDLE) vkDestroyImageView(device_, tex.view, nullptr);
            if (tex.image != VK_NULL_HANDLE) vkDestroyImage(device_, tex.image, nullptr);
            if (tex.memory != VK_NULL_HANDLE) vkFreeMemory(device_, tex.memory, nullptr);
        }
        texture_cache_.clear();

        if (default_image_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, default_image_view_, nullptr);
            default_image_view_ = VK_NULL_HANDLE;
        }
        if (default_image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, default_image_, nullptr);
            default_image_ = VK_NULL_HANDLE;
        }
        if (default_image_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, default_image_memory_, nullptr);
            default_image_memory_ = VK_NULL_HANDLE;
        }
        if (default_sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, default_sampler_, nullptr);
            default_sampler_ = VK_NULL_HANDLE;
        }
        if (desc_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
            desc_pool_ = VK_NULL_HANDLE;
        }
        if (desc_layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
            desc_layout_ = VK_NULL_HANDLE;
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
        if (vert_module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, vert_module_, nullptr);
            vert_module_ = VK_NULL_HANDLE;
        }
        if (frag_module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, frag_module_, nullptr);
            frag_module_ = VK_NULL_HANDLE;
        }
        if (vertex_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, vertex_buffer_, nullptr);
            vertex_buffer_ = VK_NULL_HANDLE;
        }
        if (vertex_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, vertex_memory_, nullptr);
            vertex_memory_ = VK_NULL_HANDLE;
        }
        if (index_buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, index_buffer_, nullptr);
            index_buffer_ = VK_NULL_HANDLE;
        }
        if (index_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, index_memory_, nullptr);
            index_memory_ = VK_NULL_HANDLE;
        }
        if (upload_fence_) vkDestroyFence(device_, upload_fence_, nullptr);
        if (upload_pool_) vkDestroyCommandPool(device_, upload_pool_, nullptr);
        upload_fence_ = VK_NULL_HANDLE; upload_pool_ = VK_NULL_HANDLE;
        upload_command_ = VK_NULL_HANDLE; upload_queue_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
    }
}

bool VulkanShaderPipeline::is_ready() const noexcept {
    return pipeline_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE;
}

VulkanShaderPipeline::DrawUpload::~DrawUpload() {
    if (!device) return;
    if (vertices) vkDestroyBuffer(device, vertices, nullptr);
    if (indices) vkDestroyBuffer(device, indices, nullptr);
    if (vertex_memory) vkFreeMemory(device, vertex_memory, nullptr);
    if (index_memory) vkFreeMemory(device, index_memory, nullptr);
}

bool VulkanShaderPipeline::upload_draw_buffer(const void* data, VkDeviceSize bytes,
                                               VkBufferUsageFlags usage,
                                               VkBuffer& buffer, VkDeviceMemory& memory) {
    if (!data || !bytes || !device_ || !physical_device_) return false;
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &properties);
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &info, nullptr, &buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    const auto wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = properties.memoryTypeCount;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1U << i)) &&
            (properties.memoryTypes[i].propertyFlags & wanted) == wanted) { type = i; break; }
    }
    if (type == properties.memoryTypeCount) return false;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &allocation, nullptr, &memory) != VK_SUCCESS ||
        vkBindBufferMemory(device_, buffer, memory, 0) != VK_SUCCESS) return false;
    void* mapped{};
    if (vkMapMemory(device_, memory, 0, bytes, 0, &mapped) != VK_SUCCESS) return false;
    std::memcpy(mapped, data, static_cast<size_t>(bytes));
    vkUnmapMemory(device_, memory);
    return true;
}

DrawResolver VulkanShaderPipeline::get_resolver() {
    return [this](const GraphicsState& state, const DrawRequest& request, NativeDraw& native) -> bool {
        if (!pipeline_ || !layout_ || !request.count || !request.instances) return false;
        native = {};

        native.pipeline = pipeline_;
        native.layout = layout_;

        // 1. Dynamic GCN Shader Translation & Pipeline Selection
        uint64_t vs_addr = 0;
        uint64_t ps_addr = 0;
        const uint32_t vs_lo = state.registers[0x2c48];
        const uint32_t vs_hi = state.registers[0x2c49];
        if (vs_lo != 0 || vs_hi != 0) {
            vs_addr = (static_cast<uint64_t>(vs_hi & 0xffu) << 40) | (static_cast<uint64_t>(vs_lo) << 8);
        }
        const uint32_t ps_lo = state.registers[0x2c08];
        const uint32_t ps_hi = state.registers[0x2c09];
        if (ps_lo != 0 || ps_hi != 0) {
            ps_addr = (static_cast<uint64_t>(ps_hi & 0xffu) << 40) | (static_cast<uint64_t>(ps_lo) << 8);
        }

        static std::atomic<unsigned> s_log_draws{0};
        if (s_log_draws.fetch_add(1) < 10) {
            std::fprintf(stderr, "[Resolver] vs_addr=0x%llx, ps_addr=0x%llx, vs_lo=0x%x, ps_lo=0x%x\n",
                         (unsigned long long)vs_addr, (unsigned long long)ps_addr, vs_lo, ps_lo);
        }

        if (vs_addr != 0 || ps_addr != 0) {
            if (!vs_addr || !ps_addr) return false;
            auto* dyn = get_or_create_dynamic_pipeline(vs_addr, ps_addr);
            if (!dyn) return false;
            native.pipeline = dyn->pipeline;
            native.layout = dyn->layout;
        }

        // 2. Texture resolution from SPI_SHADER_USER_DATA_PS (T# descriptors)
        VkDescriptorSet ds = default_descriptor_set_;
        for (uint32_t slot = 0x2c0c; slot + 3 <= 0x2c1b; slot += 4) {
            const uint32_t dw0 = state.registers[slot];
            const uint32_t dw1 = state.registers[slot + 1];
            const uint32_t dw2 = state.registers[slot + 2];
            const uint32_t dw3 = state.registers[slot + 3];
            if (dw0 != 0 && dw2 != 0) {
                uint64_t tex_addr = (static_cast<uint64_t>(dw0) << 8) | (static_cast<uint64_t>(dw1 & 0xFF) << 40);
                uint32_t tex_w = (dw2 & 0x3FFF) + 1;
                uint32_t tex_h = ((dw2 >> 14) & 0x3FFF) + 1;
                uint32_t tex_fmt = (dw1 >> 20) & 0x3F;
                uint32_t tiling = (dw3 >> 16) & 0xF;
                if (tex_addr != 0 && tex_w > 0 && tex_h > 0) {
                    VkDescriptorSet candidate = get_or_create_texture(tex_addr, tex_w, tex_h, tex_fmt, tiling);
                    if (candidate != VK_NULL_HANDLE) {
                        ds = candidate;
                        break;
                    }
                }
            }
        }
        if (ds != VK_NULL_HANDLE) {
            native.descriptors = { ds };
        }

        // Snapshot each draw before Vulkan recording. A later draw (or CPU
        // guest write) must not mutate resources already prepared for this one.
        constexpr uint64_t max_upload_bytes = 64ULL * 1024 * 1024;
        std::vector<std::uint8_t> vertex_data;
        uint64_t vertex_count = 0;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        const uint64_t color_ptr = state.registers[0x2c54];
        float color[4]{};
        if (color_ptr && guest_memory::read(reinterpret_cast<const void*>(color_ptr), color, sizeof(color))) {
            r = color[0]; g = color[1]; b = color[2]; a = color[3];
        }

        for (uint32_t slot = 0x2c4c; slot <= 0x2c5b; ++slot) {
            uint32_t descriptor[4]{};
            if (slot + 3 <= 0x2c5b) {
                std::copy_n(state.registers.data() + slot, 4, descriptor);
            }
            auto supported = [&] {
                const auto stride = (descriptor[1] >> 16) & 0x3fff;
                return descriptor[0] && descriptor[2] && (stride == 8 || stride == sizeof(DefaultVertex));
            };
            // Inline descriptors are checked before interpreting a register as
            // a pointer: the descriptor's base itself often points to readable
            // vertices, which must not be mistaken for another descriptor.
            if (!supported()) {
                const uint64_t pointer = state.registers[slot];
                if (!pointer || !guest_memory::read(reinterpret_cast<const void*>(pointer), descriptor, sizeof(descriptor)))
                    continue;
                if (!supported()) continue;
            }
            const uint64_t address = (uint64_t(descriptor[1] & 0xffff) << 32) | descriptor[0];
            const uint32_t stride = (descriptor[1] >> 16) & 0x3fff;
            const uint64_t records = descriptor[2];
            if (stride == 8) {
                if (records < 4 || !guest_memory::accessible(reinterpret_cast<const void*>(address), 8 * sizeof(float), false))
                    return false;
                float coords[8];
                if (!guest_memory::read(reinterpret_cast<const void*>(address), coords, sizeof(coords))) return false;
                const DefaultVertex quad[4] = {
                    { coords[0], coords[1], 0.0f, 0.0f, 0.0f, r, g, b, a },
                    { coords[2], coords[3], 0.0f, 1.0f, 0.0f, r, g, b, a },
                    { coords[4], coords[5], 0.0f, 0.0f, 1.0f, r, g, b, a },
                    { coords[6], coords[7], 0.0f, 1.0f, 1.0f, r, g, b, a }
                };
                vertex_data.resize(sizeof(quad));
                std::memcpy(vertex_data.data(), quad, sizeof(quad));
                vertex_count = 4;
            } else {
                const uint64_t bytes = records * stride;
                if (bytes > max_upload_bytes ||
                    !guest_memory::accessible(reinterpret_cast<const void*>(address), static_cast<size_t>(bytes), false))
                    return false;
                vertex_data.resize(static_cast<size_t>(bytes));
                if (!guest_memory::read(reinterpret_cast<const void*>(address), vertex_data.data(), vertex_data.size())) return false;
                vertex_count = records;
            }
            break;
        }

        if (vertex_data.empty()) {
            if (request.indexed || request.count != 4) return false;
            DefaultVertex quad[4];
            std::memcpy(quad, kDefaultQuad, sizeof(quad));
            for (auto& vertex : quad) { vertex.r = r; vertex.g = g; vertex.b = b; vertex.a = a; }
            vertex_data.resize(sizeof(quad));
            std::memcpy(vertex_data.data(), quad, sizeof(quad));
            vertex_count = 4;
        }
        if (!request.indexed && request.count > vertex_count) return false;

        std::vector<std::uint8_t> index_data;
        if (request.indexed) {
            const uint64_t element = request.index32 ? 4U : 2U;
            const uint64_t end = uint64_t(request.first_index) + request.count;
            const uint64_t bytes = end * element;
            if (!request.index_address || request.index_address % element || bytes > max_upload_bytes ||
                !guest_memory::accessible(reinterpret_cast<const void*>(request.index_address), static_cast<size_t>(bytes), false))
                return false;
            index_data.resize(static_cast<size_t>(bytes));
            if (!guest_memory::read(reinterpret_cast<const void*>(request.index_address), index_data.data(), index_data.size())) return false;
            for (uint64_t i = request.first_index; i < end; ++i) {
                uint32_t index{};
                if (request.index32) std::memcpy(&index, index_data.data() + i * element, sizeof(index));
                else {
                    uint16_t small{};
                    std::memcpy(&small, index_data.data() + i * element, sizeof(small));
                    index = small;
                }
                if (index >= vertex_count) return false;
            }
        }

        auto upload = std::make_shared<DrawUpload>();
        upload->device = device_;
        if (!upload_draw_buffer(vertex_data.data(), vertex_data.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                upload->vertices, upload->vertex_memory)) return false;
        native.vertex_buffers = { upload->vertices };
        native.vertex_offsets = { 0 };
        if (request.indexed) {
            if (!upload_draw_buffer(index_data.data(), index_data.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    upload->indices, upload->index_memory)) return false;
            native.index_buffer = upload->indices;
            native.index_offset = 0;
            native.index_bytes = index_data.size();
        }
        native.resources = std::move(upload);
        // 6. Dynamic Viewport / Scissor
        native.viewport = std::nullopt;
        native.scissor = std::nullopt;

        // 7. Dynamic Depth / Stencil / Cull states
        native.has_dynamic_depth_cull = true;
        const uint32_t pa_su_sc_mode = state.registers[0xA205];
        const bool cull_front = (pa_su_sc_mode & 1u) != 0;
        const bool cull_back = (pa_su_sc_mode & 2u) != 0;
        if (cull_front && cull_back) {
            native.cull_mode = VK_CULL_MODE_FRONT_AND_BACK;
        } else if (cull_front) {
            native.cull_mode = VK_CULL_MODE_FRONT_BIT;
        } else if (cull_back) {
            native.cull_mode = VK_CULL_MODE_BACK_BIT;
        } else {
            native.cull_mode = VK_CULL_MODE_NONE;
        }

        const uint32_t db_depth_cntl = state.registers[0xA200];
        native.depth_test_enable = ((db_depth_cntl >> 1) & 1u) ? VK_TRUE : VK_FALSE;
        native.depth_write_enable = ((db_depth_cntl >> 2) & 1u) ? VK_TRUE : VK_FALSE;
        const uint32_t zfunc = (db_depth_cntl >> 4) & 7u;
        native.depth_compare_op = (db_depth_cntl != 0) ? static_cast<VkCompareOp>(zfunc) : VK_COMPARE_OP_LESS_OR_EQUAL;

        return true;
    };
}

} // namespace gravity::hle::gnm
