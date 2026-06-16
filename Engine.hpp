#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <functional>
#include <queue>
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <iostream>

#define WIDTH 600
#define HEIGHT 600
#define IMAGE_COUNT 3

#define BACKGROUND_SHADER "shaders/bg"
#define FIGURE_SHADER "shaders/figure"

struct Figure
{
    short type;
};

struct Circle
{
    // Your existing custom constructor
    Circle(float cx,
           float cy,
           float radius,
           float r, float g, float b) : cx(cx), cy(cy), radius(radius), r(r), g(g), b(b)
    {
    }

    float cx;
    float cy;
    float radius;
    float r, g, b;
};

struct BgPushData
{
    float _pad;
};

class IGuiLayer
{
public:
    virtual ~IGuiLayer() = default;
    virtual void onGui() = 0;
    virtual void onAttach() {}
    virtual void onDetach() {}
};

void chk(VkResult action, const char *errorMessage)
{
    if (action != VK_SUCCESS)
    {
        printf("%s\n", errorMessage);
        getchar();
        exit(EXIT_FAILURE);
    }
}

static void imguiVulkanCheckResult(VkResult err)
{
    chk(err, "ImGui Vulkan error!");
}

uint32_t *readFile(const char *file_name, uint32_t *file_size)
{
    FILE *file = fopen(file_name, "rb");
    if (!file)
    {
        perror("Error opening file");
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error seeking to end of file\n");
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0)
    {
        fprintf(stderr, "Error getting file size\n");
        fclose(file);
        return NULL;
    }

    rewind(file);

    uint32_t *buffer = (uint32_t *)malloc(size);
    if (!buffer)
    {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, size, file);
    fclose(file);

    if (bytesRead != (size_t)size)
    {
        fprintf(stderr, "Error reading file (expected %ld, got %zu)\n", size, bytesRead);
        free(buffer);
        return NULL;
    }

    *file_size = (uint32_t)size;
    return buffer;
}

struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()> &&function)
    {
        deletors.push_back(function);
    }

    void flush()
    {
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
        {
            (*it)();
        }
        deletors.clear();
    }
};

class Engine
{
private:
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue queue;
    bool isInitialized = false;
    bool resizeRequested = false;

    VkSwapchainKHR swapChain;
    VkImage swapChainImages[IMAGE_COUNT];
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    VkImageView swapChainImageViews[IMAGE_COUNT];

    VkRenderPass renderPass;
    VkFramebuffer swapChainFramebuffers[IMAGE_COUNT];

    // subpass 0 - background
    VkPipeline bgPipeline;
    VkPipelineLayout bgPipelineLayout;

    typedef void (*BgDataCallback)(VkCommandBuffer cmd, VkPipelineLayout layout, void *userdata);
    typedef void (*BgSetupCallback)(VkDescriptorSetLayout *outLayout, uint32_t *outPushConstantSize, void *userdata);
    typedef void (*BgUpdateCallback)(void *userdata);

    BgSetupCallback bgSetupCallback = nullptr;
    BgDataCallback bgDataCallback = nullptr;
    BgUpdateCallback bgUpdateCallback = nullptr;
    void *bgUserdata = nullptr;

    // subpass 1 - figures
    VkPipeline figurePipeline;
    VkPipelineLayout figurePipelineLayout;

    VkCommandPool commandPool;
    VkCommandBuffer commandBuffers[IMAGE_COUNT];
    VkSemaphore imageAvailableSemaphores[IMAGE_COUNT];
    VkSemaphore renderFinishedSemaphores[IMAGE_COUNT];
    VkFence inFlightFences[IMAGE_COUNT];
    uint32_t currentFrame = 0;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
    VkFramebuffer imguiFramebuffers[IMAGE_COUNT];
    VkCommandBuffer imguiCommandBuffers[IMAGE_COUNT];
    VkSemaphore imguiFinishedSemaphores[IMAGE_COUNT];

