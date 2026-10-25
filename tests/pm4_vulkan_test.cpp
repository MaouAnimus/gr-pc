#include "hle/gnm.h"
#include "hle/gnm/shader_pipeline.h"
#include "hle/avplayer/avplayer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <functional>
#include <chrono>
#include <thread>
#include <windows.h>
using namespace gravity::hle::gnm;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"Vulkan line %d: %s\n",__LINE__,#x); std::exit(1); } } while(false)
namespace {
struct VulkanTest {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue gpu{};
    VkCommandPool pool{}; VkCommandBuffer cmd{}; VkFence fence{};
    VkImage image{}; VkDeviceMemory image_memory{}; VkImageView view{};
    VkBuffer pixels{},indices{}; VkDeviceMemory pixel_memory{},index_memory{};
    VkPipelineLayout layout{}; VkPipeline pipeline{};
    VkShaderModule vertex{},fragment{};
    VkDebugUtilsMessengerEXT messenger{};
    std::atomic<unsigned> validation_errors{0};
    std::uint32_t family{};
    static constexpr VkExtent2D extent{64,64};
    std::uint32_t memory_type(std::uint32_t bits,VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(physical,&props);
        for(std::uint32_t i=0;i<props.memoryTypeCount;++i)
            if((bits&(1U<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags) return i;
        CHECK(false); return 0;
    }
    void buffer(VkDeviceSize size,VkBufferUsageFlags usage,VkBuffer& out,VkDeviceMemory& memory) {
        VkBufferCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size=size; info.usage=usage;
        CHECK(vkCreateBuffer(device,&info,nullptr,&out)==VK_SUCCESS);
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,out,&req);
        VkMemoryAllocateInfo allocation{}; allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize=req.size;
        allocation.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CHECK(vkAllocateMemory(device,&allocation,nullptr,&memory)==VK_SUCCESS);
        CHECK(vkBindBufferMemory(device,out,memory,0)==VK_SUCCESS);
    }
    VkShaderModule shader(const char* file) {
        std::ifstream stream(file,std::ios::binary|std::ios::ate); CHECK(stream.is_open());
        const auto size=stream.tellg(); CHECK(size>0 && size%4==0);
        std::vector<std::uint32_t> bytes(static_cast<std::size_t>(size)/4);
        stream.seekg(0); CHECK(stream.read(reinterpret_cast<char*>(bytes.data()),size));
        VkShaderModuleCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize=static_cast<std::size_t>(size); info.pCode=bytes.data();
        VkShaderModule module{}; CHECK(vkCreateShaderModule(device,&info,nullptr,&module)==VK_SUCCESS); return module;
    }
    bool initialize() {
        VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_3;
        VkInstanceCreateInfo create{}; create.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;create.pApplicationInfo=&app;
        std::uint32_t layer_count=0;
        CHECK(vkEnumerateInstanceLayerProperties(&layer_count,nullptr)==VK_SUCCESS);
        std::vector<VkLayerProperties> layers(layer_count);
        CHECK(vkEnumerateInstanceLayerProperties(&layer_count,layers.data())==VK_SUCCESS);
        const char* validation="VK_LAYER_KHRONOS_validation";
        const bool has_validation=std::any_of(layers.begin(),layers.end(),[&](const auto& l){return std::strcmp(l.layerName,validation)==0;});
        std::uint32_t extension_count=0;
        CHECK(vkEnumerateInstanceExtensionProperties(nullptr,&extension_count,nullptr)==VK_SUCCESS);
        std::vector<VkExtensionProperties> extensions(extension_count);
        CHECK(vkEnumerateInstanceExtensionProperties(nullptr,&extension_count,extensions.data())==VK_SUCCESS);
        const bool debug=has_validation && std::any_of(extensions.begin(),extensions.end(),[](const auto& e){return std::strcmp(e.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME)==0;});
        const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkDebugUtilsMessengerCreateInfoEXT messages{};messages.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messages.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messages.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messages.pUserData=this;
        messages.pfnUserCallback=[](VkDebugUtilsMessageSeverityFlagBitsEXT,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void* user)->VkBool32 {
            ++static_cast<VulkanTest*>(user)->validation_errors;
            std::fprintf(stderr,"Vulkan validation: %s\n",data->pMessage);return VK_FALSE;
        };
        if(has_validation) {create.enabledLayerCount=1;create.ppEnabledLayerNames=&validation;}
        if(debug) {create.enabledExtensionCount=1;create.ppEnabledExtensionNames=&extension;create.pNext=&messages;}
        if(vkCreateInstance(&create,nullptr,&instance)!=VK_SUCCESS) return false;
        if(debug) {
            const auto make=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
            CHECK(make && make(instance,&messages,nullptr,&messenger)==VK_SUCCESS);
        }
        std::printf("Khronos validation: %s\n",debug?"enabled":"unavailable; pixel readback still required");
        std::uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr)==VK_SUCCESS);
        std::vector<VkPhysicalDevice> devices(count);
        if(!count) return false;
        CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data())==VK_SUCCESS);
        bool found=false;
        for(const auto candidate:devices) {
            VkPhysicalDeviceVulkan13Features features13{};features13.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceFeatures2 features{};features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;features.pNext=&features13;
            vkGetPhysicalDeviceFeatures2(candidate,&features);
            VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(candidate,&properties);
            if(properties.apiVersion<VK_API_VERSION_1_3 || !features13.dynamicRendering) continue;
            std::uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            for(std::uint32_t i=0;i<families;++i) if(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT) {
                physical=candidate; family=i; found=true;
                std::printf("PM4 Vulkan device: %s\n",properties.deviceName); break;
            }
            if(found) break;
        }
        if(!found) return false;
        float priority=1;
        VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
        VkPhysicalDeviceVulkan13Features features{};features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;features.dynamicRendering=VK_TRUE;
        VkDeviceCreateInfo device_info{};device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.pNext=&features;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue;
        CHECK(vkCreateDevice(physical,&device_info,nullptr,&device)==VK_SUCCESS);
        vkGetDeviceQueue(device,family,0,&gpu);
        VkCommandPoolCreateInfo pool_info{};pool_info.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pool_info.queueFamilyIndex=family;
        CHECK(vkCreateCommandPool(device,&pool_info,nullptr,&pool)==VK_SUCCESS);
        VkCommandBufferAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool=pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
        CHECK(vkAllocateCommandBuffers(device,&alloc,&cmd)==VK_SUCCESS);
        VkFenceCreateInfo fence_info{};fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        CHECK(vkCreateFence(device,&fence_info,nullptr,&fence)==VK_SUCCESS);
        VkImageCreateInfo image_info{};image_info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType=VK_IMAGE_TYPE_2D;image_info.format=VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent={64,64,1};image_info.mipLevels=1;image_info.arrayLayers=1;image_info.samples=VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling=VK_IMAGE_TILING_OPTIMAL;image_info.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        CHECK(vkCreateImage(device,&image_info,nullptr,&image)==VK_SUCCESS);
        VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image,&req);
        VkMemoryAllocateInfo memory{};memory.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory.allocationSize=req.size;memory.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(device,&memory,nullptr,&image_memory)==VK_SUCCESS);
        CHECK(vkBindImageMemory(device,image,image_memory,0)==VK_SUCCESS);
        VkImageViewCreateInfo view_info{};view_info.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image=image;view_info.viewType=VK_IMAGE_VIEW_TYPE_2D;view_info.format=image_info.format;
        view_info.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        CHECK(vkCreateImageView(device,&view_info,nullptr,&view)==VK_SUCCESS);
        buffer(64*64*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,pixels,pixel_memory);
        buffer(64,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,indices,index_memory);
        void* mapped=nullptr;CHECK(vkMapMemory(device,index_memory,0,64,0,&mapped)==VK_SUCCESS);
        const std::uint16_t short_indices[]={99,0,1,2}; const std::uint32_t long_indices[]={0,1,2};
        std::memcpy(mapped,short_indices,sizeof(short_indices));std::memcpy(static_cast<char*>(mapped)+16,long_indices,sizeof(long_indices));
        vkUnmapMemory(device,index_memory);
        vertex=shader("pm4_triangle.vert.spv");fragment=shader("pm4_triangle.frag.spv");
        VkPipelineLayoutCreateInfo layout_info{};layout_info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        CHECK(vkCreatePipelineLayout(device,&layout_info,nullptr,&layout)==VK_SUCCESS);
        VkPipelineShaderStageCreateInfo stages[2]{};
        for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkPipelineVertexInputStateCreateInfo input{};input.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=1;viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo samples{};samples.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=0xF;
        VkPipelineColorBlendStateCreateInfo blend{};blend.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount=1;blend.pAttachments=&attachment;
        const VkDynamicState dynamics[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamics;
        VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
        VkPipelineRenderingCreateInfo rendering{};rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&format;
        VkGraphicsPipelineCreateInfo pipeline_info{};pipeline_info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.pNext=&rendering;pipeline_info.stageCount=2;pipeline_info.pStages=stages;
        pipeline_info.pVertexInputState=&input;pipeline_info.pInputAssemblyState=&assembly;pipeline_info.pViewportState=&viewport;
        pipeline_info.pRasterizationState=&raster;pipeline_info.pMultisampleState=&samples;pipeline_info.pColorBlendState=&blend;
        pipeline_info.pDynamicState=&dynamic;pipeline_info.layout=layout;
        CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&pipeline)==VK_SUCCESS);
        return true;
    }
    NativeDraw binding(const DrawRequest& request) {
        NativeDraw result;result.pipeline=pipeline;
        if(request.indexed) {
            result.index_buffer=indices;
            result.index_offset=request.index_address==0x2000?16:request.index_address==0x3000?0:2;
            result.index_bytes=request.index_address==0x2000?12:request.index_address==0x3000?8:6;
        }
        return result;
    }
    void begin(bool rendering=true, const std::function<void()>& before_render={}) {
        CHECK(vkResetCommandBuffer(cmd,0)==VK_SUCCESS);
        VkCommandBufferBeginInfo start{};start.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        CHECK(vkBeginCommandBuffer(cmd,&start)==VK_SUCCESS);
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.image=image;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(before_render) before_render();
        if(rendering) {
            VkRenderingAttachmentInfo attachment{};attachment.sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            attachment.imageView=view;attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo info{};info.sType=VK_STRUCTURE_TYPE_RENDERING_INFO;info.renderArea={{0,0},extent};
            info.layerCount=1;info.colorAttachmentCount=1;info.pColorAttachments=&attachment;
            vkCmdBeginRendering(cmd,&info);
        }
    }
    void finish(bool rendering=true,const std::function<void()>& after_submit={}) {
        if(rendering) vkCmdEndRendering(cmd);
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.image=image;barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={64,64,1};
        vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,pixels,1,&region);
        VkBufferMemoryBarrier host{};host.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        host.buffer=pixels;host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        CHECK(vkEndCommandBuffer(cmd)==VK_SUCCESS);
        CHECK(vkResetFences(device,1,&fence)==VK_SUCCESS);
        VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
        CHECK(vkQueueSubmit(gpu,1,&submit,fence)==VK_SUCCESS);
        if(after_submit) after_submit();
        CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ULL)==VK_SUCCESS);
    }
    void check_pixel(bool green) {
        void* mapped=nullptr;CHECK(vkMapMemory(device,pixel_memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS);
        const auto* center=static_cast<const std::uint8_t*>(mapped)+(32*64+32)*4;
        if(green) { CHECK(center[0]>=49 && center[0]<=53);CHECK(center[1]>=202 && center[1]<=206);CHECK(center[2]>=24 && center[2]<=28); }
        else CHECK(center[0]==0 && center[1]==0 && center[2]==0);
        vkUnmapMemory(device,pixel_memory);
    }
    ~VulkanTest() {
        if(device) {
            vkDeviceWaitIdle(device);
            if(pipeline) vkDestroyPipeline(device,pipeline,nullptr);
            if(layout) vkDestroyPipelineLayout(device,layout,nullptr);
            if(vertex) vkDestroyShaderModule(device,vertex,nullptr);if(fragment) vkDestroyShaderModule(device,fragment,nullptr);
            if(view) vkDestroyImageView(device,view,nullptr);if(image) vkDestroyImage(device,image,nullptr);
            if(image_memory) vkFreeMemory(device,image_memory,nullptr);
            if(pixels) vkDestroyBuffer(device,pixels,nullptr);if(indices) vkDestroyBuffer(device,indices,nullptr);
            if(pixel_memory) vkFreeMemory(device,pixel_memory,nullptr);if(index_memory) vkFreeMemory(device,index_memory,nullptr);
            if(fence) vkDestroyFence(device,fence,nullptr);if(pool) vkDestroyCommandPool(device,pool,nullptr);
            vkDestroyDevice(device,nullptr);
        }
        if(messenger) {
            const auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));
            CHECK(destroy);destroy(instance,messenger,nullptr);
        }
        if(instance) vkDestroyInstance(instance,nullptr);
        CHECK(validation_errors==0);
    }
};
using Words=std::vector<std::uint32_t>;
void add(Words& words,std::uint8_t opcode,std::initializer_list<std::uint32_t> data) {
    words.push_back(pm4::type3(opcode,static_cast<std::uint32_t>(data.size())));words.insert(words.end(),data);
}
pm4::Diagnostic enqueue(CommandQueue& q,const Words& words) {
    const std::span<const std::uint32_t> span=words;return q.submit({&span,1});
}

