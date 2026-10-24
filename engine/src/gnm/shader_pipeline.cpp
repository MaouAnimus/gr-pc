#include "hle/gnm/shader_pipeline.h"
#include "hle/guest_memory.h"
#include "hle/avplayer/avplayer.h"
#include "hle/gnm/gcn_decoder.h"
#include "hle/gnm/gcn_translator.h"
#include "hle/gnm/texture_decoder.h"
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
    return 0;
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

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device_, buffer, memory, 0);
    return true;
}

bool VulkanShaderPipeline::create_linear_texture(uint32_t width, uint32_t height, VkFormat format,
                                                const void* pixels, CachedTexture& out) {
    if (!device_ || !physical_device_ || !desc_pool_ || !default_sampler_ || !width || !height) return false;

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
    const size_t byte_count = static_cast<size_t>(width) * height * bpp;
    void* mapped = nullptr;
    if (vkMapMemory(device_, out.memory, 0, (std::min)(req.size, static_cast<VkDeviceSize>(byte_count)), 0, &mapped) == VK_SUCCESS) {
        if (pixels) {
            std::memcpy(mapped, pixels, (std::min)(req.size, static_cast<VkDeviceSize>(byte_count)));
        } else {
            std::memset(mapped, 0xFF, (std::min)(req.size, static_cast<VkDeviceSize>(byte_count)));
        }
        vkUnmapMemory(device_, out.memory);
    }

    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = out.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    if (vkCreateImageView(device_, &view_ci, nullptr, &out.view) != VK_SUCCESS) {
        vkFreeMemory(device_, out.memory, nullptr);
        vkDestroyImage(device_, out.image, nullptr);
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
        vkFreeMemory(device_, out.memory, nullptr);
        vkDestroyImage(device_, out.image, nullptr);
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
        if (detile_surface(upload_pixels, linear_data.data(), width, height, fmt_info, tile_mode)) {
            upload_pixels = linear_data.data();
        }
    }

    VkFormat upload_format = fmt_info.vk_format;
    std::vector<uint8_t> decompressed_data;

    if (fmt_info.is_block_compressed) {
        const size_t rgba_bytes = static_cast<size_t>(width) * height * 4;
        decompressed_data.resize(rgba_bytes);
        if (decompress_bc_to_rgba8(upload_pixels, decompressed_data.data(), width, height, fmt_info.vk_format)) {
            upload_pixels = decompressed_data.data();
            upload_format = VK_FORMAT_R8G8B8A8_UNORM;
        }
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

bool VulkanShaderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat) {
    if (device == VK_NULL_HANDLE) return false;
    shutdown();
    device_ = device;
    physical_device_ = physicalDevice;

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

    const uint64_t key = vs_addr ^ (ps_addr << 16);
    auto it = dynamic_pipelines_.find(key);
    if (it != dynamic_pipelines_.end()) {
        return &it->second;
    }

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
                break;
            }
        }
        if (gcn_words.empty() || gcn_words.back() != 0xbf810000u) {
            return {};
        }

        std::span<const uint8_t> byte_span(reinterpret_cast<const uint8_t*>(gcn_words.data()), gcn_words.size() * sizeof(uint32_t));
        std::string cache_key = ShaderCache::key_for(byte_span, stage == gcn::Stage::Vertex ? "vs:v1" : "ps:v1");
        if (!cache_key.empty()) {
            auto cached = shader_cache_.load(cache_key);
            if (cached.has_value() && !cached->empty()) {
                return std::move(*cached);
            }
        }

        auto program = gcn::decode(gcn_words);
        if (!program.diagnostic) {
            return {};
        }

        gcn::TranslateConfig cfg;
        cfg.stage = stage;
        cfg.num_sgprs = 32;
        cfg.num_vgprs = 32;
        cfg.num_textures = 1;
        auto result = gcn::translate(program, cfg);
        if (!result.success || result.spirv.empty()) {
            return {};
        }

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
        device_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
    }
}

bool VulkanShaderPipeline::is_ready() const noexcept {
    return pipeline_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE;
}

