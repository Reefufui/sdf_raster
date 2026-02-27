#include "gui.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <cassert>
#include <chrono>
#include <algorithm>

#include <GLFW/glfw3.h>

#include "logger.hpp"

namespace sdf_raster {

namespace gui {

class UI {
public:
    UI (const UI&) = delete;
    UI& operator= (const UI&) = delete;
    ~UI ();

    static UI& instance () {
        static UI ui;
        return ui;
    }

    void init (const InitInfo& info);
    void update (uint32_t width, uint32_t height, float delta_time);
    void draw (uint32_t image_index, VkCommandBuffer cmd_buff);
    void cleanup ();

private:
    UI () = default;

    struct DepthBuffer {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    } m_depthBuffer;

    void create_imgui_render_pass ();
    void create_depth_buffer ();
    void create_imgui_framebuffers ();

private:
    VkDevice       m_device           = VK_NULL_HANDLE;
    GLFWwindow*    m_window           = nullptr;
    VkInstance     m_instance         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue        m_queue            = VK_NULL_HANDLE;
    uint32_t       m_graphics_queue_family_index;

    std::vector <VkImageView> m_swapchainImageViews;
    VkExtent2D m_surfaceExtent        = {0, 0};
    VkFormat   m_surfaceFormat        = VK_FORMAT_UNDEFINED;
    VkFormat   m_depthFormat          = VK_FORMAT_UNDEFINED;