void geometry_upload_regression(VulkanTest& vk) {
    auto& pipeline=VulkanShaderPipeline::instance();
    CHECK(pipeline.initialize(vk.device,vk.physical,VK_FORMAT_R8G8B8A8_UNORM,vk.gpu,vk.family));
    const auto resolver=pipeline.get_resolver();
    struct Vertex { float x,y,z,u,v,r,g,b,a; };
    const Vertex hidden{3,3,0,0,0,0,0,1,1};
    std::array<Vertex,8> left, right;
    left.fill(hidden);right.fill(hidden);
    const auto rectangle=[](Vertex* dst,float x0,float x1,float r,float g) {
        dst[0]={x0,-1,0,0,0,r,g,0,1};dst[1]={x1,-1,0,1,0,r,g,0,1};
        dst[2]={x0,1,0,0,1,r,g,0,1};dst[3]={x1,1,0,1,1,r,g,0,1};
    };
    rectangle(left.data(),-1,0,1,0);rectangle(right.data()+4,0,1,0,1);
    std::uint16_t left_indices[]{0,1,2,3},right_indices[]{4,5,6,7};
    // An odd-width texture exercises rowPitch as well as its shader-read layout.
    alignas(256) std::array<std::uint8_t,24> texture{};
    for(size_t i=0;i<texture.size();i+=4) {
        texture[i]=64;texture[i+1]=128;texture[i+2]=192;texture[i+3]=255;
    }
    const auto texture_address=reinterpret_cast<std::uintptr_t>(texture.data());
    CHECK(pipeline.get_or_create_texture(texture_address,3,2,0x1a)!=VK_NULL_HANDLE);
    Words words;
    add(words,0x76,{0x0c,static_cast<uint32_t>(texture_address>>8),
        static_cast<uint32_t>(texture_address>>40)|(0x1aU<<20),2U|(1U<<14),0});
    auto draw=[&](const auto& vertices,const auto& indices) {
        const auto vertex_address=reinterpret_cast<std::uintptr_t>(vertices.data());
        const auto index_address=reinterpret_cast<std::uintptr_t>(indices);
        add(words,0x76,{0x4c,static_cast<uint32_t>(vertex_address),
            static_cast<uint32_t>(vertex_address>>32)|(uint32_t(sizeof(Vertex))<<16),8,0});
        add(words,0x2a,{0});
        add(words,0x27,{8,static_cast<uint32_t>(index_address),static_cast<uint32_t>(index_address>>32),4,0});
    };
    draw(left,left_indices);draw(right,right_indices);
    CommandQueue queue;
    std::vector<std::weak_ptr<void>> owners;
    std::vector<VkBuffer> vertex_buffers,index_buffers;
    CHECK(queue.set_resolver([&](const auto& state,const auto& request,auto& native) {
        if(!resolver(state,request,native)) return false;
        owners.push_back(native.resources);vertex_buffers.push_back(native.vertex_buffers.front());
        index_buffers.push_back(native.index_buffer);return true;
    }));
    CHECK(enqueue(queue,words));
    RecordingTicket ticket;
    vk.begin();CHECK(queue.record(vk.cmd,vk.extent,ticket));
    CHECK(owners.size()==2 && !owners[0].expired() && !owners[1].expired());
    CHECK(vertex_buffers[0]!=vertex_buffers[1] && index_buffers[0]!=index_buffers[1]);
    std::shared_ptr<void> fence_resources;
    vk.finish(true,[&] {
        fence_resources=std::move(ticket.resources);
        CHECK(queue.acknowledge(ticket));
        CHECK(!owners[0].expired() && !owners[1].expired());
    });
    CHECK(!owners[0].expired() && !owners[1].expired());
    fence_resources.reset();CHECK(owners[0].expired() && owners[1].expired());
    void* mapped{};CHECK(vkMapMemory(vk.device,vk.pixel_memory,0,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS);
    const auto* pixels=static_cast<const uint8_t*>(mapped);
    const auto* red=pixels+(48*64+16)*4;const auto* green=pixels+(48*64+48)*4;
    CHECK(std::abs(int(red[0])-64)<=2 && red[1]<=2 && red[2]<=2);
    CHECK(green[0]<=2 && std::abs(int(green[1])-128)<=2 && green[2]<=2);
    vkUnmapMemory(vk.device,vk.pixel_memory);

    // Partial-readable vertex memory must fail before reading coords[2..7].
    SYSTEM_INFO system{};GetSystemInfo(&system);
    const auto page=static_cast<size_t>(system.dwPageSize);
    auto* guard=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(guard);DWORD old{};CHECK(VirtualProtect(guard+page,page,PAGE_NOACCESS,&old));
    const float coords[]{-1,-1};std::memcpy(guard+page-sizeof(coords),coords,sizeof(coords));
    GraphicsState bad;
    const auto guard_address=reinterpret_cast<std::uintptr_t>(guard+page-sizeof(coords));
    bad.registers[0x2c4c]=static_cast<uint32_t>(guard_address);
    bad.registers[0x2c4d]=static_cast<uint32_t>(guard_address>>32)|(8U<<16);
    bad.registers[0x2c4e]=4;
    DrawRequest request;request.count=4;request.instances=1;
    NativeDraw rejected;CHECK(!resolver(bad,request,rejected));CHECK(!rejected.resources);
    CHECK(VirtualFree(guard,0,MEM_RELEASE));

    // Validate all selected indices and widen first_index+count before adding.
    const auto left_address=reinterpret_cast<std::uintptr_t>(left.data());
    bad.registers[0x2c4c]=static_cast<uint32_t>(left_address);
    bad.registers[0x2c4d]=static_cast<uint32_t>(left_address>>32)|(sizeof(Vertex)<<16);
    bad.registers[0x2c4e]=8;
    request.indexed=true;request.index_address=reinterpret_cast<std::uintptr_t>(left_indices);
    request.first_index=UINT32_MAX;
    CHECK(!resolver(bad,request,rejected));
    request.first_index=0;left_indices[3]=8;
    CHECK(!resolver(bad,request,rejected));left_indices[3]=3;

    // Aborted recordings and failed preflight cannot leave permanent owners.
    owners.clear();CHECK(enqueue(queue,words));vk.begin();CHECK(queue.record(vk.cmd,vk.extent,ticket));
    queue.abort(ticket);ticket.resources.reset();
    CHECK(owners.size()==2 && owners[0].expired() && owners[1].expired());
    queue.reset();owners.clear();unsigned calls{};
    CHECK(queue.set_resolver([&](const auto& state,const auto& draw_request,auto& native) {
        if(!resolver(state,draw_request,native)) return false;
        owners.push_back(native.resources);return ++calls<2;
    }));
    CHECK(enqueue(queue,words));vk.begin();
    CHECK(queue.record(vk.cmd,vk.extent,ticket).error==pm4::Error::resources && !ticket.id);
    CHECK(owners.size()==2 && owners[0].expired() && owners[1].expired());
    vk.finish();vk.check_pixel(false);queue.reset();
    pipeline.shutdown();
    std::puts("Per-draw vertex/index snapshots, fence ownership, guard-page rejection, static texture readback and failed-preflight cleanup passed");
}
}
void vulkan_test() {
    VulkanTest vk;
    if(!vk.initialize()) {std::puts("SKIP: Vulkan 1.3 dynamic rendering unavailable");std::exit(77);}
    CommandQueue queue;
    CHECK(queue.set_resolver([&](const GraphicsState& state,const DrawRequest& request,NativeDraw& binding) {
        CHECK(state.registers[0xA000]==123);
        binding=vk.binding(request);return true;
    }));
    Words words;add(words,0x69,{0,123});add(words,0x2F,{1});add(words,0x2D,{3,2});
    add(words,0x2A,{0});add(words,0x27,{3,0x1000,0,3,0});
    add(words,0x26,{0x3000,0});add(words,0x35,{4,1,3,0});
    add(words,0x2A,{1});add(words,0x27,{3,0x2000,0,3,0});
    CHECK(enqueue(queue,words));words.assign(1,0x80000000);CHECK(enqueue(queue,words));
    RecordingTicket first;
    vk.begin();CHECK(queue.record(vk.cmd,vk.extent,first));CHECK(first.id);
    CHECK(queue.stats().pending==2 && queue.stats().submitted==0);
    CommandQueue other;
    auto wrong_queue=first;wrong_queue.owner=&other;
    CHECK(!queue.acknowledge(wrong_queue));
    CHECK(queue.set_resolver({}).error==pm4::Error::busy);
    CHECK(enqueue(queue,words)); // producer can add a batch while a ticket is in flight
    queue.abort(first);CHECK(!queue.acknowledge(first));CHECK(queue.stats().pending==3);
    vk.begin();RecordingTicket second;CHECK(queue.record(vk.cmd,vk.extent,second));CHECK(second.id!=first.id);
    vk.finish();CHECK(queue.acknowledge(second));CHECK(!queue.acknowledge(second));
    CHECK(queue.stats().pending==0 && queue.stats().submitted==3 && queue.stats().submitted_draws==4);
    vk.check_pixel(true);
    // Each draw form must produce pixels on its own, without an earlier draw
    // masking a broken index path.
    for(int form=0;form<4;++form) {
        Words draw;
        if(form==0) add(draw,0x2D,{3,2});
        if(form==1) {add(draw,0x2A,{0});add(draw,0x27,{3,0x1000,0,3,0});}
        if(form==2) {add(draw,0x2A,{0});add(draw,0x26,{0x3000,0});add(draw,0x35,{4,1,3,0});}
        if(form==3) {add(draw,0x2A,{1});add(draw,0x27,{3,0x2000,0,3,0});}
        CHECK(enqueue(queue,draw));vk.begin();CHECK(queue.record(vk.cmd,vk.extent,second));
        vk.finish();CHECK(queue.acknowledge(second));vk.check_pixel(true);
    }
    // Register state survives acknowledged submissions.
    words.clear();add(words,0x2D,{3,2});CHECK(enqueue(queue,words));
    vk.begin();CHECK(queue.record(vk.cmd,vk.extent,second));vk.finish();CHECK(queue.acknowledge(second));vk.check_pixel(true);

    queue.reset();
    CHECK(queue.set_resolver([&](const GraphicsState& state,const DrawRequest& request,NativeDraw& binding) {
        if(state.registers[0xA000]==456) return false;
        binding=vk.binding(request);return true;
    }));
    words.clear();add(words,0x69,{0,123});add(words,0x2D,{3,2});
    add(words,0x69,{0,456});add(words,0x2D,{3,2});CHECK(enqueue(queue,words));
    vk.begin();CHECK(queue.record(vk.cmd,vk.extent,second).error==pm4::Error::resources);CHECK(!second.id);
    vk.finish();vk.check_pixel(false); // first draw was NOT emitted before later failure
    CHECK(queue.stats().pending==1 && queue.stats().submitted==0);
    CHECK(queue.set_resolver([](const auto&,const auto&,auto&)->bool{throw 1;}));
    vk.begin();CHECK(queue.record(vk.cmd,vk.extent,second).error==pm4::Error::resources);
    CHECK(!second.id && queue.stats().pending==1);
    vk.finish();vk.check_pixel(false);
    CHECK(queue.set_resolver([&](const auto&,const DrawRequest& request,NativeDraw& binding){binding=vk.binding(request);return true;}));
    vk.begin();CHECK(queue.record(vk.cmd,vk.extent,second));vk.finish();CHECK(queue.acknowledge(second));vk.check_pixel(true);
    // Reset invalidates stale recording tickets.
    words={0x80000000};CHECK(enqueue(queue,words));vk.begin();CHECK(queue.record(vk.cmd,vk.extent,first));
    queue.reset();CHECK(!queue.acknowledge(first));

    // Full ABI submit -> global GNM queue -> shared frame recorder -> real Vulkan.
    CHECK(sceGnmInitialize());setDevice(vk.device);
    CHECK(command_queue().set_resolver([&](const auto&,const DrawRequest& request,NativeDraw& binding){binding=vk.binding(request);return true;}));
    words.clear();add(words,0x2D,{3,2});const std::uint32_t* address=words.data();
    const auto size=static_cast<std::uint32_t>(words.size()*4);
    CHECK(sceGnmSubmitCommandBuffers(1,&address,&size,nullptr,nullptr)==0);
    words[1]=0; // DCB lifetime ends after submit; queued draw must still contain 3 vertices
    vk.begin(false);CHECK(recordFrame(vk.cmd,vk.view,vk.extent,second));
    vk.finish(false);CHECK(command_queue().acknowledge(second));vk.check_pixel(true);
    CHECK(command_queue().stats().submitted_draws==1);
    sceGnmShutdown();
    geometry_upload_regression(vk);
    std::puts("PM4 Vulkan: auto/indexed16/indexed32/offset draws, readback, transactional retry and GNM ABI integration passed");
}

