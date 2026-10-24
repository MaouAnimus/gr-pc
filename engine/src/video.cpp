#include "hle/video.h"
#include "hle/gnm.h"
#include "hle/gnm/shader_pipeline.h"
#include "hle/kernel.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <thread>

namespace gravity::hle::video {

class VideoRenderer {
public:
    VideoRenderer() = default;
    ~VideoRenderer() {
        shutdown();
    }

    hle::Status initialize(bool headless = false) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (initialized_) {
            return {hle::Error::invalid_state};
        }

        mode_width_ = 1920;
        mode_height_ = 1080;
        mode_refresh_ = 60;
        last_frame_time_ = std::chrono::steady_clock::now();

        current_frame_ = 0;
        presented_frames_ = 0;
        render_failed_ = false;
        if (headless) {
            initialized_ = true;
            headless_ = true;
            return {};
        }
        // 1. Initialize SDL video
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        // 2. Create Window
        window_ = SDL_CreateWindow("Gravity Rush Remastered", 1280, 720,
                                   SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window_) {
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        // 3. Get Vulkan extensions
        Uint32 ext_count = 0;
        char const* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
        if (!sdl_exts || ext_count == 0) {
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        std::vector<const char*> extensions(sdl_exts, sdl_exts + ext_count);

        // 4. Create Vulkan Instance
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "Gravity Rush Remastered";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "GravityRushRuntime";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instance_ci{};
        instance_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_ci.pApplicationInfo = &app_info;
        instance_ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instance_ci.ppEnabledExtensionNames = extensions.data();

        if (vkCreateInstance(&instance_ci, nullptr, &instance_) != VK_SUCCESS) {
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        // 5. Create Surface
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        // 6. Select Physical Device
        uint32_t gpu_count = 0;
        vkEnumeratePhysicalDevices(instance_, &gpu_count, nullptr);
        if (gpu_count == 0) {
            cleanup_vulkan();
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        std::vector<VkPhysicalDevice> gpus(gpu_count);
        vkEnumeratePhysicalDevices(instance_, &gpu_count, gpus.data());
        physical_device_ = gpus[0];

        // Find queue family
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, queue_families.data());

        graphics_family_ = UINT32_MAX;
        for (uint32_t i = 0; i < queue_family_count; ++i) {
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &present_support);
            if ((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
                graphics_family_ = i;
                break;
            }
        }

        if (graphics_family_ == UINT32_MAX) {
            cleanup_vulkan();
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        // 7. Create Logical Device with Dynamic Rendering
        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo queue_ci{};
        queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_ci.queueFamilyIndex = graphics_family_;
        queue_ci.queueCount = 1;
        queue_ci.pQueuePriorities = &queue_priority;

        const char* device_extensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Enable Vulkan 1.3 Dynamic Rendering
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;

        VkDeviceCreateInfo device_ci{};
        device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_ci.pNext = &features13;
        device_ci.queueCreateInfoCount = 1;
        device_ci.pQueueCreateInfos = &queue_ci;
        device_ci.enabledExtensionCount = 1;
        device_ci.ppEnabledExtensionNames = device_extensions;

        if (vkCreateDevice(physical_device_, &device_ci, nullptr, &device_) != VK_SUCCESS) {
            cleanup_vulkan();
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);

        // 8. Create Command Pool
        VkCommandPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_ci.queueFamilyIndex = graphics_family_;
        if (vkCreateCommandPool(device_, &pool_ci, nullptr, &command_pool_) != VK_SUCCESS) {
            cleanup_vulkan();
            cleanup_sdl();
            return {hle::Error::unsupported};
        }

        if (!create_swapchain()) {
            cleanup_swapchain();
            cleanup_vulkan();
            cleanup_sdl();
            return {hle::Error::system_error};
        }

        // 9. Inform GNM subsystem of device and setup shader pipeline
        gnm::setDevice(device_);
        gnm::VulkanShaderPipeline::instance().initialize(device_, physical_device_, VK_FORMAT_B8G8R8A8_UNORM);
        gnm::command_queue().set_resolver(gnm::VulkanShaderPipeline::instance().get_resolver());

        initialized_ = true;
        headless_ = false;
        return {};
    }

    void shutdown() {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (!initialized_) return;

        if (!headless_) {
            if (device_ != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(device_);
            }
            gnm::VulkanShaderPipeline::instance().shutdown();
            cleanup_swapchain();
            cleanup_vulkan();
        } else {
            gnm::VulkanShaderPipeline::instance().shutdown();
        }

        gnm::setDevice(VK_NULL_HANDLE);
        cleanup_sdl();
        initialized_ = false;
        headless_ = false;
    }

    void setFrameBuf(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch, uint32_t format) {
        (void)addr; (void)pitch; (void)format;
        std::lock_guard<hle::Mutex> lock(mutex_);
        framebuf_width_ = width;
        framebuf_height_ = height;
    }

    void waitVblank() {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (!initialized_ || render_failed_) return;

        // Process SDL window events to keep OS window message pump alive
        if (window_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    // Quit requested
                }
            }
        }

        if (!headless_ && device_ != VK_NULL_HANDLE && swapchain_ != VK_NULL_HANDLE) {
            render_frame();
        } else {
            // Emulate 60 FPS pacing in headless / software fallback
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_frame_time_).count();
            constexpr int64_t frame_micros = 16666; // 60 Hz
            if (elapsed < frame_micros) {
                std::this_thread::sleep_for(std::chrono::microseconds(frame_micros - elapsed));
            }
            last_frame_time_ = std::chrono::steady_clock::now();
        }
    }

