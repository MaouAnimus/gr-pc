#include "hle/gnm/gcn_decoder.h"
#include "hle/gnm/gcn_translator.h"
#include "hle/gnm/spirv_builder.h"
#include "hle/gnm/spirv_validation.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>
#include <cmath>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using namespace gravity::hle::gnm;
using namespace gravity::hle::gnm::gcn;
using namespace gravity::hle::gnm::spirv;

namespace {

constexpr std::uint32_t endpgm = 0xbf810000u;

std::size_t opcode_count(const std::vector<std::uint32_t>& words, std::uint16_t opcode) {
    std::size_t count{};
    for (std::size_t i = 5; i < words.size(); i += words[i] >> 16) {
        CHECK((words[i] >> 16) && (words[i] >> 16) <= words.size() - i);
        count += (words[i] & 65535) == opcode;
    }
    return count;
}

void valid(const TranslateResult& result) {
    if (!result.success) std::fprintf(stderr, "%s\n", result.error.c_str());
    CHECK(result.success);
    std::string error;
    CHECK(validate_spirv(result.spirv, error));
}

// ========================= SPIR-V Builder Tests =========================

void builder_empty_module() {
    Builder b;
    b.add_capability(Capability::Shader);
    b.set_memory_model(AddressingModel::Logical, MemoryModel::GLSL450);

    auto void_t = b.type_void();
    auto func_t = b.type_function(void_t);
    auto main_fn = b.begin_function(void_t, func_t);
    b.add_entry_point(ExecutionModel::Vertex, main_fn, "main", {});
    b.begin_block();
    b.op_return();
    b.end_function();

    auto spirv = b.finalize();
    CHECK(spirv.size() >= 5);
    CHECK(spirv[0] == 0x07230203U); // Magic
    CHECK(spirv[1] == 0x00010500U); // Version 1.5
    CHECK(spirv[3] > 0);            // Bound
    CHECK(spirv[4] == 0);           // Schema
}

void builder_types() {
    Builder b;
    auto f32 = b.type_float(32);
    auto f32_2 = b.type_float(32);
    CHECK(f32 == f32_2); // Deduplication

    auto u32 = b.type_int(32, false);
    auto i32 = b.type_int(32, true);
    CHECK(u32 != i32);

    auto v4f = b.type_vector(f32, 4);
    auto v4f_2 = b.type_vector(f32, 4);
    CHECK(v4f == v4f_2);

    auto v3f = b.type_vector(f32, 3);
    CHECK(v3f != v4f);

    auto bool_t = b.type_bool();
    auto bool_t2 = b.type_bool();
    CHECK(bool_t == bool_t2);
}

void builder_constants() {
    Builder b;
    auto c1 = b.const_float32(1.0f);
    auto c1_2 = b.const_float32(1.0f);
    CHECK(c1 == c1_2);

    auto c2 = b.const_float32(2.0f);
    CHECK(c1 != c2);

    auto i0 = b.const_int32(0);
    auto i0_2 = b.const_int32(0);
    CHECK(i0 == i0_2);

    auto u42 = b.const_uint32(42);
    auto u42_2 = b.const_uint32(42);
    CHECK(u42 == u42_2);
}

void builder_instructions() {
    Builder b;
    b.add_capability(Capability::Shader);
    auto glsl = b.import_extended("GLSL.std.450");
    b.set_memory_model(AddressingModel::Logical, MemoryModel::GLSL450);

    auto f32 = b.type_float(32);
    auto v4f = b.type_vector(f32, 4);
    auto void_t = b.type_void();
    auto func_t = b.type_function(void_t);
    auto ptr_f32 = b.type_pointer(StorageClass::Function, f32);

    auto main_fn = b.begin_function(void_t, func_t);
    b.add_entry_point(ExecutionModel::Fragment, main_fn, "main", {});
    b.add_execution_mode(main_fn, ExecutionMode::OriginUpperLeft);
    b.begin_block();

    auto var = b.variable(ptr_f32, StorageClass::Function, b.const_float32(0.0f));
    auto a = b.const_float32(1.0f);
    auto b_val = b.const_float32(2.0f);
    auto sum = b.op_fadd(f32, a, b_val);
    b.op_store(var, sum);

    auto loaded = b.op_load(f32, var);
    auto product = b.op_fmul(f32, loaded, b.const_float32(3.0f));
    auto negated = b.op_fnegate(f32, product);
    (void)negated;

    auto sqrt_val = b.ext_inst(f32, glsl, GLSLstd450::Sqrt, {a});
    (void)sqrt_val;

    auto vec = b.op_composite_construct(v4f, {a, b_val, sum, product});
    auto x = b.op_composite_extract(f32, vec, 0);
    (void)x;

    b.op_return();
    b.end_function();

    auto spirv = b.finalize();
    CHECK(spirv.size() >= 20);
    CHECK(spirv[0] == 0x07230203U);
}

// ========================= Translator Tests =========================

void translate_trivial_vs() {
    // Minimal vertex shader: mov v0 to position export.
    // v_mov_b32 v0, 1.0
    // v_mov_b32 v1, 0.0
    // v_mov_b32 v2, 0.0
    // v_mov_b32 v3, 1.0
    // exp pos0, v0, v1, v2, v3 done
    // s_endpgm
    const std::array words{
        0x7e0002f2u,  // v_mov_b32 v0, 1.0 (inline constant 242 = 1.0)
        0x7e020280u,  // v_mov_b32 v1, 0 (zero)
        0x7e040280u,  // v_mov_b32 v2, 0
        0x7e0602f2u,  // v_mov_b32 v3, 1.0
        0xf80008cfu,  // exp pos0, v0,v1,v2,v3 done (target 12 = pos0)
        0x03020100u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_sgprs = 16;
    config.num_vgprs = 16;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
    CHECK(result.spirv[0] == 0x07230203U);
    CHECK(!result.exports.empty());
    CHECK(result.exports[0].target == 12); // pos0 is target 12 in GCN ISA
}

void translate_trivial_ps() {
    // Minimal fragment shader: output solid color from VGPRs.
    // v_mov_b32 v0, 1.0  (red)
    // v_mov_b32 v1, 0.0  (green)
    // v_mov_b32 v2, 0.0  (blue)
    // v_mov_b32 v3, 1.0  (alpha)
    // exp mrt0, v0,v1,v2,v3 done
    // s_endpgm
    const std::array words{
        0x7e0002f2u,  // v_mov_b32 v0, 1.0
        0x7e020280u,  // v_mov_b32 v1, 0
        0x7e040280u,  // v_mov_b32 v2, 0
        0x7e0602f2u,  // v_mov_b32 v3, 1.0
        0xf800080fu,  // exp mrt0, v0,v1,v2,v3 done (target 0 = mrt0)
        0x03020100u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Fragment;
    config.num_sgprs = 8;
    config.num_vgprs = 8;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
    CHECK(!result.exports.empty());
    // mrt0 target = 0 (MRT exports start at 0 in the target field)
    CHECK(result.exports[0].target == 0);
}

void translate_alu_ops() {
    // Shader with various ALU operations.
    const std::array words{
        0x7e0002f2u,             // v_mov_b32 v0, 1.0
        0x7e0202f4u,             // v_mov_b32 v1, 2.0
        0x06040100u,             // v_add_f32 v2, v0, v1  (VOP2 opcode 3)
        0x10060300u,             // v_mul_f32 v3, v0, v1  (VOP2 opcode 8)
        0xd2820004u, 0x040a0100u,// v_mad_f32 v4, v0, v1, v2  (VOP3 opcode 321)
        0xf80008cfu,             // exp pos0
        0x03020100u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_vgprs = 16;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
}

void translate_scalar_ops() {
    // Shader with scalar ALU.
    const std::array words{
        0xbeeb03ffu, 0x00000003u,  // s_mov_b32 vcc_hi, 3 (SOP1 literal)
        0xb000ffffu,               // s_movk_i32 s0, -1
        0x80018200u,               // s_add_u32 s1, s0, 2
        0xbf068001u,               // s_cmp_eq_u32 s1, 0 (SOPC)
        0xbf8c007fu,               // s_waitcnt
        0x7e0002f2u,               // v_mov_b32 v0, 1.0
        0x7e020280u,               // v_mov_b32 v1, 0
        0x7e040280u,               // v_mov_b32 v2, 0
        0x7e0602f2u,               // v_mov_b32 v3, 1.0
        0xf80008cfu, 0x03020100u,  // exp pos0
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_sgprs = 32;
    config.num_vgprs = 16;

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv[0] == 0x07230203U);
}

void translate_with_texture() {
    // Fragment shader that samples a texture.
    const std::array words{
        0x7e0002f2u,             // v_mov_b32 v0, 1.0 (u coord placeholder)
        0x7e0202f3u,             // v_mov_b32 v1, 0.5 (-0.5? no, 0x2f3 = -0.5) 
        0xf0800f00u, 0x00400200u,// image_sample, opcode32, resource s0, sampler s8
        0xf800080fu,             // exp mrt0, v2,v3,v4,v5 done
        0x05040302u,
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Fragment;
    config.num_sgprs = 16;
    config.num_vgprs = 16;
    config.num_textures = 1;
    config.num_samplers = 1;
    config.sampled_texture = SampledTextureBinding{0, 8, 0, 0};

    auto result = translate(program, config);
    CHECK(result.success);
    CHECK(result.spirv.size() >= 5);
    valid(result);
    CHECK(opcode_count(result.spirv, Op::ImageSampleImplicitLod) == 1);
}

void translate_spirv_valid() {
    // Verify SPIR-V structural validity of a generated module.
    const std::array words{
        0x7e0002f2u,             // v_mov_b32 v0, 1.0
        0x7e020280u,             // v_mov_b32 v1, 0
        0x7e040280u,             // v_mov_b32 v2, 0
        0x7e0602f2u,             // v_mov_b32 v3, 1.0
        0xf80008cfu, 0x03020100u,// exp pos0 done
        endpgm
    };

    auto program = decode(words);
    CHECK(program.diagnostic);

    TranslateConfig config;
    config.stage = Stage::Vertex;
    config.num_vgprs = 8;

    auto result = translate(program, config);
    CHECK(result.success);
    valid(result);

    // Validate SPIR-V header.
    CHECK(result.spirv[0] == 0x07230203U); // Magic
    CHECK((result.spirv[1] & 0xFFFF0000U) >= 0x00010000U); // Version >= 1.0
    CHECK(result.spirv[3] > 0); // Bound
    CHECK(result.spirv[4] == 0); // Schema

    // Walk through SPIR-V instructions and verify word counts.
    for (std::size_t i = 5; i < result.spirv.size();) {
        auto word = result.spirv[i];
        auto wc = word >> 16;
        CHECK(wc > 0);
        CHECK(wc <= result.spirv.size() - i);
        i += wc;
    }
    // Must consume all words.
    std::size_t pos = 5;
    while (pos < result.spirv.size()) pos += result.spirv[pos] >> 16;
    CHECK(pos == result.spirv.size());
}

void translate_invalid_program() {
    // Empty program should fail.
    Program empty;
    TranslateConfig config;
    auto result = translate(empty, config);
    CHECK(!result.success);
    CHECK(!result.error.empty());
}

// Execute numeric fragment output on Vulkan. This catches valid-but-wrong
// opcode mappings that SPIR-V validation alone cannot detect.
struct NumericGpu {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{};
    VkImage image{}; VkImageView view{}; VkDeviceMemory image_memory{};
    VkBuffer readback{}; VkDeviceMemory readback_memory{};
    VkPipelineLayout layout{}; VkShaderModule vertex{};
    std::uint32_t memory_type(std::uint32_t mask, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(physical, &props);
        for (std::uint32_t i=0; i<props.memoryTypeCount; ++i)
            if ((mask & (1u<<i)) && (props.memoryTypes[i].propertyFlags & flags)==flags) return i;
        CHECK(false); return 0;
    }
    VkShaderModule shader(const std::vector<std::uint32_t>& words) {
        std::string error; CHECK(validate_spirv(words,error));
        VkShaderModuleCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize=words.size()*4; info.pCode=words.data(); VkShaderModule module{};
        CHECK(vkCreateShaderModule(device,&info,nullptr,&module)==VK_SUCCESS); return module;
    }
    NumericGpu() {
        VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo=&app;
        if (vkCreateInstance(&info,nullptr,&instance)!=VK_SUCCESS) { std::puts("SKIP Vulkan 1.3"); std::exit(77); }
        std::uint32_t count{}; CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr)==VK_SUCCESS);
        std::vector<VkPhysicalDevice> devices(count); CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data())==VK_SUCCESS);
        std::uint32_t family{};
        for (const auto candidate:devices) {
            VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(candidate,&props);
            VkPhysicalDeviceVulkan13Features features{}; features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceFeatures2 queried{}; queried.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; queried.pNext=&features;
            vkGetPhysicalDeviceFeatures2(candidate,&queried);
            if (props.apiVersion<VK_API_VERSION_1_3 || !features.dynamicRendering) continue;
            std::uint32_t n{}; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
            std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
            for (std::uint32_t i=0;i<n;++i) if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT) { physical=candidate; family=i; break; }
            if(physical) break;
        }
        if(!physical) { std::puts("SKIP Vulkan dynamic rendering"); std::exit(77); }
        const float priority=1;
        VkDeviceQueueCreateInfo queue_info{}; queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex=family; queue_info.queueCount=1; queue_info.pQueuePriorities=&priority;
        VkPhysicalDeviceVulkan13Features features{}; features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES; features.dynamicRendering=VK_TRUE;
        VkDeviceCreateInfo create{}; create.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; create.pNext=&features;
        create.queueCreateInfoCount=1; create.pQueueCreateInfos=&queue_info;
        CHECK(vkCreateDevice(physical,&create,nullptr,&device)==VK_SUCCESS); vkGetDeviceQueue(device,family,0,&queue);
        VkImageCreateInfo image_info{}; image_info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; image_info.imageType=VK_IMAGE_TYPE_2D;
        image_info.format=VK_FORMAT_R32G32B32A32_SFLOAT; image_info.extent={1,1,1}; image_info.mipLevels=1; image_info.arrayLayers=1;
        image_info.samples=VK_SAMPLE_COUNT_1_BIT; image_info.tiling=VK_IMAGE_TILING_OPTIMAL;
        image_info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        CHECK(vkCreateImage(device,&image_info,nullptr,&image)==VK_SUCCESS);
        VkMemoryRequirements req; vkGetImageMemoryRequirements(device,image,&req);
        VkMemoryAllocateInfo allocation{}; allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocation.allocationSize=req.size;
        allocation.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(device,&allocation,nullptr,&image_memory)==VK_SUCCESS); CHECK(vkBindImageMemory(device,image,image_memory,0)==VK_SUCCESS);
        VkImageViewCreateInfo view_info{}; view_info.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; view_info.image=image;
        view_info.viewType=VK_IMAGE_VIEW_TYPE_2D; view_info.format=image_info.format; view_info.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        CHECK(vkCreateImageView(device,&view_info,nullptr,&view)==VK_SUCCESS);
        VkBufferCreateInfo buffer{}; buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; buffer.size=16; buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        CHECK(vkCreateBuffer(device,&buffer,nullptr,&readback)==VK_SUCCESS); vkGetBufferMemoryRequirements(device,readback,&req);
        allocation.allocationSize=req.size; allocation.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CHECK(vkAllocateMemory(device,&allocation,nullptr,&readback_memory)==VK_SUCCESS); CHECK(vkBindBufferMemory(device,readback,readback_memory,0)==VK_SUCCESS);
        VkCommandPoolCreateInfo pool_info{}; pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pool_info.queueFamilyIndex=family;
        pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; CHECK(vkCreateCommandPool(device,&pool_info,nullptr,&pool)==VK_SUCCESS);
        VkCommandBufferAllocateInfo commands{}; commands.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; commands.commandPool=pool;
        commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; commands.commandBufferCount=1; CHECK(vkAllocateCommandBuffers(device,&commands,&command)==VK_SUCCESS);
        VkFenceCreateInfo fence_info{}; fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; CHECK(vkCreateFence(device,&fence_info,nullptr,&fence)==VK_SUCCESS);
        VkPipelineLayoutCreateInfo layout_info{}; layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        CHECK(vkCreatePipelineLayout(device,&layout_info,nullptr,&layout)==VK_SUCCESS);
        Builder b; b.add_capability(Capability::Shader); b.set_memory_model(AddressingModel::Logical,MemoryModel::GLSL450);
        const auto u32=b.type_int(32,false), f32=b.type_float(32), v4=b.type_vector(f32,4);
        const auto index=b.variable(b.type_pointer(StorageClass::Input,u32),StorageClass::Input);
        const auto position=b.variable(b.type_pointer(StorageClass::Output,v4),StorageClass::Output);
        b.decorate(index,Decoration::BuiltIn,static_cast<std::uint32_t>(BuiltIn::VertexIndex));
        b.decorate(position,Decoration::BuiltIn,static_cast<std::uint32_t>(BuiltIn::Position));
        const auto main=b.begin_function(b.type_void(),b.type_function(b.type_void()));
        b.add_entry_point(ExecutionModel::Vertex,main,"main",{index,position}); b.begin_block();
        const auto id=b.op_load(u32,index), two=b.const_uint32(2);
        const auto x=b.op_convert_u_to_f(f32,b.op_bitwise_and(u32,b.op_shift_left(u32,id,b.const_uint32(1)),two));
        const auto y=b.op_convert_u_to_f(f32,b.op_bitwise_and(u32,id,two));
        b.op_store(position,b.op_composite_construct(v4,{
            b.op_fsub(f32,b.op_fmul(f32,x,b.const_float32(2)),b.const_float32(1)),
            b.op_fsub(f32,b.op_fmul(f32,y,b.const_float32(2)),b.const_float32(1)),b.const_float32(0),b.const_float32(1)}));
        b.op_return(); b.end_function(); vertex=shader(b.finalize());
    }
    std::array<float,4> render(const std::vector<std::uint32_t>& code) {
        const auto fragment=shader(code);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(auto& stage:stages) { stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName="main"; }
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertex; stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fragment;
        VkPipelineVertexInputStateCreateInfo input{}; input.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{}; assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,1,1,0,1}; VkRect2D scissor{{0,0},{1,1}};
        VkPipelineViewportStateCreateInfo viewport_info{}; viewport_info.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_info.viewportCount=1; viewport_info.pViewports=&viewport; viewport_info.scissorCount=1; viewport_info.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo raster{}; raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo multisample{}; multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{}; attachment.colorWriteMask=15;
        VkPipelineColorBlendStateCreateInfo blend{}; blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; blend.attachmentCount=1; blend.pAttachments=&attachment;
        const auto format=VK_FORMAT_R32G32B32A32_SFLOAT;
        VkPipelineRenderingCreateInfo rendering{}; rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO; rendering.colorAttachmentCount=1; rendering.pColorAttachmentFormats=&format;
        VkGraphicsPipelineCreateInfo pipeline_info{}; pipeline_info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; pipeline_info.pNext=&rendering;
        pipeline_info.stageCount=2; pipeline_info.pStages=stages; pipeline_info.pVertexInputState=&input; pipeline_info.pInputAssemblyState=&assembly;
        pipeline_info.pViewportState=&viewport_info; pipeline_info.pRasterizationState=&raster; pipeline_info.pMultisampleState=&multisample;
        pipeline_info.pColorBlendState=&blend; pipeline_info.layout=layout; VkPipeline pipeline{};
        CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&pipeline)==VK_SUCCESS);
        CHECK(vkResetCommandBuffer(command,0)==VK_SUCCESS);
        VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; CHECK(vkBeginCommandBuffer(command,&begin)==VK_SUCCESS);
        VkImageMemoryBarrier barrier{}; barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; barrier.image=image;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkRenderingAttachmentInfo color{}; color.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO; color.imageView=view;
        color.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; color.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo scope{}; scope.sType=VK_STRUCTURE_TYPE_RENDERING_INFO; scope.renderArea={{0,0},{1,1}}; scope.layerCount=1;
        scope.colorAttachmentCount=1; scope.pColorAttachments=&color;
        vkCmdBeginRendering(command,&scope); vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline); vkCmdDraw(command,3,1,0,0); vkCmdEndRendering(command);
        barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={1,1,1};
        vkCmdCopyImageToBuffer(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&copy);
        VkBufferMemoryBarrier host{}; host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; host.buffer=readback; host.size=VK_WHOLE_SIZE;
        host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT; host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        CHECK(vkEndCommandBuffer(command)==VK_SUCCESS); CHECK(vkResetFences(device,1,&fence)==VK_SUCCESS);
        VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount=1; submit.pCommandBuffers=&command;
        CHECK(vkQueueSubmit(queue,1,&submit,fence)==VK_SUCCESS); CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,5'000'000'000)==VK_SUCCESS);
        void* mapped{}; CHECK(vkMapMemory(device,readback_memory,0,16,0,&mapped)==VK_SUCCESS);
        std::array<float,4> value; std::memcpy(value.data(),mapped,16); vkUnmapMemory(device,readback_memory);
        vkDestroyPipeline(device,pipeline,nullptr); vkDestroyShaderModule(device,fragment,nullptr); return value;
    }
    ~NumericGpu() {
        if(device) {
            vkDeviceWaitIdle(device); vkDestroyShaderModule(device,vertex,nullptr); vkDestroyPipelineLayout(device,layout,nullptr);
            vkDestroyFence(device,fence,nullptr); vkDestroyCommandPool(device,pool,nullptr);
            vkDestroyBuffer(device,readback,nullptr); vkFreeMemory(device,readback_memory,nullptr);
            vkDestroyImageView(device,view,nullptr); vkDestroyImage(device,image,nullptr); vkFreeMemory(device,image_memory,nullptr); vkDestroyDevice(device,nullptr);
        }
        if(instance) vkDestroyInstance(instance,nullptr);
    }
};