    DeletionQueue mainDeletionQueue;
    DeletionQueue swapchainDeletionQueue;

    std::vector<IGuiLayer *> guiLayers;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        printf("failed to find suitable memory type!\n");
        exit(EXIT_FAILURE);
    }

    void createInstance()
    {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Vulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Super Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        uint32_t count;
        const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&count);
        const char *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = count;
        createInfo.ppEnabledExtensionNames = glfwExtensions;
        createInfo.enabledLayerCount = sizeof(validationLayers) / sizeof(validationLayers[0]);
        createInfo.ppEnabledLayerNames = validationLayers;

        chk(vkCreateInstance(&createInfo, NULL, &instance), "failed to create vulkan instance!");
    }

    void createSurface()
    {
        chk(glfwCreateWindowSurface(instance, window, NULL, &surface), "failed to create window surface!");
    }

    void pickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);

        if (deviceCount == 0)
        {
            printf("failed to find GPUs with Vulkan support!");
            getchar();
            exit(EXIT_FAILURE);
        }

        VkPhysicalDevice *devices = new VkPhysicalDevice[deviceCount];
        chk(vkEnumeratePhysicalDevices(instance, &deviceCount, devices), "failed to filling devices variable!");
        physicalDevice = devices[0];
        free(devices);
    }

    void createLogicalDevice()
    {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = 0;
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        const char *deviceExtensions[] = {"VK_KHR_swapchain"};

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.ppEnabledExtensionNames = deviceExtensions;
        createInfo.enabledExtensionCount = 1;
        createInfo.enabledLayerCount = 0;

        chk(vkCreateDevice(physicalDevice, &createInfo, NULL, &device), "failed to create logical device!");

        vkGetDeviceQueue(device, 0, 0, &queue);
    }

    void createSwapChain(int width, int height)
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
        VkExtent2D extent;
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            extent = capabilities.currentExtent;
        }
        else
        {
            extent.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = surface;
        createInfo.minImageCount = IMAGE_COUNT;
        createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        chk(vkCreateSwapchainKHR(device, &createInfo, NULL, &swapChain), "failed to create swap chain!");

        uint32_t count;
        vkGetSwapchainImagesKHR(device, swapChain, &count, NULL);
        vkGetSwapchainImagesKHR(device, swapChain, &count, swapChainImages);

        swapChainImageFormat = createInfo.imageFormat;
        swapChainExtent = createInfo.imageExtent;

        swapchainDeletionQueue.push_function([=]()
                                             { vkDestroySwapchainKHR(device, swapChain, nullptr); });
    }

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        viewInfo.subresourceRange.aspectMask = aspectFlags;

        VkImageView imageView;
        chk(vkCreateImageView(device, &viewInfo, NULL, &imageView), "failed to create render pass!");

        return imageView;
    }

    void createImageViews()
    {
        for (size_t i = 0; i < IMAGE_COUNT; i++)
        {
            swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        }
    }

    void createRenderPass()
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // subpass 0 - background
        VkSubpassDescription subpassBg = {};
        subpassBg.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassBg.colorAttachmentCount = 1;
        subpassBg.pColorAttachments = &colorAttachmentRef;

        // subpass 1 - figures
        VkSubpassDescription subpassFigures = {};
        subpassFigures.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassFigures.colorAttachmentCount = 1;
        subpassFigures.pColorAttachments = &colorAttachmentRef;

        VkSubpassDescription subpasses[2] = {subpassBg, subpassFigures};

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // Dependency 1: subpass 0 -> subpass 1
        VkSubpassDependency dep1 = {};
        dep1.srcSubpass = 0;
        dep1.dstSubpass = 1;
        dep1.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep1.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep1.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency dependencys[2] = {dependency, dep1};

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 2;
        renderPassInfo.pSubpasses = subpasses;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencys;

        chk(vkCreateRenderPass(device, &renderPassInfo, NULL, &renderPass), "failed to create render pass!");

        mainDeletionQueue.push_function([=]()
                                        { vkDestroyRenderPass(device, renderPass, nullptr); });
    }

    void createImGuiRenderPass()
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        chk(vkCreateRenderPass(device, &renderPassInfo, NULL, &imguiRenderPass), "failed to create imgui render pass!");

        mainDeletionQueue.push_function([=]()
                                        { vkDestroyRenderPass(device, imguiRenderPass, nullptr); });
    }

    void createFramebuffers()
    {
        for (size_t i = 0; i < IMAGE_COUNT; i++)
        {
            VkImageView attachments[] = {swapChainImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = (uint32_t)(sizeof(attachments) / sizeof(attachments[0]));
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            chk(vkCreateFramebuffer(device, &framebufferInfo, NULL, &swapChainFramebuffers[i]), "failed to create framebuffer!");

            swapchainDeletionQueue.push_function([=]()
                                                 {
                vkDestroyFramebuffer(device, swapChainFramebuffers[i], nullptr);
                vkDestroyImageView(device, swapChainImageViews[i], nullptr); });
        }
    }

    void createImGuiFramebuffers()
    {
        for (size_t i = 0; i < IMAGE_COUNT; i++)
        {
            VkImageView attachments[] = {swapChainImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = imguiRenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;

            chk(vkCreateFramebuffer(device, &framebufferInfo, NULL, &imguiFramebuffers[i]), "failed to create imgui framebuffer!");

            swapchainDeletionQueue.push_function([=]()
                                                 { vkDestroyFramebuffer(device, imguiFramebuffers[i], nullptr); });
        }
    }

    VkShaderModule createShaderModule(const uint32_t *code, uint32_t file_size)
    {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = file_size;
        createInfo.pCode = code;

        VkShaderModule shaderModule;
        chk(vkCreateShaderModule(device, &createInfo, NULL, &shaderModule), "failed to create shader module!");

        return shaderModule;
    }

    void createPipelines()
    {
        VkPipelineVertexInputStateCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia = {};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp = {};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn = {};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkPipelineRasterizationStateCreateInfo rast = {};
        rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL;
        rast.lineWidth = 1.0f;
        rast.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms = {};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendOff = {};
        blendOff.blendEnable = VK_FALSE;
        blendOff.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbOff = {};
        cbOff.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbOff.attachmentCount = 1;
        cbOff.pAttachments = &blendOff;

        VkPipelineLayoutCreateInfo bgLayoutCI = {};
        bgLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        uint32_t bgPushConstantSize = 0;
        VkDescriptorSetLayout bgDescriptorLayout = {};
        if (bgSetupCallback)
        {
            bgSetupCallback(&bgDescriptorLayout, &bgPushConstantSize, bgUserdata);
            bgLayoutCI.setLayoutCount = 1;
            bgLayoutCI.pSetLayouts = &bgDescriptorLayout;
        }
        // TODO: Change system
        if (bgPushConstantSize > 0)
        {
            VkPushConstantRange bgPcRange = {};
            bgPcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            bgPcRange.offset = 0;
            bgPcRange.size = bgPushConstantSize;
            bgLayoutCI.pushConstantRangeCount = 1;
            bgLayoutCI.pPushConstantRanges = &bgPcRange;
        }

        chk(vkCreatePipelineLayout(device, &bgLayoutCI, NULL, &bgPipelineLayout),
            "failed to create bg pipeline layout!");
        mainDeletionQueue.push_function([=]()
                                        { vkDestroyPipelineLayout(device, bgPipelineLayout, nullptr); });

        uint32_t vsz, fsz;
        auto *bv = readFile(BACKGROUND_SHADER ".vert.spv", &vsz);
        auto *bf = readFile(BACKGROUND_SHADER ".frag.spv", &fsz);
        VkShaderModule bgVert = createShaderModule(bv, vsz);
        VkShaderModule bgFrag = createShaderModule(bf, fsz);
        free(bv);
        free(bf);

        VkPipelineShaderStageCreateInfo bgStages[2];
        bgStages[0] = {};
        bgStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        bgStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        bgStages[0].module = bgVert;
        bgStages[0].pName = "main";
        bgStages[1] = {};
        bgStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        bgStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        bgStages[1].module = bgFrag;
        bgStages[1].pName = "main";

        VkGraphicsPipelineCreateInfo bgPI = {};
        bgPI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        bgPI.stageCount = 2;
        bgPI.pStages = bgStages;
        bgPI.pVertexInputState = &vi;
        bgPI.pInputAssemblyState = &ia;
        bgPI.pViewportState = &vp;
        bgPI.pRasterizationState = &rast;
        bgPI.pMultisampleState = &ms;
        bgPI.pColorBlendState = &cbOff;
        bgPI.pDynamicState = &dyn;
        bgPI.layout = bgPipelineLayout;
        bgPI.renderPass = renderPass;
        bgPI.subpass = 0;

        chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &bgPI, NULL, &bgPipeline),
            "failed to create bg pipeline!");
        mainDeletionQueue.push_function([=]()
                                        { vkDestroyPipeline(device, bgPipeline, nullptr); });

        vkDestroyShaderModule(device, bgVert, nullptr);
        vkDestroyShaderModule(device, bgFrag, nullptr);

        VkPipelineColorBlendAttachmentState blendOn = {};
        blendOn.blendEnable = VK_TRUE;
        blendOn.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendOn.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendOn.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendOn.colorBlendOp = VK_BLEND_OP_ADD;
        blendOn.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendOn.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendOn.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cbOn = {};
        cbOn.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbOn.attachmentCount = 1;
        cbOn.pAttachments = &blendOn;

        VkPushConstantRange pcRange = {};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(Circle);

        VkPipelineLayoutCreateInfo circleLayoutCI = {};
        circleLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        circleLayoutCI.pushConstantRangeCount = 1;
        circleLayoutCI.pPushConstantRanges = &pcRange;
        chk(vkCreatePipelineLayout(device, &circleLayoutCI, NULL, &figurePipelineLayout),
            "failed to create circle pipeline layout!");
        mainDeletionQueue.push_function([=]()
                                        { vkDestroyPipelineLayout(device, figurePipelineLayout, nullptr); });

        auto *cv = readFile(FIGURE_SHADER ".vert.spv", &vsz);
        auto *cf = readFile(FIGURE_SHADER ".frag.spv", &fsz);
        VkShaderModule circleVert = createShaderModule(cv, vsz);
        VkShaderModule circleFrag = createShaderModule(cf, fsz);
        free(cv);
        free(cf);

        VkPipelineShaderStageCreateInfo circleStages[2];
        circleStages[0] = {};
        circleStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        circleStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        circleStages[0].module = circleVert;
        circleStages[0].pName = "main";
        circleStages[1] = {};
        circleStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        circleStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        circleStages[1].module = circleFrag;
        circleStages[1].pName = "main";

        VkGraphicsPipelineCreateInfo circlePI = {};
        circlePI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        circlePI.stageCount = 2;
        circlePI.pStages = circleStages;
        circlePI.pVertexInputState = &vi;
        circlePI.pInputAssemblyState = &ia;
        circlePI.pViewportState = &vp;
        circlePI.pRasterizationState = &rast;
        circlePI.pMultisampleState = &ms;
        circlePI.pColorBlendState = &cbOn;
        circlePI.pDynamicState = &dyn;
        circlePI.layout = figurePipelineLayout;
        circlePI.renderPass = renderPass;
        circlePI.subpass = 1; // subpass 1

        chk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &circlePI, NULL, &figurePipeline),
            "failed to create circle pipeline!");
        mainDeletionQueue.push_function([=]()
                                        { vkDestroyPipeline(device, figurePipeline, nullptr); });

        vkDestroyShaderModule(device, circleVert, nullptr);
        vkDestroyShaderModule(device, circleFrag, nullptr);
    }

    void createCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = 0;

        chk(vkCreateCommandPool(device, &poolInfo, NULL, &commandPool), "failed to create command pool!");
        mainDeletionQueue.push_function([=]()
                                        { vkDestroyCommandPool(device, commandPool, nullptr); });
    }

    void createCommandBuffers()
    {
        VkCommandBuffer allBuffers[IMAGE_COUNT * 2];

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = IMAGE_COUNT * 2;

        chk(vkAllocateCommandBuffers(device, &allocInfo, allBuffers), "failed to allocate command buffers!");

        for (int i = 0; i < IMAGE_COUNT; i++)
        {
            commandBuffers[i] = allBuffers[i];
            imguiCommandBuffers[i] = allBuffers[IMAGE_COUNT + i];
        }
    }

    void createSyncObjects()
    {
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < IMAGE_COUNT; i++)
        {
            if (vkCreateSemaphore(device, &semaphoreInfo, NULL, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, NULL, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &semaphoreInfo, NULL, &imguiFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, NULL, &inFlightFences[i]) != VK_SUCCESS)
            {
                printf("failed to create synchronization objects for a frame!");
                getchar();
                exit(EXIT_FAILURE);
            }
            else
            {
                mainDeletionQueue.push_function([=]()
                                                {
                    vkDestroyFence(device, inFlightFences[i], nullptr);
                    vkDestroySemaphore(device, imguiFinishedSemaphores[i], nullptr);
                    vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
                    vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr); });
            }
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        vkResetCommandBuffer(commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        chk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "failed to begin recording command buffer!");

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
        renderPassInfo.renderArea.extent = swapChainExtent;

        VkClearValue clearValues[1] = {};
        clearValues[0].color = (VkClearColorValue){{0.0f, 0.0f, 0.0f, 1.0f}};

        renderPassInfo.clearValueCount = (uint32_t)(sizeof(clearValues) / sizeof(clearValues[0]));
        renderPassInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, bgPipeline);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)swapChainExtent.width;
        viewport.height = (float)swapChainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapChainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        if (bgDataCallback)
            bgDataCallback(commandBuffer, bgPipelineLayout, bgUserdata);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, figurePipeline);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        uint32_t figurecount = static_cast<uint32_t>(figures.size());
        for (uint32_t j = 0; j < figurecount; j++)
        {
            // [POI] Pass static sphere data as push constants
            vkCmdPushConstants(
                commandBuffer,
                figurePipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(Circle),
                &figures[j]);
            vkCmdDraw(commandBuffer, 6, 1, 0, 0);
        }

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            printf("failed to record command buffer!");
            getchar();
            exit(EXIT_FAILURE);
        }
    }

    void recordImGuiCommandBuffer(uint32_t imageIndex)
    {
        VkCommandBuffer cmd = imguiCommandBuffers[imageIndex];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        chk(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin imgui command buffer!");

        VkClearValue clearValue = {};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = imguiRenderPass;
        renderPassInfo.framebuffer = imguiFramebuffers[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapChainExtent;
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        vkCmdEndRenderPass(cmd);

        chk(vkEndCommandBuffer(cmd), "failed to end imgui command buffer!");
    }

    void createImGuiDescriptorPool()
    {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
        };

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 100;
        poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
        poolInfo.pPoolSizes = poolSizes;

        chk(vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiDescriptorPool),
            "failed to create imgui descriptor pool!");

        mainDeletionQueue.push_function([=]()
                                        { vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr); });
    }

    void initImGui()
    {
        createImGuiDescriptorPool();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window, true);

        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.Instance = instance;
        initInfo.PhysicalDevice = physicalDevice;
        initInfo.Device = device;
        initInfo.QueueFamily = 0;
        initInfo.Queue = queue;
        initInfo.DescriptorPool = imguiDescriptorPool;
        initInfo.PipelineInfoMain.RenderPass = imguiRenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.MinImageCount = IMAGE_COUNT;
        initInfo.ImageCount = IMAGE_COUNT;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.CheckVkResultFn = imguiVulkanCheckResult;

        ImGui_ImplVulkan_Init(&initInfo);

        // Download fonts (Not necessary yet)
        // {
        //     VkCommandBufferAllocateInfo allocInfo = {};
        //     allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        //     allocInfo.commandPool = commandPool;
        //     allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        //     allocInfo.commandBufferCount = 1;

        //     VkCommandBuffer fontCmd;
        //     vkAllocateCommandBuffers(device, &allocInfo, &fontCmd);

        //     VkCommandBufferBeginInfo beginInfo = {};
        //     beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        //     beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        //     vkBeginCommandBuffer(fontCmd, &beginInfo);

        //     // ImGui_ImplVulkan_CreateFontsTexture();

        //     vkEndCommandBuffer(fontCmd);

        //     VkSubmitInfo submitInfo = {};
        //     submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        //     submitInfo.commandBufferCount = 1;
        //     submitInfo.pCommandBuffers = &fontCmd;
        //     vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        //     vkQueueWaitIdle(queue);

        //     vkFreeCommandBuffers(device, commandPool, 1, &fontCmd);
        // }

        mainDeletionQueue.push_function([=]()
                                        {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(); });
    }

    static void framebufferResizeCallback(GLFWwindow *window, int width, int height)
    {
        Engine *engine = reinterpret_cast<Engine *>(glfwGetWindowUserPointer(window));
        engine->resizeRequested = true;
    }

    void resizeSwapchain()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        while (width == 0 || height == 0)
        {
            glfwGetFramebufferSize(window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(device);

        swapchainDeletionQueue.flush();

        createSwapChain(width, height);
        createImageViews();
        createFramebuffers();
        createImGuiFramebuffers();

        resizeRequested = false;
    }

public:
    GLFWwindow *window;
    std::vector<Circle> figures;

    void initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan window", NULL, NULL);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetWindowUserPointer(window, this);
    }

    void initVulkan()
    {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain(WIDTH, HEIGHT);
        createImageViews();
        createRenderPass();
        createImGuiRenderPass();
        createFramebuffers();
        createImGuiFramebuffers();
        createPipelines();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
        initImGui();
        isInitialized = true;
    }

    VkExtent2D getExtent() const { return swapChainExtent; }

    VkDevice getDevice() const { return device; }

    void pushLayer(IGuiLayer *layer)
    {
        layer->onAttach();
        guiLayers.push_back(layer);
    }

    void popLayer(IGuiLayer *layer)
    {
        layer->onDetach();
        guiLayers.erase(std::remove(guiLayers.begin(), guiLayers.end(), layer), guiLayers.end());
    }

    VkDescriptorSet createUbo(VkDeviceSize size, VkDescriptorSetLayout layout, void **outMapped, VkBuffer *outBuffer, VkDeviceMemory *outMemory)
    {
        VkBufferCreateInfo bufCI = {};
        bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCI.size = size;
        bufCI.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bufCI, NULL, outBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, *outBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device, &allocInfo, NULL, outMemory);
        vkBindBufferMemory(device, *outBuffer, *outMemory, 0);
        vkMapMemory(device, *outMemory, 0, size, 0, outMapped);

        VkDescriptorPool pool;
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolCI = {};
        poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCI.maxSets = 1;
        poolCI.poolSizeCount = 1;
        poolCI.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolCI, NULL, &pool);

        VkDescriptorSet ds;
        VkDescriptorSetAllocateInfo dsAlloc = {};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = pool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &layout;
        vkAllocateDescriptorSets(device, &dsAlloc, &ds);

        VkDescriptorBufferInfo bufInfo = {};
        bufInfo.buffer = *outBuffer;
        bufInfo.offset = 0;
        bufInfo.range = size;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = ds;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

        return ds;
    }

    void createDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo layoutCI, VkDescriptorSetLayout *outLayout)
    {
        chk(vkCreateDescriptorSetLayout(device, &layoutCI, NULL, outLayout), "failed to create descriptor set layout!");

        mainDeletionQueue.push_function([=]()
                                        { vkDestroyDescriptorSetLayout(device, *outLayout, nullptr); });
    }

    VkDescriptorSet createSSBO(VkDeviceSize size, VkDescriptorSetLayout layout, void **outMapped, VkBuffer *outBuffer, VkDeviceMemory *outMemory)
    {
        VkBufferCreateInfo bufCI = {};
        bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufCI.size = size;
        bufCI.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &bufCI, NULL, outBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(device, *outBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device, &allocInfo, NULL, outMemory);
        vkBindBufferMemory(device, *outBuffer, *outMemory, 0);
        vkMapMemory(device, *outMemory, 0, size, 0, outMapped);

        VkDescriptorPool pool;
        VkDescriptorPoolSize poolSize = {};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolCI = {};
        poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCI.maxSets = 1;
        poolCI.poolSizeCount = 1;
        poolCI.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolCI, NULL, &pool);

        mainDeletionQueue.push_function([=]()
                                        { vkDestroyDescriptorPool(device, pool, nullptr); });

        VkDescriptorSet ds;
        VkDescriptorSetAllocateInfo dsAlloc = {};
        dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAlloc.descriptorPool = pool;
        dsAlloc.descriptorSetCount = 1;
        dsAlloc.pSetLayouts = &layout;
        vkAllocateDescriptorSets(device, &dsAlloc, &ds);

        VkDescriptorBufferInfo bufInfo = {};
        bufInfo.buffer = *outBuffer;
        bufInfo.offset = 0;
        bufInfo.range = size;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = ds;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

        return ds;
    }

    void setBgCallbacks(BgSetupCallback setup, BgDataCallback data, BgUpdateCallback update, void *userdata)
    {
        bgSetupCallback = setup;
        bgDataCallback = data;
        bgUpdateCallback = update;
        bgUserdata = userdata;
    }

    void setBgDataCallback(BgDataCallback cb, void *userdata)
    {
        bgDataCallback = cb;
        bgUserdata = userdata;
    }

    void drawFrame()
    {
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        if (bgUpdateCallback)
            bgUpdateCallback(bgUserdata);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
                                                imageAvailableSemaphores[currentFrame],
                                                VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            resizeSwapchain();
            return;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        for (auto *layer : guiLayers)
        {
            layer->onGui();
        }

        ImGui::Render();

        vkResetCommandBuffer(commandBuffers[currentFrame], 0);

        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

        recordImGuiCommandBuffer(imageIndex);

        {
            VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            chk(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE),
                "failed to submit main command buffer!");
        }

        {
            VkSemaphore waitSemaphores[] = {renderFinishedSemaphores[currentFrame]};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {imguiFinishedSemaphores[currentFrame]};

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = waitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &imguiCommandBuffers[imageIndex];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = signalSemaphores;

            chk(vkQueueSubmit(queue, 1, &submitInfo, inFlightFences[currentFrame]),
                "failed to submit imgui command buffer!");
        }

        VkSwapchainKHR swapChains[] = {swapChain};
        VkSemaphore presentWait[] = {imguiFinishedSemaphores[currentFrame]};

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = presentWait;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(queue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
            resizeRequested = true;

        if (resizeRequested)
            resizeSwapchain();

        currentFrame = (currentFrame == IMAGE_COUNT - 1) ? 0 : currentFrame + 1;
    }

    void cleanup()
    {
        if (isInitialized)
        {
            vkDeviceWaitIdle(device);

            swapchainDeletionQueue.flush();
            mainDeletionQueue.flush();

            vkDestroyDevice(device, nullptr);
            vkDestroySurfaceKHR(instance, surface, nullptr);
            vkDestroyInstance(instance, nullptr);
            glfwDestroyWindow(window);
        }
    }
};