    hle::Status setMode(uint32_t width, uint32_t height, uint32_t refreshRate) {
        std::lock_guard<hle::Mutex> lock(mutex_);
        if (!initialized_ || !width || !height || !refreshRate) return {hle::Error::invalid_argument};
        mode_width_ = width;
        mode_height_ = height;
        mode_refresh_ = refreshRate;
        return {};
    }

    void getMode(uint32_t* width, uint32_t* height, uint32_t* refreshRate) const {
        if (width) *width = mode_width_;
        if (height) *height = mode_height_;
        if (refreshRate) *refreshRate = mode_refresh_;
    }

    [[nodiscard]] bool isInitialized() const noexcept { return initialized_; }
    uint64_t presentedFrames() const noexcept { return presented_frames_; }
    [[nodiscard]] bool isHeadless() const noexcept { return headless_; }

private:
    bool create_swapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);

        swapchain_extent_ = caps.currentExtent;
        if (swapchain_extent_.width == UINT32_MAX) {
            swapchain_extent_.width = 1280;
            swapchain_extent_.height = 720;
        }

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
            image_count = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR swap_ci{};
        swap_ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swap_ci.surface = surface_;
        swap_ci.minImageCount = image_count;
        swap_ci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        swap_ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swap_ci.imageExtent = swapchain_extent_;
        swap_ci.imageArrayLayers = 1;
        swap_ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swap_ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swap_ci.preTransform = caps.currentTransform;
        swap_ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swap_ci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync 60Hz
        swap_ci.clipped = VK_TRUE;

        if (vkCreateSwapchainKHR(device_, &swap_ci, nullptr, &swapchain_) != VK_SUCCESS) {
            return false;
        }