std::uint32_t mov(unsigned destination, unsigned source) { return 0x7e000200u | (destination<<17) | source; }
void literal(std::vector<std::uint32_t>& words, unsigned destination, float value) {
    std::uint32_t bits; std::memcpy(&bits,&value,4); words.push_back(mov(destination,255)); words.push_back(bits);
}
void export_color(std::vector<std::uint32_t>& words, std::array<unsigned,4> registers) {
    words.push_back(0xf800080fu); words.push_back(registers[0]|(registers[1]<<8)|(registers[2]<<16)|(registers[3]<<24)); words.push_back(endpgm);
}
TranslateResult fragment(const std::vector<std::uint32_t>& words) {
    TranslateConfig config; config.stage=Stage::Fragment; const auto result=translate(decode(words),config); valid(result); return result;
}
void translate_numeric() {
    NumericGpu gpu;
    const auto check=[&](const std::vector<std::uint32_t>& words, std::array<float,4> expected) {
        const auto actual=gpu.render(fragment(words).spirv);
        for(unsigned i=0;i<4;++i) { if(std::abs(actual[i]-expected[i])>0.00001f) std::fprintf(stderr,"channel%u actual=%f expected=%f\n",i,actual[i],expected[i]); CHECK(std::abs(actual[i]-expected[i])<0.00001f); }
    };
    std::vector<std::uint32_t> words; literal(words,0,2.75f);
    words.push_back(0x7e000000u|(1u<<17)|(33u<<9)|256u); // TRUNC, not RCP
    words.push_back(0x7e000000u|(2u<<17)|(42u<<9)|256u); // RCP, not LOG
    words.push_back(0x7e000000u|(3u<<17)|(51u<<9)|256u); // SQRT, not FRACT
    export_color(words,{0,1,2,3}); check(words,{2.75f,2,1/2.75f,std::sqrt(2.75f)});
    words.clear(); literal(words,0,1.25f); literal(words,1,3.5f);
    words.push_back((15u<<25)|(2u<<17)|(1u<<9)|256u); // MIN
    words.push_back((16u<<25)|(3u<<17)|(1u<<9)|256u); // MAX
    export_color(words,{0,1,2,3}); check(words,{1.25f,3.5f,1.25f,3.5f});
    words.clear(); literal(words,0,-0.75f); literal(words,1,0.5f);
    words.push_back(0xd0000000u|(259u<<17)|4u|0x100u|0x800u); // ADD dst=v4, abs0, clamp
    words.push_back(256u|(257u<<9)|(1u<<27)|(1u<<30)); // mul2, neg1
    export_color(words,{4,4,4,4}); check(words,{0.5f,0.5f,0.5f,0.5f});
    words={0xb0000007u,0xb0010003u,0x87020100u,0x88030100u,0x8f048101u,
        0x7e000c02u,0x7e020c03u,0x7e040c04u,mov(3,242)};
    export_color(words,{0,1,2,3}); check(words,{3,7,6,1});
}