    VkDescriptorPool m_imguiPool         = VK_NULL_HANDLE;
    VkRenderPass     m_imguiRenderPass   = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_imguiFramebuffers;
};

UI::~UI () {
    this->cleanup ();
}

void UI::create_imgui_render_pass () {
    std::array <VkAttachmentDescription, 2> attachments {};

    attachments [0].format         = m_surfaceFormat;
    attachments [0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments [0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments [0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments [0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments [0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments [0].initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments [0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments [1].format         = m_depthFormat;
    attachments [1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments [1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments [1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments [1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments [1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments [1].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments [1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint        = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount     = 1;
    subpass.pColorAttachments        = &colorReference;
    subpass.pDepthStencilAttachment  = &depthReference;

    VkSubpassDependency dependency {};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo {};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast <uint32_t> (attachments.size ());
    renderPassInfo.pAttachments    = attachments.data ();
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass (m_device, &renderPassInfo, nullptr, &m_imguiRenderPass) != VK_SUCCESS) {
        throw std::runtime_error ("Failed to create ImGui render pass!");
    }

    LOG_INFO ("[UI] Created render pass.");
}

void UI::create_depth_buffer () {
    assert (m_physicalDevice != VK_NULL_HANDLE && "Physical device must be valid to create depth buffer.");
    assert (m_device != VK_NULL_HANDLE && "Device must be valid to create depth buffer.");
    assert (m_surfaceExtent.width > 0 && m_surfaceExtent.height > 0 && "Surface extent must be valid to create depth buffer.");
    assert (m_depthFormat != VK_FORMAT_UNDEFINED && "Depth format must be defined to create depth buffer.");

    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties (m_physicalDevice, m_depthFormat, &format_properties);

    VkImageCreateInfo image_info {};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = m_depthFormat;
    image_info.extent.width  = m_surfaceExtent.width;
    image_info.extent.height = m_surfaceExtent.height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage (m_device, &image_info, nullptr, &m_depthBuffer.image) != VK_SUCCESS) {
        throw std::runtime_error ("Failed to create depth image!");
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements (m_device, m_depthBuffer.image, &mem_req);

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties (m_physicalDevice, &mem_props);

    uint32_t type_index = UINT32_MAX;
    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_req.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            type_index = i;
            break;
        }
    }

    if (type_index == UINT32_MAX) {
        throw std::runtime_error ("Failed to find suitable memory type for depth buffer!");
    }

    VkMemoryAllocateInfo alloc_info {};
    alloc_info.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = type_index;

    if (vkAllocateMemory (m_device, &alloc_info, nullptr, &m_depthBuffer.memory) != VK_SUCCESS) {
        throw std::runtime_error ("Failed to allocate depth image memory!");
    }

     if (vkBindImageMemory (m_device, m_depthBuffer.image, m_depthBuffer.memory, 0) != VK_SUCCESS) {
        throw std::runtime_error ("Failed to bind depth image memory!");
    }

    VkImageViewCreateInfo view_info {};
    view_info.sType           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image           = m_depthBuffer.image;
    view_info.viewType        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format          = m_depthFormat;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (m_depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || m_depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
         view_info.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView (m_device, &view_info, nullptr, &m_depthBuffer.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view!");
    }

    LOG_INFO ("[UI] Created depth buffer.");
}

void UI::create_imgui_framebuffers () {
    assert (m_device != VK_NULL_HANDLE && "Device must be valid to create framebuffers.");
    assert (m_imguiRenderPass != VK_NULL_HANDLE && "ImGui RenderPass must be valid to create framebuffers.");
    assert (!m_swapchainImageViews.empty() && "Swapchain Image Views must not be empty.");
    assert (m_depthBuffer.view != VK_NULL_HANDLE && "Depth buffer view must be valid to create framebuffers.");
    assert (m_surfaceExtent.width > 0 && m_surfaceExtent.height > 0 && "Surface extent must be valid to create framebuffers.");

    m_imguiFramebuffers.resize (m_swapchainImageViews.size ());

    for (size_t i = 0; i < m_swapchainImageViews.size (); ++i) {
        std::array <VkImageView, 2> attachments;
        attachments [0] = m_swapchainImageViews [i];
        attachments [1] = m_depthBuffer.view;

        VkFramebufferCreateInfo framebuffer_info {};
        framebuffer_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass      = m_imguiRenderPass;
        framebuffer_info.attachmentCount = static_cast <uint32_t> (attachments.size ());
        framebuffer_info.pAttachments    = attachments.data ();
        framebuffer_info.width           = m_surfaceExtent.width;
        framebuffer_info.height          = m_surfaceExtent.height;
        framebuffer_info.layers          = 1;

        if (vkCreateFramebuffer (m_device, &framebuffer_info, nullptr, &m_imguiFramebuffers [i]) != VK_SUCCESS) {
            throw std::runtime_error ("Failed to create ImGui framebuffer for swapchain image " + std::to_string (i) + "!");
        }
    }

    LOG_INFO ("[UI] Created framebuffers.");
}

void UI::init (const InitInfo& info) {
    assert (info.device != VK_NULL_HANDLE && "InitInfo.device must be valid.");
    assert (info.window != nullptr && "InitInfo.window must be valid.");
    assert (info.instance != VK_NULL_HANDLE && "InitInfo.instance must be valid.");
    assert (info.physical_device != VK_NULL_HANDLE && "InitInfo.physical_device must be valid.");
    assert (info.graphics_queue != VK_NULL_HANDLE && "InitInfo.graphics_queue must be valid.");
    assert (info.graphics_queue_family_index != UINT32_MAX && "InitInfo.graphics_queue_family_index must be valid."); // UINT32_MAX обычно означает недействительный индекс
    assert (!info.swapchain_image_views.empty() && "InitInfo.swapchain_image_views must not be empty.");
    assert (info.surface_extent.width > 0 && info.surface_extent.height > 0 && "InitInfo.surface_extent must be valid.");
    assert (info.surface_format != VK_FORMAT_UNDEFINED && "InitInfo.surface_format must be defined.");
    assert (info.depth_format != VK_FORMAT_UNDEFINED && "InitInfo.depth_format must be defined.");

    m_device            = info.device;
    m_window            = info.window;
    m_instance          = info.instance;
    m_physicalDevice    = info.physical_device;
    m_queue             = info.graphics_queue;
    m_graphics_queue_family_index = info.graphics_queue_family_index;
    m_swapchainImageViews = info.swapchain_image_views;
    m_surfaceExtent     = info.surface_extent;
    m_surfaceFormat     = info.surface_format;
    m_depthFormat       = info.depth_format;

    this->create_depth_buffer ();
    this->create_imgui_render_pass ();
    this->create_imgui_framebuffers ();

    VkDescriptorPoolSize pool_size {};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1000;

    VkDescriptorPoolCreateInfo pool_info {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets       = 1000;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;

    if (vkCreateDescriptorPool (m_device, &pool_info, nullptr, &m_imguiPool) != VK_SUCCESS) {
        throw std::runtime_error ("Failed to create ImGui descriptor pool!");
    }

    ImGui::CreateContext ();
    LOG_INFO ("[UI] Created ImGui context.");
    ImGuiIO& io = ImGui::GetIO ();

    int window_w, window_h;
    int framebuffer_w, framebuffer_h;
    glfwGetWindowSize (m_window, &window_w, &window_h);
    glfwGetFramebufferSize (m_window, &framebuffer_w, &framebuffer_h);

    io.DisplaySize           = ImVec2 (static_cast <float> (window_w), static_cast <float> (window_h));
    io.DisplayFramebufferScale = ImVec2 (
        static_cast <float> (framebuffer_w) / static_cast <float> (window_w),
        static_cast <float> (framebuffer_h) / static_cast <float> (window_h)
    );
    io.DeltaTime = 1.0f / 60.0f;

    // ImGui_ImplVulkan_InitInfo im_init_info {};
    // im_init_info.Instance           = m_instance;
    // im_init_info.PhysicalDevice     = m_physicalDevice;
    // im_init_info.Device             = m_device;
    // im_init_info.Queue              = m_queue;
    // im_init_info.QueueFamily        = m_graphics_queue_family_index;
    // // im_init_info.DescriptorPool     = m_imguiPool;
    // im_init_info.DescriptorPool     = VK_NULL_HANDLE;
    // im_init_info.DescriptorPoolSize = 1000;
    // im_init_info.MinImageCount      = 2;
    // im_init_info.ImageCount         = static_cast <uint32_t> (m_swapchainImageViews.size ());
    // im_init_info.PipelineInfoMain.RenderPass = m_imguiRenderPass;
    // im_init_info.PipelineInfoMain.Subpass = 0;
    // im_init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // im_init_info.UseDynamicRendering = VK_FALSE;
    // im_init_info.PipelineCache = VK_NULL_HANDLE;
    // im_init_info.Allocator = VK_NULL_HANDLE;
    // im_init_info.CheckVkResultFn = check_vk_result;
    // im_init_info.ApiVersion     = VK_API_VERSION_1_4;

    VkPhysicalDeviceProperties props {};
    vkGetPhysicalDeviceProperties (m_physicalDevice, &props);
    LOG_INFO ("Physical device: {}", props.deviceName);


    // LOG_INFO ("[UI] Initing vulkan for imgui...");
    // ImGui_ImplVulkan_Init (&im_init_info);
    // LOG_INFO ("[UI] Inited vulkan for imgui.");

    ImGui::StyleColorsDark ();
}

void UI::update (uint32_t width, uint32_t height, float delta_time) {
    ImGuiIO& io = ImGui::GetIO ();
    io.DisplaySize = ImVec2 (static_cast <float> (width), static_cast <float> (height));
    io.DeltaTime = std::max (0.0001f, delta_time);

    // ImGui_ImplVulkan_NewFrame ();
    // ImGui::NewFrame ();

    ImGui::Begin ("Hello Vulkan ImGui");
    ImGui::Text ("Hello, Vulkan ImGui User!");
    ImGui::End ();

    ImGui::EndFrame ();
}

void UI::draw (uint32_t image_index, VkCommandBuffer cmd_buff) {
    if (image_index >= m_imguiFramebuffers.size ()) {
        throw std::out_of_range ("Invalid image index for ImGui framebuffer. Index: " + std::to_string (image_index) +
                                ", available framebuffers: " + std::to_string (m_imguiFramebuffers.size ()));
    }
    VkFramebuffer current_framebuffer = m_imguiFramebuffers [image_index];

    std::array <VkClearValue, 2> clear_value {};
    clear_value [0].color.float32 [0] = 0.0f; clear_value [0].color.float32 [1] = 0.0f;
    clear_value [0].color.float32 [2] = 0.0f; clear_value [0].color.float32 [3] = 0.0f;
    clear_value [1].depthStencil.depth = 1.0f;
    clear_value [1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo render_pass_begin {};
    render_pass_begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin.renderPass      = m_imguiRenderPass;
    render_pass_begin.framebuffer     = current_framebuffer;
    render_pass_begin.renderArea.offset = {0, 0};
    render_pass_begin.renderArea.extent = m_surfaceExtent;
    render_pass_begin.clearValueCount = static_cast <uint32_t> (clear_value.size ());
    render_pass_begin.pClearValues    = clear_value.data ();

    ImGui::Render ();

    vkCmdBeginRenderPass (cmd_buff, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
    // ImGui_ImplVulkan_RenderDrawData (ImGui::GetDrawData (), cmd_buff);
    vkCmdEndRenderPass (cmd_buff);
}

void UI::cleanup () {
    // ImGui_ImplVulkan_Shutdown ();
    ImGui::DestroyContext ();

    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    for (VkFramebuffer fb : m_imguiFramebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (m_device, fb, nullptr);
        }
    }
    m_imguiFramebuffers.clear ();

    if (m_imguiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass (m_device, m_imguiRenderPass, nullptr);
        m_imguiRenderPass = VK_NULL_HANDLE;
    }

    if (m_depthBuffer.view != VK_NULL_HANDLE) {
        vkDestroyImageView (m_device, m_depthBuffer.view, nullptr);
        m_depthBuffer.view = VK_NULL_HANDLE;
    }
    if (m_depthBuffer.image != VK_NULL_HANDLE) {
        vkDestroyImage (m_device, m_depthBuffer.image, nullptr);
        m_depthBuffer.image = VK_NULL_HANDLE;
    }
     if (m_depthBuffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory (m_device, m_depthBuffer.memory, nullptr);
        m_depthBuffer.memory = VK_NULL_HANDLE;
    }

    if (m_imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool (m_device, m_imguiPool, nullptr);
        m_imguiPool = VK_NULL_HANDLE;
    }

    m_swapchainImageViews.clear ();
    m_device = VK_NULL_HANDLE;
    m_window = nullptr;
    m_instance = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
}

void init (const InitInfo& info) {
    UI::instance ().init (info);
}

void update (uint32_t width, uint32_t height, float delta_time) {
    UI::instance ().update (width, height, delta_time);
}

void draw (uint32_t image_index, VkCommandBuffer cmd_buff) {
    UI::instance ().draw (image_index, cmd_buff);
}

void cleanup () {
    UI::instance ().cleanup ();
}

}

}