        uint32_t actual_count = 0;
        if (vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr) != VK_SUCCESS || !actual_count) return false;
        swapchain_images_.resize(actual_count);
        if (vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, swapchain_images_.data()) != VK_SUCCESS) return false;

        swapchain_image_views_.resize(actual_count);
        for (uint32_t i = 0; i < actual_count; ++i) {
            VkImageViewCreateInfo view_ci{};
            view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_ci.image = swapchain_images_[i];
            view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_ci.format = VK_FORMAT_B8G8R8A8_UNORM;
            view_ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_ci.subresourceRange.baseMipLevel = 0;
            view_ci.subresourceRange.levelCount = 1;
            view_ci.subresourceRange.baseArrayLayer = 0;
            view_ci.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_, &view_ci, nullptr, &swapchain_image_views_[i]) != VK_SUCCESS) return false;
        }

        // Allocate command buffers and sync primitives
        command_buffers_.resize(actual_count);
        VkCommandBufferAllocateInfo alloc_ci{};
        alloc_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_ci.commandPool = command_pool_;
        alloc_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_ci.commandBufferCount = actual_count;
        if (vkAllocateCommandBuffers(device_, &alloc_ci, command_buffers_.data()) != VK_SUCCESS) return false;

        image_available_semaphores_.resize(actual_count);
        render_finished_semaphores_.resize(actual_count);
        in_flight_fences_.resize(actual_count);

        VkSemaphoreCreateInfo sem_ci{};
        sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_ci{};
        fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < actual_count; ++i) {
            if (vkCreateSemaphore(device_, &sem_ci, nullptr, &image_available_semaphores_[i]) != VK_SUCCESS) return false;
            if (vkCreateSemaphore(device_, &sem_ci, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS) return false;
            if (vkCreateFence(device_, &fence_ci, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) return false;
        }
        return true;
    }

    void cleanup_swapchain() {
        if (device_ == VK_NULL_HANDLE) return;

        if (!command_buffers_.empty() && command_buffers_[0] != VK_NULL_HANDLE)
            vkFreeCommandBuffers(device_, command_pool_, static_cast<uint32_t>(command_buffers_.size()), command_buffers_.data());
        command_buffers_.clear();
        for (auto fence : in_flight_fences_) {
            if (fence) vkDestroyFence(device_, fence, nullptr);
        }
        in_flight_fences_.clear();

        for (auto sem : render_finished_semaphores_) {
            if (sem) vkDestroySemaphore(device_, sem, nullptr);
        }
        render_finished_semaphores_.clear();

        for (auto sem : image_available_semaphores_) {
            if (sem) vkDestroySemaphore(device_, sem, nullptr);
        }
        image_available_semaphores_.clear();

        for (auto view : swapchain_image_views_) {
            if (view) vkDestroyImageView(device_, view, nullptr);
        }
        swapchain_image_views_.clear();

        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void render_frame() {
        if (in_flight_fences_.empty()) return;

        if (vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, 5000000000ULL) != VK_SUCCESS) { render_failed_ = true; return; }

        uint32_t image_index = 0;
        VkResult res = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                             image_available_semaphores_[current_frame_],
                                             VK_NULL_HANDLE, &image_index);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(device_);
            cleanup_swapchain();
            current_frame_ = 0;
            if (!create_swapchain()) render_failed_ = true;
            return;
        }
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) { render_failed_ = true; return; }
        if (image_index >= swapchain_images_.size()) { render_failed_ = true; return; }
        if (vkResetFences(device_, 1, &in_flight_fences_[current_frame_]) != VK_SUCCESS) { render_failed_ = true; return; }

        VkCommandBuffer cmd = command_buffers_[current_frame_];
        if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) { render_failed_ = true; return; }

        VkCommandBufferBeginInfo begin_ci{};
        begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &begin_ci) != VK_SUCCESS) { render_failed_ = true; return; }

        // Transition image to COLOR_ATTACHMENT_OPTIMAL
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = swapchain_images_[image_index];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dep_info{};
        dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep_info.imageMemoryBarrierCount = 1;
        dep_info.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep_info);

        // Render frame with GNM translator
        gnm::RecordingTicket ticket;
        struct AbortRecording {
            gnm::RecordingTicket& ticket;
            ~AbortRecording() { gnm::command_queue().abort(ticket); }
        } abort_recording{ticket};
        const auto recorded = gnm::recordFrame(cmd, swapchain_image_views_[image_index], swapchain_extent_, ticket);
        if (!recorded) {
            static std::atomic<unsigned> rejected_count{};
            if (rejected_count.fetch_add(1) < 3) {
                std::cerr << "PM4/Vulkan recording notice at DWORD " << recorded.word_offset
                          << ", opcode " << recorded.opcode << ": " << recorded.message
                          << " (presenting clear frame while shader translation is pending)\n";
            }
            if (recorded.error != gnm::pm4::Error::resources) {
                render_failed_ = true;
                return;
            }
        }

        // Transition to PRESENT_SRC_KHR
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier2(cmd, &dep_info);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) { render_failed_ = true; return; }

        // Submit
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_available_semaphores_[current_frame_];
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_finished_semaphores_[current_frame_];

        if (vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) { render_failed_ = true; return; }

        if (ticket.id && !gnm::command_queue().acknowledge(ticket)) {
            render_failed_ = true; return;
        }
        // Present
        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &render_finished_semaphores_[current_frame_];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain_;
        present_info.pImageIndices = &image_index;

        res = vkQueuePresentKHR(graphics_queue_, &present_info);
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR && res != VK_ERROR_OUT_OF_DATE_KHR) {
            render_failed_ = true; return;
        }
        if (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR) ++presented_frames_;

        current_frame_ = (current_frame_ + 1) % swapchain_images_.size();
    }

    void cleanup_vulkan() {
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

    void cleanup_sdl() {
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }

    hle::Mutex mutex_{};
    bool initialized_{false};
    bool headless_{false};
    bool render_failed_{false};
    uint64_t presented_frames_{0};
    SDL_Window* window_{nullptr};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphics_queue_{VK_NULL_HANDLE};
    uint32_t graphics_family_{UINT32_MAX};

    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkExtent2D swapchain_extent_{1280, 720};
    std::vector<VkImage> swapchain_images_{};
    std::vector<VkImageView> swapchain_image_views_{};
    std::vector<VkCommandBuffer> command_buffers_{};

    std::vector<VkSemaphore> image_available_semaphores_{};
    std::vector<VkSemaphore> render_finished_semaphores_{};
    std::vector<VkFence> in_flight_fences_{};
    uint32_t current_frame_{0};

    uint32_t framebuf_width_{1920};
    uint32_t framebuf_height_{1080};
    uint32_t mode_width_{1920};
    uint32_t mode_height_{1080};
    uint32_t mode_refresh_{60};
    std::chrono::steady_clock::time_point last_frame_time_{};
};