void avplayer_vulkan_test(gravity::hle::avplayer::SceAvPlayerHandle handle) {
    using namespace gravity::hle::avplayer;
    VulkanTest vk;
    if(!vk.initialize()) { std::puts("SKIP: Vulkan 1.3 unavailable"); std::exit(77); }
    auto& pipeline = VulkanShaderPipeline::instance();
    CHECK(pipeline.initialize(vk.device, vk.physical, VK_FORMAT_R8G8B8A8_UNORM,vk.gpu,vk.family));
    CommandQueue queue;
    CHECK(queue.set_resolver(pipeline.get_resolver()));
    std::uintptr_t stable_address{};
    CHECK(sceAvPlayerStart(handle)==0);
    const auto started = std::chrono::steady_clock::now();
    for(unsigned i=0; i<120; ++i) {
        SceAvPlayerFrameInfoEx frame{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for(;;) {
            const auto result = sceAvPlayerGetVideoDataEx(handle, &frame);
            if(!result) break;
            CHECK(result == static_cast<std::int32_t>(0x80540002U));
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto address = reinterpret_cast<std::uintptr_t>(frame.pData);
        if(!i) stable_address = address;
        CHECK(address == stable_address && address % 256 == 0);
        Words words;
        add(words, 0x76, {0x10, static_cast<std::uint32_t>(address >> 8),
            static_cast<std::uint32_t>(address >> 40), (frame.width - 1) | ((frame.height - 1) << 14)});
        add(words, 0x2D, {4, 2});
        CHECK(enqueue(queue, words));
        vk.begin(true, [&] { CHECK(pipeline.prepare_video_textures(vk.cmd, i % 3, 3)); });
        RecordingTicket ticket;
        CHECK(queue.record(vk.cmd, vk.extent, ticket));
        vk.finish(); CHECK(queue.acknowledge(ticket));
        void* mapped{};
        CHECK(vkMapMemory(vk.device, vk.pixel_memory, 0, VK_WHOLE_SIZE, 0, &mapped)==VK_SUCCESS);
        const auto* rgba = static_cast<const std::uint8_t*>(mapped);
        const auto* expected = static_cast<const std::uint8_t*>(frame.pData);
        for(const auto y : {16U,48U}) for(unsigned channel=0; channel<4; ++channel) {
            const auto actual = rgba[(y*64+32)*4+channel];
            const auto source = expected[((y*frame.height/64)*frame.width+frame.width/2)*4+channel];
            CHECK(std::abs(int(actual)-int(source)) <= 2);
        }
        vkUnmapMemory(vk.device, vk.pixel_memory);
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    // Retire closed-player descriptors only after the frame-slot fence.
    CHECK(sceAvPlayerStop(handle)==0);
    CHECK(sceAvPlayerAddSource(handle, "/avtest/fixture.mp4")==0);
    vk.begin(true, [&] { CHECK(pipeline.prepare_video_textures(vk.cmd, 0, 3)); }); vk.finish();
    pipeline.reset_video_textures();
    pipeline.shutdown();
    CHECK(vk.validation_errors==0);
    std::printf("AvPlayer H.264 1080p60 -> dynamic Vulkan sampler: 120 alternating frames, 3 slots, RGBA readback passed; %.3f s, %.1f offscreen fps\n",
        elapsed, 120 / elapsed);
}

