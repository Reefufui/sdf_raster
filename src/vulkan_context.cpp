#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

#include "vulkan_context.hpp"
#include "utils.hpp"

static VKAPI_ATTR VkResult VKAPI_CALL vk_create_debug_utils_messenger_ext(
    VkInstance a_instance,
    const VkDebugUtilsMessengerCreateInfoEXT* a_create_info,
    const VkAllocationCallbacks* a_allocator,
    VkDebugUtilsMessengerEXT* a_debug_messenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(a_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(a_instance, a_create_info, a_allocator, a_debug_messenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static VKAPI_ATTR void VKAPI_CALL vk_destroy_debug_utils_messenger_ext(
    VkInstance a_instance,
    VkDebugUtilsMessengerEXT a_debug_messenger,
    const VkAllocationCallbacks* a_allocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(a_instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(a_instance, a_debug_messenger, a_allocator);
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT /*message_severity*/,
    VkDebugUtilsMessageTypeFlagsEXT /*message_type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* a_callback_data,
    void* /*p_user_data*/) {
    std::cerr << "Vulkan Validation Layer: " << a_callback_data->pMessage << std::endl;
    return VK_FALSE;
}

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

SwapChainSupportDetails query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;
    MY_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities));

    uint32_t format_count;
    MY_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr));
    if (format_count != 0) {
        details.formats.resize(format_count);
        MY_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data()));
    }

    uint32_t present_mode_count;
    MY_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr));
    if (present_mode_count != 0) {
        details.present_modes.resize(present_mode_count);
        MY_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data()));
    }
    return details;
}

VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats) {
    for (const auto& available_format : available_formats) {
        if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return available_format;
        }
    }
    return available_formats[0];
}

VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes) {
    for (const auto& available_present_mode : available_present_modes) {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) { // Предпочтительнее для низкой задержки
            return available_present_mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities, int width, int height) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actual_extent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        actual_extent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actual_extent.width));
        actual_extent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actual_extent.height));
        return actual_extent;
    }
}

VulkanContext::VulkanContext() : vk_utils::VulkanContext(vk_utils::globalContextGet()) {
    assert (vk_utils::globalContextIsInitialized());
}

VulkanContext::~VulkanContext() {
    shutdown();
    vk_utils::globalContextDestroy();
}

void VulkanContext::init(GLFWwindow* a_window, int a_width, int a_height) {
    this->window = a_window;
    setup_debug_messenger();
    MY_VK_CHECK(glfwCreateWindowSurface(this->instance, this->window, nullptr, &this->surface));
    create_swap_chain(a_width, a_height);
    create_image_views();
    create_render_pass();
    create_framebuffers();
    create_command_buffers();
    create_sync_objects();
}

