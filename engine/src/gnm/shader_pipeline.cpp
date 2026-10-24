#include "hle/gnm/shader_pipeline.h"
#include <array>
#include <vector>
#include <cstring>
#include <iostream>

namespace gravity::hle::gnm {

namespace {

#include "spv_arrays.inc"

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

bool VulkanShaderPipeline::initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat) {
    if (device == VK_NULL_HANDLE) return false;
    shutdown();
    device_ = device;

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

    // 3. Create Pipeline Layout
    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 0;
    layout_ci.pSetLayouts = nullptr;
    layout_ci.pushConstantRangeCount = 0;
    if (vkCreatePipelineLayout(device_, &layout_ci, nullptr, &layout_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    // 4. Create Buffers (64KB default scratch vertex & index buffers)
    vertex_buffer_size_ = 65536;
    index_buffer_size_ = 65536;
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

    // 5. Build Graphics Pipeline with Dynamic Rendering
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module_;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module_;
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
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    cb_attach.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cb_attach;

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering_ci{};
    rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_ci.colorAttachmentCount = 1;
    rendering_ci.pColorAttachmentFormats = &colorFormat;

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
    pipeline_ci.pColorBlendState = &cb;
    pipeline_ci.pDynamicState = &ds;
    pipeline_ci.layout = layout_;
    pipeline_ci.renderPass = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline_) != VK_SUCCESS) {
        shutdown();
        return false;
    }

    return true;
}

void VulkanShaderPipeline::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
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
    }
}

bool VulkanShaderPipeline::is_ready() const noexcept {
    return pipeline_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE;
}

DrawResolver VulkanShaderPipeline::get_resolver() {
    return [this](const GraphicsState& state, const DrawRequest& request, NativeDraw& native) -> bool {
        (void)state;
        if (!pipeline_ || !layout_) return false;

        native.pipeline = pipeline_;
        native.layout = layout_;
        native.vertex_buffers = { vertex_buffer_ };
        native.vertex_offsets = { 0 };

        if (request.indexed) {
            native.index_buffer = index_buffer_;
            native.index_offset = 0;
            native.index_bytes = index_buffer_size_;
        }
        return true;
    };
}

} // namespace gravity::hle::gnm