DrawResolver VulkanShaderPipeline::get_resolver() {
    return [this](const GraphicsState& state, const DrawRequest& request, NativeDraw& native) -> bool {
        if (!pipeline_ || !layout_) return false;

        native.pipeline = pipeline_;
        native.layout = layout_;

        // 1. Dynamic GCN Shader Translation & Pipeline Selection
        uint64_t vs_addr = 0;
        uint64_t ps_addr = 0;
        const uint32_t vs_lo = state.registers[0x2c48];
        const uint32_t vs_hi = state.registers[0x2c49];
        if (vs_lo != 0 || vs_hi != 0) {
            vs_addr = (static_cast<uint64_t>(vs_hi) << 40) | (static_cast<uint64_t>(vs_lo) << 8);
        }
        const uint32_t ps_lo = state.registers[0x2c08];
        const uint32_t ps_hi = state.registers[0x2c09];
        if (ps_lo != 0 || ps_hi != 0) {
            ps_addr = (static_cast<uint64_t>(ps_hi) << 40) | (static_cast<uint64_t>(ps_lo) << 8);
        }

        if (vs_addr != 0 && ps_addr != 0) {
            auto* dyn = get_or_create_dynamic_pipeline(vs_addr, ps_addr);
            if (dyn) {
                native.pipeline = dyn->pipeline;
                native.layout = dyn->layout;
            }
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

        // 3. Vertex Buffer binding (V# descriptors in SPI_SHADER_USER_DATA_VS)
        bool found_vbuf = false;
        for (uint32_t slot = 0x2c4c; slot + 3 <= 0x2c5b; slot += 4) {
            const uint32_t dw0 = state.registers[slot];
            const uint32_t dw1 = state.registers[slot + 1];
            const uint32_t dw2 = state.registers[slot + 2];
            if (dw0 != 0 && dw2 != 0) {
                uint64_t vbuf_addr = (static_cast<uint64_t>(dw1 & 0xFFFF) << 32) | dw0;
                uint32_t stride = (dw1 >> 16) & 0x3FFF;
                uint32_t num_records = dw2;
                size_t total_bytes = stride > 0 ? static_cast<size_t>(stride) * num_records : static_cast<size_t>(num_records);
                if (vbuf_addr != 0 && total_bytes > 0 && guest_memory::accessible(reinterpret_cast<const void*>(vbuf_addr), (std::min)(total_bytes, static_cast<size_t>(vertex_buffer_size_)), false)) {
                    void* mapped = nullptr;
                    if (vkMapMemory(device_, vertex_memory_, 0, (std::min)(static_cast<VkDeviceSize>(total_bytes), vertex_buffer_size_), 0, &mapped) == VK_SUCCESS) {
                        std::memcpy(mapped, reinterpret_cast<const void*>(vbuf_addr), (std::min)(static_cast<VkDeviceSize>(total_bytes), vertex_buffer_size_));
                        vkUnmapMemory(device_, vertex_memory_);
                        found_vbuf = true;
                        break;
                    }
                }
            }
        }

        native.vertex_buffers = { vertex_buffer_ };
        native.vertex_offsets = { 0 };

        // 4. Dynamic Quad/Color updating fallback if no V# buffer found
        if (!found_vbuf && !request.indexed && request.count == 4) {
            float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            const uint64_t color_ptr = state.registers[0x2c54];
            if (color_ptr && guest_memory::accessible(reinterpret_cast<const void*>(color_ptr), 16, false)) {
                const float* col_f = reinterpret_cast<const float*>(color_ptr);
                r = col_f[0]; g = col_f[1]; b = col_f[2]; a = col_f[3];
            }
            const DefaultVertex quad[4] = {
                { -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  r, g, b, a },
                {  1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  r, g, b, a },
                { -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  r, g, b, a },
                {  1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  r, g, b, a }
            };
            void* mapped = nullptr;
            if (vkMapMemory(device_, vertex_memory_, 0, sizeof(quad), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, quad, sizeof(quad));
                vkUnmapMemory(device_, vertex_memory_);
            }
        }

        // 5. Index Buffer resolution
        if (request.indexed) {
            const auto elem_size = request.index32 ? 4U : 2U;
            const auto req_bytes = static_cast<VkDeviceSize>(request.first_index + request.count) * elem_size;
            if (request.index_address && guest_memory::accessible(reinterpret_cast<const void*>(request.index_address), req_bytes, false)) {
                void* mapped = nullptr;
                if (vkMapMemory(device_, index_memory_, 0, (std::min)(req_bytes, index_buffer_size_), 0, &mapped) == VK_SUCCESS) {
                    std::memcpy(mapped, reinterpret_cast<const void*>(request.index_address), (std::min)(req_bytes, index_buffer_size_));
                    vkUnmapMemory(device_, index_memory_);
                }
            }
            native.index_buffer = index_buffer_;
            native.index_offset = 0;
            native.index_bytes = index_buffer_size_;
        }

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