static VideoRenderer g_videoRenderer;

VideoRenderer& get_video_renderer() noexcept {
    return g_videoRenderer;
}

bool video_is_initialized() noexcept {
    return g_videoRenderer.isInitialized();
}

extern "C" {

GRAVITY_SYSV_ABI gravity::hle::Status sceVideoOutInitialize() {
    return g_videoRenderer.initialize();
}

GRAVITY_SYSV_ABI void sceVideoOutShutdown() {
    g_videoRenderer.shutdown();
}

GRAVITY_SYSV_ABI void sceVideoOutSetFrameBuf(uint64_t addr, uint32_t width, uint32_t height,
                                            uint32_t pitch, uint32_t format) {
    g_videoRenderer.setFrameBuf(addr, width, height, pitch, format);
}

GRAVITY_SYSV_ABI void sceVideoOutWaitVblank() {
    g_videoRenderer.waitVblank();
}

GRAVITY_SYSV_ABI gravity::hle::Status sceVideoOutSetMode(uint32_t width, uint32_t height,
                                                        uint32_t refreshRate) {
    return g_videoRenderer.setMode(width, height, refreshRate);
}

GRAVITY_SYSV_ABI void sceVideoOutGetMode(uint32_t* width, uint32_t* height,
                                        uint32_t* refreshRate) {
    g_videoRenderer.getMode(width, height, refreshRate);
}

} // extern "C"

} // namespace gravity::hle::video
namespace gravity::hle::video {
bool video_is_headless() noexcept { return get_video_renderer().isHeadless(); }
hle::Status initialize_headless() { return get_video_renderer().initialize(true); }
}

namespace gravity::hle::video { uint64_t video_presented_frames() noexcept { return get_video_renderer().presentedFrames(); } }