void VulkanContext::shutdown() {
    if (this->device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(this->device);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (this->render_finished_semaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(this->device, this->render_finished_semaphores[i], nullptr);
        if (this->image_available_semaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(this->device, this->image_available_semaphores[i], nullptr);
        if (this->in_flight_fences[i] != VK_NULL_HANDLE) vkDestroyFence(this->device, this->in_flight_fences[i], nullptr);
    }
    this->render_finished_semaphores.clear();
    this->image_available_semaphores.clear();
    this->in_flight_fences.clear();

    for (auto framebuffer : this->swap_chain_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(this->device, framebuffer, nullptr);
    }
    this->swap_chain_framebuffers.clear();

    if (this->render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(this->device, this->render_pass, nullptr);
    this->render_pass = VK_NULL_HANDLE;

    for (auto imageView : this->swap_chain_image_views) {
        if (imageView != VK_NULL_HANDLE) vkDestroyImageView(this->device, imageView, nullptr);
    }
    this->swap_chain_image_views.clear();

    if (this->swap_chain != VK_NULL_HANDLE) vkDestroySwapchainKHR(this->device, this->swap_chain, nullptr);
    this->swap_chain = VK_NULL_HANDLE;

    if (this->debug_messenger != VK_NULL_HANDLE) {
        vk_destroy_debug_utils_messenger_ext(this->instance, this->debug_messenger, nullptr);
        this->debug_messenger = VK_NULL_HANDLE;
    }

    if (this->surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(this->instance, this->surface, nullptr);
    this->surface = VK_NULL_HANDLE;

    vk_utils::globalContextDestroy();
}

void VulkanContext::resize(int a_width, int a_height) {
    if (a_width == 0 || a_height == 0) {
        return;
    }

    vkDeviceWaitIdle(this->device);

    for (auto framebuffer : this->swap_chain_framebuffers) {
        vkDestroyFramebuffer(this->device, framebuffer, nullptr);
    }
    this->swap_chain_framebuffers.clear();

    for (auto imageView : this->swap_chain_image_views) {
        vkDestroyImageView(this->device, imageView, nullptr);
    }
    this->swap_chain_image_views.clear();

    vkDestroySwapchainKHR(this->device, this->swap_chain, nullptr);
    this->swap_chain = VK_NULL_HANDLE;

    create_swap_chain(a_width, a_height);
    create_image_views();
    create_framebuffers();
}

void VulkanContext::setup_debug_messenger() {
    VkDebugUtilsMessengerCreateInfoEXT create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = debug_callback;

    MY_VK_CHECK(vk_create_debug_utils_messenger_ext(this->instance, &create_info, nullptr, &this->debug_messenger));
}

void VulkanContext::create_swap_chain(int width, int height) {
    SwapChainSupportDetails swap_chain_support = query_swap_chain_support(this->physicalDevice, this->surface);

    VkSurfaceFormatKHR surface_format = choose_swap_surface_format(swap_chain_support.formats);
    VkPresentModeKHR present_mode = choose_swap_present_mode(swap_chain_support.present_modes);
    VkExtent2D extent = choose_swap_extent(swap_chain_support.capabilities, width, height);

    uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = this->surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;
    create_info.preTransform = swap_chain_support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    MY_VK_CHECK(vkCreateSwapchainKHR(this->device, &create_info, nullptr, &this->swap_chain));

    MY_VK_CHECK(vkGetSwapchainImagesKHR(this->device, this->swap_chain, &image_count, nullptr));
    this->swap_chain_images.resize(image_count);
    MY_VK_CHECK(vkGetSwapchainImagesKHR(this->device, this->swap_chain, &image_count, this->swap_chain_images.data()));

    this->swap_chain_image_format = surface_format.format;
    this->swap_chain_extent = extent;
}

void VulkanContext::create_image_views() {
    this->swap_chain_image_views.resize(this->swap_chain_images.size());
    for (size_t i = 0; i < this->swap_chain_images.size(); i++) {
        VkImageViewCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = this->swap_chain_images[i];
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = this->swap_chain_image_format;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;

        MY_VK_CHECK(vkCreateImageView(this->device, &create_info, nullptr, &this->swap_chain_image_views[i]));
    }
}

void VulkanContext::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = this->swap_chain_image_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    MY_VK_CHECK(vkCreateRenderPass(this->device, &render_pass_info, nullptr, &this->render_pass));
}

void VulkanContext::create_framebuffers() {
    this->swap_chain_framebuffers.resize(this->swap_chain_image_views.size());
    for (size_t i = 0; i < this->swap_chain_image_views.size(); i++) {
        VkImageView attachments[] = {
            this->swap_chain_image_views[i]
        };

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = this->render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = this->swap_chain_extent.width;
        framebuffer_info.height = this->swap_chain_extent.height;
        framebuffer_info.layers = 1;

        MY_VK_CHECK(vkCreateFramebuffer(this->device, &framebuffer_info, nullptr, &this->swap_chain_framebuffers[i]));
    }
}

void VulkanContext::create_command_buffers() {
    this->command_buffers.resize(this->swap_chain_framebuffers.size());

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = this->commandPool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t)this->command_buffers.size();

    MY_VK_CHECK(vkAllocateCommandBuffers(this->device, &alloc_info, this->command_buffers.data()));
}

void VulkanContext::create_sync_objects() {
    this->image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    this->render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
    this->in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        MY_VK_CHECK(vkCreateSemaphore(this->device, &semaphore_info, nullptr, &this->image_available_semaphores[i]));
        MY_VK_CHECK(vkCreateSemaphore(this->device, &semaphore_info, nullptr, &this->render_finished_semaphores[i]));
        MY_VK_CHECK(vkCreateFence(this->device, &fence_info, nullptr, &this->in_flight_fences[i]));
    }
}

VkCommandBuffer VulkanContext::begin_frame() {
    vkWaitForFences(this->device, 1, &this->in_flight_fences[this->current_frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(this->device, this->swap_chain, UINT64_MAX, this->image_available_semaphores[this->current_frame], VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        int width, height;
        glfwGetFramebufferSize(this->window, &width, &height);
        resize(width, height);
        return VK_NULL_HANDLE;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vkResetFences(this->device, 1, &this->in_flight_fences[this->current_frame]);
    vkResetCommandBuffer(this->command_buffers[image_index], 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    MY_VK_CHECK(vkBeginCommandBuffer(this->command_buffers[image_index], &begin_info));

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->render_pass;
    render_pass_info.framebuffer = this->swap_chain_framebuffers[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = this->swap_chain_extent;

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(this->command_buffers[image_index], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    return this->command_buffers[image_index];
}

void VulkanContext::end_frame(VkCommandBuffer command_buffer) {
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdEndRenderPass(command_buffer);
    MY_VK_CHECK(vkEndCommandBuffer(command_buffer));

    uint32_t image_index = 0;
    for (uint32_t i = 0; i < this->command_buffers.size(); ++i) {
        if (this->command_buffers[i] == command_buffer) {
            image_index = i;
            break;
        }
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {this->image_available_semaphores[this->current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    VkSemaphore signal_semaphores[] = {this->render_finished_semaphores[this->current_frame]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    MY_VK_CHECK(vkQueueSubmit(this->computeQueue, 1, &submit_info, this->in_flight_fences[this->current_frame]));

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swap_chains[] = {this->swap_chain};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &image_index;
    present_info.pResults = nullptr;

    VkResult result = vkQueuePresentKHR(this->computeQueue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        int width, height;
        glfwGetFramebufferSize(this->window, &width, &height);
        resize(width, height);
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    this->current_frame = (this->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

uint32_t VulkanContext::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(this->physicalDevice, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanContext::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    MY_VK_CHECK(vkCreateBuffer(this->device, &buffer_info, nullptr, &buffer));

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(this->device, buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

    MY_VK_CHECK(vkAllocateMemory(this->device, &alloc_info, nullptr, &buffer_memory));
    MY_VK_CHECK(vkBindBufferMemory(this->device, buffer, buffer_memory, 0));
}

void VulkanContext::create_image_and_view(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                           VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& image_memory, VkImageView& image_view) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    MY_VK_CHECK(vkCreateImage(this->device, &image_info, nullptr, &image));

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(this->device, image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

    MY_VK_CHECK(vkAllocateMemory(this->device, &alloc_info, nullptr, &image_memory));
    MY_VK_CHECK(vkBindImageMemory(this->device, image, image_memory, 0));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    MY_VK_CHECK(vkCreateImageView(this->device, &view_info, nullptr, &image_view));
}

void VulkanContext::transition_image_layout(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        cmd_buf,
        source_stage, destination_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void VulkanContext::copy_buffer_to_image(VkCommandBuffer cmd_buf, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd_buf, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