void translate_strict() {
    const auto reject=[](const std::vector<std::uint32_t>& words, std::size_t offset) {
        const auto program=decode(words); CHECK(program.diagnostic); TranslateConfig config; config.stage=Stage::Fragment;
        const auto result=translate(program,config); CHECK(!result.success && result.spirv.empty() && !result.error.empty());
        CHECK(result.error_word_offset==offset);
    };
    reject({mov(0,242),0xbf820001u,mov(0,128),0xf800080fu,0u,endpgm},1); // valid branch target, unsupported control flow
    reject({0xbe802100u,mov(0,242),0xf800080fu,0u,endpgm},0); // fetch call
    reject({0xc0000000u,mov(0,242),0xf800080fu,0u,endpgm},0); // memory cannot become zero
    reject({0xd8340000u,0u,mov(0,242),0xf800080fu,0u,endpgm},0); // LDS cannot disappear
    reject({mov(0,126),0xf800080fu,0u,endpgm},0); // EXEC cannot become all-ones
    reject({mov(0,0),0xf800080fu,0u,endpgm},0); // unbound user SGPR
    reject({mov(0,265),0xf800080fu,0u,endpgm},0); // undefined VGPR
    reject({mov(0,242),0x7e000000u|(1u<<17)|(7u<<9)|256u,0xf800080fu,0x01010101u,endpgm},1); // saturating float-to-uint conversion needs explicit lowering
    reject({mov(0,242),0xf8001c0fu,0u,endpgm},1); // compressed EXP
    reject({mov(0,242),0xf8000801u,0u,endpgm},1); // partial EXP
    reject({mov(0,242),0xf800081fu,0u,endpgm},1); // MRT1 cannot overwrite MRT0
    auto program=decode(std::array{mov(0,242),0xf800080fu,0u,endpgm});
    program.instructions[0].opcode=33;
    CHECK(!translate(program,TranslateConfig{}).success); // inconsistent decoded metadata
    const auto sample=decode(std::array{mov(0,242),mov(1,240),0xf0800f00u,0x00400200u,0xf800080fu,0x05040302u,endpgm});
    TranslateConfig config; config.stage=Stage::Fragment; config.num_textures=config.num_samplers=1;
    CHECK(!translate(sample,config).success); // absent resource contract
    config.sampled_texture=SampledTextureBinding{4,8,0,0};
    CHECK(!translate(sample,config).success); // wrong resource base
    config.sampled_texture=SampledTextureBinding{0,16,0,0};
    CHECK(!translate(sample,config).success); // sampler lies outside the allocated SGPR file
    config.sampled_texture=SampledTextureBinding{0,9,0,0};
    CHECK(!translate(sample,config).success); // sampler SGPR tuple is not aligned
}

void cache_config_identity() {
    TranslateConfig original; const auto identity=cache_identity(original);
    auto config=original; config.num_vgprs++; CHECK(cache_identity(config)!=identity);
    config=original; config.vertex_vgpr_bases={1}; CHECK(cache_identity(config)!=identity);
    config=original; config.user_sgpr_values={1}; CHECK(cache_identity(config)!=identity);
    config=original; config.sampled_texture=SampledTextureBinding{0,8,0,0}; CHECK(cache_identity(config)!=identity);
    const auto bound=cache_identity(config); config.sampled_texture->sampler_sgpr_base=12; CHECK(cache_identity(config)!=bound);
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];

    if (suite == "builder_empty")       builder_empty_module();
    else if (suite == "builder_types")  builder_types();
    else if (suite == "builder_const")  builder_constants();
    else if (suite == "builder_instr")  builder_instructions();
    else if (suite == "translate_vs")   translate_trivial_vs();
    else if (suite == "translate_ps")   translate_trivial_ps();
    else if (suite == "translate_alu")  translate_alu_ops();
    else if (suite == "translate_scalar") translate_scalar_ops();
    else if (suite == "translate_tex")  translate_with_texture();
    else if (suite == "spirv_valid")    translate_spirv_valid();
    else if (suite == "translate_invalid") translate_invalid_program();
    else if (suite == "translate_numeric") translate_numeric();
    else if (suite == "translate_strict") translate_strict();
    else if (suite == "cache_identity") cache_config_identity();
    else return 2;

    std::puts("PASS GCN translator");
}
