#include "vulkan_engine.hpp"
#include "embedded_shaders.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace umbra {

namespace {

const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
#ifdef __APPLE__
    , "VK_KHR_portability_subset"
#endif
};

const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};

#if defined(ENABLE_VALIDATION_LAYERS) && ENABLE_VALIDATION_LAYERS == 1
constexpr bool USE_VALIDATION = true;
#else
constexpr bool USE_VALIDATION = false;
#endif

} // namespace

void VulkanEngine::init(GLFWwindow* window) {
    window_ = window;
    dynCpuCopy_.fill(0);
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipelines();
    createCommandPool();
    createDepthResources();
    createDynamicLightResources();
    createTextureResources();
    createFramebuffers();
    createUniformBuffers();
    createHudBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
}

void VulkanEngine::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    cleanupSwapChain();
    flushDeferredDestroys(true);
    destroyAllChunkMeshes();

    if (blockTexSampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, blockTexSampler_, nullptr);
    if (blockTexView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_, blockTexView_, nullptr);
    if (blockTexImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, blockTexImage_, nullptr);
        vkFreeMemory(device_, blockTexMemory_, nullptr);
    }

    if (itemTexSampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, itemTexSampler_, nullptr);
    if (itemTexView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_, itemTexView_, nullptr);
    if (itemTexImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, itemTexImage_, nullptr);
        vkFreeMemory(device_, itemTexMemory_, nullptr);
    }

    if (dynLightSampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, dynLightSampler_, nullptr);
    if (dynLightImageView_ != VK_NULL_HANDLE)
        vkDestroyImageView(device_, dynLightImageView_, nullptr);
    if (dynLightImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, dynLightImage_, nullptr);
        vkFreeMemory(device_, dynLightImageMemory_, nullptr);
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (i < dynStagingBuffers_.size()) {
            vkDestroyBuffer(device_, dynStagingBuffers_[i], nullptr);
            vkFreeMemory(device_, dynStagingMemory_[i], nullptr);
        }
        if (i < uniformBuffers_.size()) {
            vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
            vkFreeMemory(device_, uniformBuffersMemory_[i], nullptr);
        }
        if (i < hudVertexBuffers_.size()) {
            vkDestroyBuffer(device_, hudVertexBuffers_[i], nullptr);
            vkFreeMemory(device_, hudVertexBuffersMemory_[i], nullptr);
        }
    }

    if (descriptorPool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    if (hudDescriptorSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_, hudDescriptorSetLayout_, nullptr);
    if (descriptorSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);

    if (hudPipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_, hudPipeline_, nullptr);
    if (hudPipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_, hudPipelineLayout_, nullptr);
    if (voxelPipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_, voxelPipeline_, nullptr);
    if (translucentPipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_, translucentPipeline_, nullptr);
    if (skyPipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (renderPass_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device_, renderPass_, nullptr);

    if (transferFence_ != VK_NULL_HANDLE)
        vkDestroyFence(device_, transferFence_, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (i < renderFinishedSemaphores_.size())
            vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
        if (i < imageAvailableSemaphores_.size())
            vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
        if (i < inFlightFences_.size())
            vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }

    if (commandPool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (device_ != VK_NULL_HANDLE)
        vkDestroyDevice(device_, nullptr);
    if (surface_ != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_ != VK_NULL_HANDLE)
        vkDestroyInstance(instance_, nullptr);
}

void VulkanEngine::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "Umbra Voxel Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
    appInfo.pEngineName        = "Umbra";
    appInfo.engineVersion      = VK_MAKE_VERSION(2, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts || glfwExtCount == 0) {
        throw std::runtime_error("GLFW failed to find required Vulkan instance extensions.\n"
                                 "Please ensure graphics drivers with Vulkan 1.2 support are installed.");
    }
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (USE_VALIDATION) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());

        bool found = false;
        for (const auto& lp : available) {
            if (std::strcmp(lp.layerName, VALIDATION_LAYERS[0]) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            createInfo.enabledLayerCount   = static_cast<uint32_t>(VALIDATION_LAYERS.size());
            createInfo.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        } else {
            std::cerr << "[WARN] Requested validation layer not installed; continuing without it.\n";
        }
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
}

void VulkanEngine::createSurface() {
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GLFW Vulkan surface.");
    }
}

void VulkanEngine::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("No Vulkan-compatible GPU found.");
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice dev : devices) {
        if (!isDeviceSuitable(dev)) continue;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDevice_ = dev;
            std::cout << "[UMBRA] Selected GPU: " << props.deviceName << "\n";
            return;
        }
        if (physicalDevice_ == VK_NULL_HANDLE) {
            physicalDevice_ = dev;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable Vulkan GPU.");
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    std::cout << "[UMBRA] Selected GPU: " << props.deviceName << "\n";
}

bool VulkanEngine::isDeviceSuitable(VkPhysicalDevice dev) const {
    QueueFamilyIndices indices = findQueueFamilies(dev);
    if (!indices.isComplete()) return false;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, available.data());

    std::set<std::string> required(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    if (!required.empty()) return false;

    SwapChainSupportDetails swap = querySwapChainSupport(dev);
    return !swap.formats.empty() && !swap.presentModes.empty();
}

VulkanEngine::QueueFamilyIndices VulkanEngine::findQueueFamilies(VkPhysicalDevice dev) const {
    QueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
        if (presentSupport == VK_TRUE) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
    }
    return indices;
}

VulkanEngine::SwapChainSupportDetails VulkanEngine::querySwapChainSupport(VkPhysicalDevice dev) const {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surface_, &details.capabilities);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &fmtCount, nullptr);
    if (fmtCount > 0) {
        details.formats.resize(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface_, &fmtCount, details.formats.data());
    }

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &pmCount, nullptr);
    if (pmCount > 0) {
        details.presentModes.resize(pmCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface_, &pmCount, details.presentModes.data());
    }
    return details;
}

void VulkanEngine::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    std::set<uint32_t> uniqueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos       = queueInfos.data();
    createInfo.pEnabledFeatures        = &features;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(DEVICE_EXTENSIONS.size());
    createInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device.");
    }

    vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
}

void VulkanEngine::createSwapChain() {
    SwapChainSupportDetails support = querySwapChainSupport(physicalDevice_);

    VkSurfaceFormatKHR surfaceFormat = support.formats[0];
    for (const auto& avail : support.formats) {
        if (avail.format == VK_FORMAT_B8G8R8A8_UNORM &&
            avail.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = avail;
            break;
        }
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& avail : support.presentModes) {
        if (avail == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = avail;
            break;
        }
    }

    VkExtent2D extent{};
    if (support.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = support.capabilities.currentExtent;
    } else {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window_, &w, &h);
        extent.width = std::clamp(static_cast<uint32_t>(w),
                                  support.capabilities.minImageExtent.width,
                                  support.capabilities.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(h),
                                   support.capabilities.minImageExtent.height,
                                   support.capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = surfaceFormat.format;
    ci.imageColorSpace  = surfaceFormat.colorSpace;
    ci.imageExtent      = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    uint32_t qFamilies[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsFamily != indices.presentFamily) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = qFamilies;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = presentMode;
    ci.clipped        = VK_TRUE;
    ci.oldSwapchain   = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapChain_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain.");
    }

    vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
    swapChainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, swapChainImages_.data());
    swapChainImageFormat_ = surfaceFormat.format;
    swapChainExtent_      = extent;
}

void VulkanEngine::createImageViews() {
    swapChainImageViews_.resize(swapChainImages_.size());
    for (size_t i = 0; i < swapChainImages_.size(); ++i) {
        swapChainImageViews_[i] = createImageView(
            swapChainImages_[i], swapChainImageFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void VulkanEngine::createRenderPass() {
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = swapChainImageFormat_;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAtt{};
    depthAtt.format         = findDepthFormat();
    depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAtt, depthAtt};
    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments    = attachments.data();
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass.");
    }
}

void VulkanEngine::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // Binding 0: Per-frame Uniform Buffer (camera, sun/sky, fog, quality flags)
    bindings[0].binding            = 0;
    bindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount    = 1;
    bindings[0].stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: Hardware-filtered 32x32x32 R8_UNORM Occluded Dynamic Light 3D Texture
    bindings[1].binding            = 1;
    bindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount    = 1;
    bindings[1].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 2: Real PNG Block Texture 2D Array (assets/texture/block/*.png)
    bindings[2].binding            = 2;
    bindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount    = 1;
    bindings[2].stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(device_, &info, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout.");
    }

    // HUD Descriptor Set Layout: Binding 0 = Real PNG Item Texture 2D Array (assets/texture/items/*.png)
    VkDescriptorSetLayoutBinding hudBinding{};
    hudBinding.binding            = 0;
    hudBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hudBinding.descriptorCount    = 1;
    hudBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    hudBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo hudInfo{};
    hudInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    hudInfo.bindingCount = 1;
    hudInfo.pBindings    = &hudBinding;

    if (vkCreateDescriptorSetLayout(device_, &hudInfo, nullptr, &hudDescriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HUD descriptor set layout.");
    }
}

void VulkanEngine::createGraphicsPipelines() {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &descriptorSetLayout_;

    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout.");
    }

    std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynInfo.pDynamicStates    = dynStates.data();

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.scissorCount  = 1;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cbAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cbAtt;

    // 1. Voxel Terrain Pipeline
    VkShaderModule voxVert = createShaderModule(readBinaryFile("assets/shaders/voxel.vert.spv"));
    VkShaderModule voxFrag = createShaderModule(readBinaryFile("assets/shaders/voxel.frag.spv"));

    VkPipelineShaderStageCreateInfo voxStages[2]{};
    voxStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    voxStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    voxStages[0].module = voxVert;
    voxStages[0].pName  = "main";
    voxStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    voxStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    voxStages[1].module = voxFrag;
    voxStages[1].pName  = "main";

    auto bindingDesc = VoxelVertex::getBindingDescription();
    auto attrDescs   = VoxelVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo voxVertexInput{};
    voxVertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    voxVertexInput.vertexBindingDescriptionCount   = 1;
    voxVertexInput.pVertexBindingDescriptions      = &bindingDesc;
    voxVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    voxVertexInput.pVertexAttributeDescriptions    = attrDescs.data();

    VkPipelineRasterizationStateCreateInfo voxRast{};
    voxRast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    voxRast.polygonMode = VK_POLYGON_MODE_FILL;
    voxRast.lineWidth   = 1.0f;
    voxRast.cullMode    = VK_CULL_MODE_BACK_BIT;
    voxRast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineDepthStencilStateCreateInfo voxDepth{};
    voxDepth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    voxDepth.depthTestEnable  = VK_TRUE;
    voxDepth.depthWriteEnable = VK_TRUE;
    voxDepth.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkGraphicsPipelineCreateInfo voxPipeInfo{};
    voxPipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    voxPipeInfo.stageCount          = 2;
    voxPipeInfo.pStages             = voxStages;
    voxPipeInfo.pVertexInputState   = &voxVertexInput;
    voxPipeInfo.pInputAssemblyState = &ia;
    voxPipeInfo.pViewportState      = &vpState;
    voxPipeInfo.pRasterizationState = &voxRast;
    voxPipeInfo.pMultisampleState   = &ms;
    voxPipeInfo.pDepthStencilState  = &voxDepth;
    voxPipeInfo.pColorBlendState    = &cb;
    voxPipeInfo.pDynamicState       = &dynInfo;
    voxPipeInfo.layout              = pipelineLayout_;
    voxPipeInfo.renderPass          = renderPass_;
    voxPipeInfo.subpass             = 0;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &voxPipeInfo, nullptr, &voxelPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create voxel graphics pipeline.");
    }

    // 1B. Translucent Voxel Pipeline (Water + Glass: alpha blended, depth test ON, depth write OFF)
    //     Reuses voxel.vert.spv & voxel.frag.spv -> ZERO extra shader binaries needed!
    VkPipelineDepthStencilStateCreateInfo transDepth = voxDepth;
    transDepth.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState transBlendAtt{};
    transBlendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    transBlendAtt.blendEnable         = VK_TRUE;
    transBlendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    transBlendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transBlendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    transBlendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    transBlendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transBlendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo transCb{};
    transCb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    transCb.attachmentCount = 1;
    transCb.pAttachments    = &transBlendAtt;

    VkGraphicsPipelineCreateInfo transPipeInfo = voxPipeInfo;
    transPipeInfo.pDepthStencilState = &transDepth;
    transPipeInfo.pColorBlendState   = &transCb;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &transPipeInfo, nullptr, &translucentPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create translucent voxel graphics pipeline.");
    }
    vkDestroyShaderModule(device_, voxVert, nullptr);
    vkDestroyShaderModule(device_, voxFrag, nullptr);

    // 2. Clear 360-Degree Sky Dome Pipeline
    VkShaderModule skyVert = createShaderModule(readBinaryFile("assets/shaders/sky.vert.spv"));
    VkShaderModule skyFrag = createShaderModule(readBinaryFile("assets/shaders/sky.frag.spv"));

    VkPipelineShaderStageCreateInfo skyStages[2]{};
    skyStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    skyStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    skyStages[0].module = skyVert;
    skyStages[0].pName  = "main";
    skyStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    skyStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    skyStages[1].module = skyFrag;
    skyStages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo emptyInput{};
    emptyInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineRasterizationStateCreateInfo skyRast{};
    skyRast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    skyRast.polygonMode = VK_POLYGON_MODE_FILL;
    skyRast.lineWidth   = 1.0f;
    skyRast.cullMode    = VK_CULL_MODE_NONE;
    skyRast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineDepthStencilStateCreateInfo skyDepth{};
    skyDepth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    skyDepth.depthTestEnable  = VK_TRUE;
    skyDepth.depthWriteEnable = VK_FALSE;
    skyDepth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo skyPipeInfo = voxPipeInfo;
    skyPipeInfo.pStages             = skyStages;
    skyPipeInfo.pVertexInputState   = &emptyInput;
    skyPipeInfo.pRasterizationState = &skyRast;
    skyPipeInfo.pDepthStencilState  = &skyDepth;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &skyPipeInfo, nullptr, &skyPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create sky graphics pipeline.");
    }
    vkDestroyShaderModule(device_, skyVert, nullptr);
    vkDestroyShaderModule(device_, skyFrag, nullptr);

    // 3. Alpha-Blended 2D HUD, Inventory, Pause & Settings UI Pipeline (with PNG Item Texture Array)
    VkPipelineLayoutCreateInfo hudLayoutInfo{};
    hudLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    hudLayoutInfo.setLayoutCount = 1;
    hudLayoutInfo.pSetLayouts    = &hudDescriptorSetLayout_;
    if (vkCreatePipelineLayout(device_, &hudLayoutInfo, nullptr, &hudPipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HUD pipeline layout.");
    }

    VkShaderModule hudVert = createShaderModule(readBinaryFile("assets/shaders/hud.vert.spv"));
    VkShaderModule hudFrag = createShaderModule(readBinaryFile("assets/shaders/hud.frag.spv"));

    VkPipelineShaderStageCreateInfo hudStages[2]{};
    hudStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    hudStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    hudStages[0].module = hudVert;
    hudStages[0].pName  = "main";
    hudStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    hudStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    hudStages[1].module = hudFrag;
    hudStages[1].pName  = "main";

    auto hudBinding = HudVertex::getBindingDescription();
    auto hudAttrs   = HudVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo hudVertexInput{};
    hudVertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    hudVertexInput.vertexBindingDescriptionCount   = 1;
    hudVertexInput.pVertexBindingDescriptions      = &hudBinding;
    hudVertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(hudAttrs.size());
    hudVertexInput.pVertexAttributeDescriptions    = hudAttrs.data();

    VkPipelineDepthStencilStateCreateInfo hudDepth{};
    hudDepth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    hudDepth.depthTestEnable  = VK_FALSE;
    hudDepth.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState hudBlendAtt{};
    hudBlendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    hudBlendAtt.blendEnable         = VK_TRUE;
    hudBlendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    hudBlendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    hudBlendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    hudBlendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    hudBlendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    hudBlendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo hudCb{};
    hudCb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    hudCb.attachmentCount = 1;
    hudCb.pAttachments    = &hudBlendAtt;

    VkGraphicsPipelineCreateInfo hudPipeInfo = skyPipeInfo;
    hudPipeInfo.pStages            = hudStages;
    hudPipeInfo.pVertexInputState  = &hudVertexInput;
    hudPipeInfo.pDepthStencilState = &hudDepth;
    hudPipeInfo.pColorBlendState   = &hudCb;
    hudPipeInfo.layout             = hudPipelineLayout_;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &hudPipeInfo, nullptr, &hudPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create HUD graphics pipeline.");
    }
    vkDestroyShaderModule(device_, hudVert, nullptr);
    vkDestroyShaderModule(device_, hudFrag, nullptr);
}

void VulkanEngine::createCommandPool() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(device_, &info, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool.");
    }
}

void VulkanEngine::createDepthResources() {
    VkFormat depthFmt = findDepthFormat();
    createImage(swapChainExtent_.width, swapChainExtent_.height, depthFmt,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage_, depthImageMemory_);
    depthImageView_ = createImageView(depthImage_, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanEngine::createDynamicLightResources() {
    // 32x32x32 R8_UNORM 3D GPU texture (32 KB VRAM) for zero-remesh occluded dynamic lighting
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_3D;
    imgInfo.extent        = {
        static_cast<uint32_t>(DYN_TEX_SIZE),
        static_cast<uint32_t>(DYN_TEX_SIZE),
        static_cast<uint32_t>(DYN_TEX_SIZE)
    };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.format        = VK_FORMAT_R8_UNORM;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imgInfo, nullptr, &dynLightImage_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 3D dynamic light image.");
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device_, dynLightImage_, &memReq);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &alloc, nullptr, &dynLightImageMemory_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate 3D dynamic light image memory.");
    }
    vkBindImageMemory(device_, dynLightImage_, dynLightImageMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = dynLightImage_;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format                          = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &dynLightImageView_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 3D dynamic light image view.");
    }

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter    = VK_FILTER_LINEAR;
    sampInfo.minFilter    = VK_FILTER_LINEAR;
    sampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.minLod       = 0.0f;
    sampInfo.maxLod       = 0.0f;

    if (vkCreateSampler(device_, &sampInfo, nullptr, &dynLightSampler_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 3D dynamic light sampler.");
    }

    // Per-frame persistently mapped 32 KB staging buffers (zero per-frame allocations!)
    VkDeviceSize stagingSize = static_cast<VkDeviceSize>(DYN_TEX_VOLUME);
    dynStagingBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    dynStagingMemory_.resize(MAX_FRAMES_IN_FLIGHT);
    dynStagingMapped_.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     dynStagingBuffers_[i], dynStagingMemory_[i]);
        vkMapMemory(device_, dynStagingMemory_[i], 0, stagingSize, 0, &dynStagingMapped_[i]);
        std::memset(dynStagingMapped_[i], 0, static_cast<size_t>(stagingSize));
    }
    dynUploadNeeded_     = true;
    dynImageInitialized_ = false;
}

void VulkanEngine::uploadTextureArray2D(const TextureArrayData& tex,
                                        VkSamplerAddressMode addressMode,
                                        VkImage& outImage,
                                        VkDeviceMemory& outMemory,
                                        VkImageView& outView,
                                        VkSampler& outSampler) {
    const VkDeviceSize totalBytes = static_cast<VkDeviceSize>(tex.pixels.size());
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    createBuffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuf, stagingMem);

    void* mapped = nullptr;
    vkMapMemory(device_, stagingMem, 0, totalBytes, 0, &mapped);
    std::memcpy(mapped, tex.pixels.data(), static_cast<size_t>(totalBytes));
    vkUnmapMemory(device_, stagingMem);

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.extent        = {tex.width, tex.height, 1};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = tex.layers;
    imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imgInfo, nullptr, &outImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 2D texture array image.");
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device_, outImage, &memReq);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_, &alloc, nullptr, &outMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate 2D texture array memory.");
    }
    vkBindImageMemory(device_, outImage, outMemory, 0);

    // One-time upload command buffer transitioning UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandPool        = commandPool_;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image                           = outImage;
    toTransfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel   = 0;
    toTransfer.subresourceRange.levelCount     = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount     = tex.layers;
    toTransfer.srcAccessMask                   = 0;
    toTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = tex.layers;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {tex.width, tex.height, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuf, outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader{};
    toShader.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShader.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toShader.image                           = outImage;
    toShader.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    toShader.subresourceRange.baseMipLevel   = 0;
    toShader.subresourceRange.levelCount     = 1;
    toShader.subresourceRange.baseArrayLayer = 0;
    toShader.subresourceRange.layerCount     = tex.layers;
    toShader.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toShader);

    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence uploadFence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fenceInfo, nullptr, &uploadFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture upload fence.");
    }

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    if (vkQueueSubmit(graphicsQueue_, 1, &submit, uploadFence) != VK_SUCCESS) {
        vkDestroyFence(device_, uploadFence, nullptr);
        throw std::runtime_error("Failed to submit 2D texture array upload.");
    }
    vkWaitForFences(device_, 1, &uploadFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, uploadFence, nullptr);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    vkDestroyBuffer(device_, stagingBuf, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = outImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = tex.layers;

    if (vkCreateImageView(device_, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 2D texture array image view.");
    }

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter    = VK_FILTER_NEAREST;
    sampInfo.minFilter    = VK_FILTER_NEAREST;
    sampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU = addressMode;
    sampInfo.addressModeV = addressMode;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.minLod       = 0.0f;
    sampInfo.maxLod       = 0.0f;

    if (vkCreateSampler(device_, &sampInfo, nullptr, &outSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create 2D texture array sampler.");
    }
}

void VulkanEngine::createTextureResources() {
    TextureArrayData blockTex = loadBlockTextureArray();
    uploadTextureArray2D(blockTex, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                         blockTexImage_, blockTexMemory_, blockTexView_, blockTexSampler_);

    TextureArrayData itemTex = loadItemTextureArray();
    uploadTextureArray2D(itemTex, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         itemTexImage_, itemTexMemory_, itemTexView_, itemTexSampler_);
}

void VulkanEngine::createFramebuffers() {
    swapChainFramebuffers_.resize(swapChainImageViews_.size());
    for (size_t i = 0; i < swapChainImageViews_.size(); ++i) {
        std::array<VkImageView, 2> atts = {swapChainImageViews_[i], depthImageView_};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = renderPass_;
        fbInfo.attachmentCount = static_cast<uint32_t>(atts.size());
        fbInfo.pAttachments    = atts.data();
        fbInfo.width           = swapChainExtent_.width;
        fbInfo.height          = swapChainExtent_.height;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &swapChainFramebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer.");
        }
    }
}

void VulkanEngine::createUniformBuffers() {
    VkDeviceSize size = sizeof(FrameUBO);
    uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory_.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     uniformBuffers_[i], uniformBuffersMemory_[i]);
        vkMapMemory(device_, uniformBuffersMemory_[i], 0, size, 0, &uniformBuffersMapped_[i]);
    }
}

void VulkanEngine::createHudBuffers() {
    VkDeviceSize size = sizeof(HudVertex) * HudBuilder::MAX_HUD_VERTICES;
    hudVertexBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    hudVertexBuffersMemory_.resize(MAX_FRAMES_IN_FLIGHT);
    hudVertexBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     hudVertexBuffers_[i], hudVertexBuffersMemory_[i]);
        vkMapMemory(device_, hudVertexBuffersMemory_[i], 0, size, 0, &hudVertexBuffersMapped_[i]);
    }
}

void VulkanEngine::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // Per frame: 1x 3D dynLight + 1x 2D-array blockTextures; plus 1x HUD itemTextures set
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2 + 1;

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    info.pPoolSizes    = poolSizes.data();
    info.maxSets       = MAX_FRAMES_IN_FLIGHT + 1;

    if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool.");
    }
}

void VulkanEngine::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout_);
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool     = descriptorPool_;
    alloc.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    alloc.pSetLayouts        = layouts.data();

    descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device_, &alloc, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets.");
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = uniformBuffers_[i];
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(FrameUBO);

        VkDescriptorImageInfo dynImgInfo{};
        dynImgInfo.sampler     = dynLightSampler_;
        dynImgInfo.imageView   = dynLightImageView_;
        dynImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo blockImgInfo{};
        blockImgInfo.sampler     = blockTexSampler_;
        blockImgInfo.imageView   = blockTexView_;
        blockImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = descriptorSets_[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &bufInfo;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = descriptorSets_[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &dynImgInfo;

        writes[2].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet          = descriptorSets_[i];
        writes[2].dstBinding      = 2;
        writes[2].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo      = &blockImgInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate & bind HUD Item Texture Array descriptor set
    VkDescriptorSetAllocateInfo hudAlloc{};
    hudAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    hudAlloc.descriptorPool     = descriptorPool_;
    hudAlloc.descriptorSetCount = 1;
    hudAlloc.pSetLayouts        = &hudDescriptorSetLayout_;

    if (vkAllocateDescriptorSets(device_, &hudAlloc, &hudDescriptorSet_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate HUD descriptor set.");
    }

    VkDescriptorImageInfo itemImgInfo{};
    itemImgInfo.sampler     = itemTexSampler_;
    itemImgInfo.imageView   = itemTexView_;
    itemImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet hudWrite{};
    hudWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    hudWrite.dstSet          = hudDescriptorSet_;
    hudWrite.dstBinding      = 0;
    hudWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hudWrite.descriptorCount = 1;
    hudWrite.pImageInfo      = &itemImgInfo;

    vkUpdateDescriptorSets(device_, 1, &hudWrite, 0, nullptr);
}

void VulkanEngine::createCommandBuffers() {
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo info{};
    info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool        = commandPool_;
    info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(device_, &info, commandBuffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers.");
    }
}

void VulkanEngine::createSyncObjects() {
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects.");
        }
    }

    VkFenceCreateInfo transferFenceInfo{};
    transferFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device_, &transferFenceInfo, nullptr, &transferFence_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create transfer fence.");
    }
}

void VulkanEngine::destroyChunkGpuMesh(ChunkGpuMesh& gpu) {
    if (gpu.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.vertexBuffer, nullptr);
        vkFreeMemory(device_, gpu.vertexMemory, nullptr);
        gpu.vertexBuffer = VK_NULL_HANDLE;
        gpu.vertexMemory = VK_NULL_HANDLE;
    }
    if (gpu.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.indexBuffer, nullptr);
        vkFreeMemory(device_, gpu.indexMemory, nullptr);
        gpu.indexBuffer = VK_NULL_HANDLE;
        gpu.indexMemory = VK_NULL_HANDLE;
    }
    if (gpu.transVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.transVertexBuffer, nullptr);
        vkFreeMemory(device_, gpu.transVertexMemory, nullptr);
        gpu.transVertexBuffer = VK_NULL_HANDLE;
        gpu.transVertexMemory = VK_NULL_HANDLE;
    }
    if (gpu.transIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, gpu.transIndexBuffer, nullptr);
        vkFreeMemory(device_, gpu.transIndexMemory, nullptr);
        gpu.transIndexBuffer = VK_NULL_HANDLE;
        gpu.transIndexMemory = VK_NULL_HANDLE;
    }
    gpu.indexCount      = 0;
    gpu.transIndexCount = 0;
}

void VulkanEngine::destroyAllChunkMeshes() {
    for (auto& [coord, gpu] : chunkMeshes_) {
        destroyChunkGpuMesh(gpu);
    }
    chunkMeshes_.clear();
}

void VulkanEngine::flushDeferredDestroys(bool forceAll) {
    if (deferredDestroys_.empty()) return;
    size_t writeIdx = 0;
    for (size_t i = 0; i < deferredDestroys_.size(); ++i) {
        auto& item = deferredDestroys_[i];
        if (forceAll || item.framesRemaining <= 1) {
            destroyChunkGpuMesh(item.gpu);
        } else {
            --item.framesRemaining;
            if (writeIdx != i) deferredDestroys_[writeIdx] = item;
            ++writeIdx;
        }
    }
    deferredDestroys_.resize(writeIdx);
}

void VulkanEngine::updateDynamicLightVolume(const uint8_t* volume32x32x32) {
    if (!volume32x32x32) return;
    std::memcpy(dynCpuCopy_.data(), volume32x32x32, static_cast<size_t>(DYN_TEX_VOLUME));
    dynUploadNeeded_ = true;
}

void VulkanEngine::recordDynamicLightUpload(VkCommandBuffer cmd) {
    if (!dynUploadNeeded_ && dynImageInitialized_) return;

    std::memcpy(dynStagingMapped_[currentFrame_], dynCpuCopy_.data(), static_cast<size_t>(DYN_TEX_VOLUME));

    // Barrier 1: UNDEFINED or SHADER_READ_ONLY_OPTIMAL -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout                       = dynImageInitialized_
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image                           = dynLightImage_;
    toTransfer.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel   = 0;
    toTransfer.subresourceRange.levelCount     = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount     = 1;
    toTransfer.srcAccessMask                   = dynImageInitialized_ ? VK_ACCESS_SHADER_READ_BIT : 0;
    toTransfer.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkPipelineStageFlags srcStage = dynImageInitialized_
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copy{};
    copy.bufferOffset                    = 0;
    copy.bufferRowLength                 = 0;
    copy.bufferImageHeight               = 0;
    copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel       = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount     = 1;
    copy.imageOffset                     = {0, 0, 0};
    copy.imageExtent                     = {
        static_cast<uint32_t>(DYN_TEX_SIZE),
        static_cast<uint32_t>(DYN_TEX_SIZE),
        static_cast<uint32_t>(DYN_TEX_SIZE)
    };
    vkCmdCopyBufferToImage(cmd, dynStagingBuffers_[currentFrame_], dynLightImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    // Barrier 2: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier toShader{};
    toShader.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShader.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    toShader.image                           = dynLightImage_;
    toShader.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    toShader.subresourceRange.baseMipLevel   = 0;
    toShader.subresourceRange.levelCount     = 1;
    toShader.subresourceRange.baseArrayLayer = 0;
    toShader.subresourceRange.layerCount     = 1;
    toShader.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toShader);

    dynUploadNeeded_     = false;
    dynImageInitialized_ = true;
}

void VulkanEngine::syncChunkMeshes(const std::vector<ChunkCoord>& toUnload,
                                   const std::vector<std::pair<ChunkCoord, MeshData>>& toUpload) {
    if (toUnload.empty() && toUpload.empty()) return;

    // 1. Defer destruction of unloaded chunks (ZERO vkDeviceWaitIdle!)
    for (ChunkCoord coord : toUnload) {
        auto it = chunkMeshes_.find(coord);
        if (it != chunkMeshes_.end()) {
            deferredDestroys_.push_back({it->second, MAX_FRAMES_IN_FLIGHT + 1});
            chunkMeshes_.erase(it);
        }
    }

    if (toUpload.empty()) return;

    // 2. Compute total staging bytes for ONE single coalesced transfer command buffer (opaque + translucent)!
    VkDeviceSize totalStagingBytes = 0;
    struct PendingCopy {
        ChunkCoord   coord{};
        ChunkGpuMesh gpu{};
        VkDeviceSize vOffset  = 0;
        VkDeviceSize vSize    = 0;
        VkDeviceSize iOffset  = 0;
        VkDeviceSize iSize    = 0;
        VkDeviceSize tvOffset = 0;
        VkDeviceSize tvSize   = 0;
        VkDeviceSize tiOffset = 0;
        VkDeviceSize tiSize   = 0;
    };
    std::vector<PendingCopy> pending;
    pending.reserve(toUpload.size());

    auto alignUp = [](VkDeviceSize v, VkDeviceSize a) noexcept -> VkDeviceSize {
        return (v + (a - 1)) & ~(a - 1);
    };

    for (const auto& [coord, mesh] : toUpload) {
        // Retire old GPU buffers safely after in-flight frames finish
        auto it = chunkMeshes_.find(coord);
        if (it != chunkMeshes_.end()) {
            deferredDestroys_.push_back({it->second, MAX_FRAMES_IN_FLIGHT + 1});
            chunkMeshes_.erase(it);
        }
        bool hasOpaque = !mesh.vertices.empty() && !mesh.indices.empty();
        bool hasTrans  = !mesh.transVertices.empty() && !mesh.transIndices.empty();
        if (!hasOpaque && !hasTrans) continue;

        PendingCopy pc{};
        pc.coord = coord;
        if (hasOpaque) {
            pc.vSize   = sizeof(VoxelVertex) * mesh.vertices.size();
            pc.iSize   = sizeof(uint32_t) * mesh.indices.size();
            pc.vOffset = alignUp(totalStagingBytes, 16);
            totalStagingBytes = pc.vOffset + pc.vSize;
            pc.iOffset = alignUp(totalStagingBytes, 16);
            totalStagingBytes = pc.iOffset + pc.iSize;
            pc.gpu.indexCount = static_cast<uint32_t>(mesh.indices.size());
        }
        if (hasTrans) {
            pc.tvSize   = sizeof(VoxelVertex) * mesh.transVertices.size();
            pc.tiSize   = sizeof(uint32_t) * mesh.transIndices.size();
            pc.tvOffset = alignUp(totalStagingBytes, 16);
            totalStagingBytes = pc.tvOffset + pc.tvSize;
            pc.tiOffset = alignUp(totalStagingBytes, 16);
            totalStagingBytes = pc.tiOffset + pc.tiSize;
            pc.gpu.transIndexCount = static_cast<uint32_t>(mesh.transIndices.size());
        }
        pending.push_back(pc);
    }

    if (pending.empty()) return;

    // Single coalesced staging buffer for the entire chunk upload batch
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    createBuffer(totalStagingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuf, stagingMem);

    void* mapped = nullptr;
    vkMapMemory(device_, stagingMem, 0, totalStagingBytes, 0, &mapped);
    uint8_t* dstBase = static_cast<uint8_t*>(mapped);

    size_t pIdx = 0;
    for (const auto& [coord, mesh] : toUpload) {
        bool hasOpaque = !mesh.vertices.empty() && !mesh.indices.empty();
        bool hasTrans  = !mesh.transVertices.empty() && !mesh.transIndices.empty();
        if (!hasOpaque && !hasTrans) continue;

        PendingCopy& pc = pending[pIdx++];
        if (hasOpaque) {
            std::memcpy(dstBase + pc.vOffset, mesh.vertices.data(), static_cast<size_t>(pc.vSize));
            std::memcpy(dstBase + pc.iOffset, mesh.indices.data(), static_cast<size_t>(pc.iSize));
            createBuffer(pc.vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, pc.gpu.vertexBuffer, pc.gpu.vertexMemory);
            createBuffer(pc.iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, pc.gpu.indexBuffer, pc.gpu.indexMemory);
        }
        if (hasTrans) {
            std::memcpy(dstBase + pc.tvOffset, mesh.transVertices.data(), static_cast<size_t>(pc.tvSize));
            std::memcpy(dstBase + pc.tiOffset, mesh.transIndices.data(), static_cast<size_t>(pc.tiSize));
            createBuffer(pc.tvSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, pc.gpu.transVertexBuffer, pc.gpu.transVertexMemory);
            createBuffer(pc.tiSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, pc.gpu.transIndexBuffer, pc.gpu.transIndexMemory);
        }
    }
    vkUnmapMemory(device_, stagingMem);

    // Record ALL chunk copies into ONE single command buffer submit
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandPool        = commandPool_;
    alloc.commandBufferCount = 1;

    VkCommandBuffer cmd{};
    vkAllocateCommandBuffers(device_, &alloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    for (const auto& pc : pending) {
        if (pc.vSize > 0) {
            VkBufferCopy vCopy{pc.vOffset, 0, pc.vSize};
            vkCmdCopyBuffer(cmd, stagingBuf, pc.gpu.vertexBuffer, 1, &vCopy);
            VkBufferCopy iCopy{pc.iOffset, 0, pc.iSize};
            vkCmdCopyBuffer(cmd, stagingBuf, pc.gpu.indexBuffer, 1, &iCopy);
        }
        if (pc.tvSize > 0) {
            VkBufferCopy tvCopy{pc.tvOffset, 0, pc.tvSize};
            vkCmdCopyBuffer(cmd, stagingBuf, pc.gpu.transVertexBuffer, 1, &tvCopy);
            VkBufferCopy tiCopy{pc.tiOffset, 0, pc.tiSize};
            vkCmdCopyBuffer(cmd, stagingBuf, pc.gpu.transIndexBuffer, 1, &tiCopy);
        }
    }
    vkEndCommandBuffer(cmd);

    vkResetFences(device_, 1, &transferFence_);
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;

    if (vkQueueSubmit(graphicsQueue_, 1, &submit, transferFence_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit batched chunk upload.");
    }
    vkWaitForFences(device_, 1, &transferFence_, VK_TRUE, UINT64_MAX);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    vkDestroyBuffer(device_, stagingBuf, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);

    for (const auto& pc : pending) {
        chunkMeshes_.emplace(pc.coord, pc.gpu);
    }
}

void VulkanEngine::uploadChunkMesh(ChunkCoord coord, const MeshData& mesh) {
    std::vector<std::pair<ChunkCoord, MeshData>> one;
    one.emplace_back(coord, mesh);
    syncChunkMeshes({}, one);
}

void VulkanEngine::unloadChunkMesh(ChunkCoord coord) {
    syncChunkMeshes({coord}, {});
}

void VulkanEngine::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex,
                                       const Frustum& frustum, const vec3& cameraPos,
                                       uint32_t hudVertexCount) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer.");
    }

    // Async 32 KB 3D dynamic light upload BEFORE render pass (zero stall!)
    recordDynamicLightUpload(cmd);

    std::array<VkClearValue, 2> clears{};
    clears[0].color        = {{0.002f, 0.003f, 0.006f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = renderPass_;
    rpBegin.framebuffer       = swapChainFramebuffers_[imageIndex];
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = swapChainExtent_;
    rpBegin.clearValueCount   = static_cast<uint32_t>(clears.size());
    rpBegin.pClearValues      = clears.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x        = 0.0f;
    vp.y        = 0.0f;
    vp.width    = static_cast<float>(swapChainExtent_.width);
    vp.height   = static_cast<float>(swapChainExtent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{{0, 0}, swapChainExtent_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &descriptorSets_[currentFrame_], 0, nullptr);

    // 1. Collect frustum-visible chunks once & draw OPAQUE voxel pass
    struct TransDrawItem {
        const ChunkGpuMesh* gpu;
        float               distSq;
    };
    thread_local std::vector<TransDrawItem> visibleTrans;
    visibleTrans.clear();
    size_t visibleCount = 0;
    uint32_t opaqueTris = 0;
    uint32_t transTris  = 0;

    if (!chunkMeshes_.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, voxelPipeline_);
        const VkDeviceSize offsets[] = {0};
        for (const auto& [coord, gpu] : chunkMeshes_) {
            bool hasOpaque = (gpu.indexCount > 0 && gpu.vertexBuffer != VK_NULL_HANDLE);
            bool hasTrans  = (gpu.transIndexCount > 0 && gpu.transVertexBuffer != VK_NULL_HANDLE);
            if (!hasOpaque && !hasTrans) continue;

            float minX = static_cast<float>(coord.cx * CHUNK_SIZE);
            float minZ = static_cast<float>(coord.cz * CHUNK_SIZE);
            if (!frustum.intersectsAABB(minX, 0.0f, minZ,
                                        minX + static_cast<float>(CHUNK_SIZE),
                                        static_cast<float>(CHUNK_HEIGHT),
                                        minZ + static_cast<float>(CHUNK_SIZE))) {
                continue;
            }
            ++visibleCount;
            if (hasOpaque) {
                VkBuffer vbufs[] = {gpu.vertexBuffer};
                vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
                vkCmdBindIndexBuffer(cmd, gpu.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, gpu.indexCount, 1, 0, 0, 0);
                opaqueTris += gpu.indexCount / 3;
            }
            if (hasTrans) {
                float dx = (minX + 8.0f) - cameraPos.x;
                float dz = (minZ + 8.0f) - cameraPos.z;
                visibleTrans.push_back({&gpu, dx * dx + dz * dz});
            }
        }
    }
    lastVisibleChunks_ = visibleCount;
    lastOpaqueTris_    = opaqueTris;

    // 2. Draw Clear 360-Degree Sky Sphere wherever depth == 1.0
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // 3. Draw Translucent Voxel Pass sorted back-to-front (Water + Glass -> clean alpha compositing!)
    if (!visibleTrans.empty()) {
        std::sort(visibleTrans.begin(), visibleTrans.end(),
                  [](const TransDrawItem& a, const TransDrawItem& b) {
                      return a.distSq > b.distSq;
                  });
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, translucentPipeline_);
        const VkDeviceSize offsets[] = {0};
        for (const TransDrawItem& item : visibleTrans) {
            const ChunkGpuMesh* gpu = item.gpu;
            VkBuffer vbufs[] = {gpu->transVertexBuffer};
            vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
            vkCmdBindIndexBuffer(cmd, gpu->transIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, gpu->transIndexCount, 1, 0, 0, 0);
            transTris += gpu->transIndexCount / 3;
        }
    }
    lastTransTris_ = transTris;

    // 4. Draw Alpha-Blended 2D HUD, Inventory, Shift+F3 Debugger, Pause & Settings UI (with PNG Item Textures)
    if (hudVertexCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, hudPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, hudPipelineLayout_,
                                0, 1, &hudDescriptorSet_, 0, nullptr);
        VkBuffer hudBufs[] = {hudVertexBuffers_[currentFrame_]};
        const VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, hudBufs, offsets);
        vkCmdDraw(cmd, hudVertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to end command buffer recording.");
    }
}

void VulkanEngine::drawFrame(const FrameUBO& ubo, const std::vector<HudVertex>& hudVerts) {
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
    flushDeferredDestroys(false);

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(
        device_, swapChain_, UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    } else if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image.");
    }

    std::memcpy(uniformBuffersMapped_[currentFrame_], &ubo, sizeof(FrameUBO));

    uint32_t hudCount = static_cast<uint32_t>(
        std::min(hudVerts.size(), HudBuilder::MAX_HUD_VERTICES));
    if (hudCount > 0) {
        std::memcpy(hudVertexBuffersMapped_[currentFrame_],
                    hudVerts.data(),
                    sizeof(HudVertex) * hudCount);
    }

    Frustum frustum = extractFrustumVulkan(ubo.viewProj);

    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, frustum,
                        vec3(ubo.cameraPosTime.x, ubo.cameraPosTime.y, ubo.cameraPosTime.z),
                        hudCount);

    VkSemaphore waitSems[]            = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSems[]          = {renderFinishedSemaphores_[currentFrame_]};

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = waitSems;
    submit.pWaitDstStageMask    = waitStages;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &commandBuffers_[currentFrame_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = signalSems;

    if (vkQueueSubmit(graphicsQueue_, 1, &submit, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer.");
    }

    VkSwapchainKHR swaps[] = {swapChain_};
    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = signalSems;
    present.swapchainCount     = 1;
    present.pSwapchains        = swaps;
    present.pImageIndices      = &imageIndex;

    VkResult res = vkQueuePresentKHR(presentQueue_, &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapChain();
    } else if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image.");
    }

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanEngine::cleanupSwapChain() {
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, depthImage_, nullptr);
        vkFreeMemory(device_, depthImageMemory_, nullptr);
        depthImage_       = VK_NULL_HANDLE;
        depthImageMemory_ = VK_NULL_HANDLE;
    }
    for (VkFramebuffer fb : swapChainFramebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    swapChainFramebuffers_.clear();

    for (VkImageView iv : swapChainImageViews_) {
        vkDestroyImageView(device_, iv, nullptr);
    }
    swapChainImageViews_.clear();

    if (swapChain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapChain_, nullptr);
        swapChain_ = VK_NULL_HANDLE;
    }
}

void VulkanEngine::recreateSwapChain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window_, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(device_);
    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createFramebuffers();
}

uint32_t VulkanEngine::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type.");
}

VkFormat VulkanEngine::findDepthFormat() const {
    const std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };
    for (VkFormat fmt : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return fmt;
        }
    }
    throw std::runtime_error("Failed to find supported depth buffer format.");
}

void VulkanEngine::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties,
                                VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo info{};
    info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size        = size;
    info.usage       = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &info, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer.");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device_, buffer, &memReq);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &alloc, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan buffer memory.");
    }
    vkBindBufferMemory(device_, buffer, bufferMemory, 0);
}

void VulkanEngine::createImage(uint32_t width, uint32_t height, VkFormat format,
                               VkImageTiling tiling, VkImageUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.extent        = {width, height, 1};
    info.mipLevels     = 1;
    info.arrayLayers   = 1;
    info.format        = format;
    info.tiling        = tiling;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.usage         = usage;
    info.samples       = VK_SAMPLE_COUNT_1_BIT;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &info, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image.");
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device_, image, &memReq);

    VkMemoryAllocateInfo alloc{};
    alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize  = memReq.size;
    alloc.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &alloc, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan image memory.");
    }
    vkBindImageMemory(device_, image, imageMemory, 0);
}

VkImageView VulkanEngine::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo info{};
    info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image                           = image;
    info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    info.format                          = format;
    info.subresourceRange.aspectMask     = aspectFlags;
    info.subresourceRange.baseMipLevel   = 0;
    info.subresourceRange.levelCount     = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount     = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &info, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view.");
    }
    return view;
}

VkShaderModule VulkanEngine::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &info, nullptr, &mod) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module.");
    }
    return mod;
}

std::vector<char> VulkanEngine::readBinaryFile(const std::string& path) {
    // 1. Gather candidate search paths
    std::vector<std::string> candidates;
    candidates.push_back(path);

    // If path starts with "build/", also try without "build/" (e.g. running from build directory)
    if (path.rfind("build/", 0) == 0) {
        candidates.push_back(path.substr(6));
    }

    size_t slashPos = path.find_last_of("/\\");
    std::string filename = (slashPos != std::string::npos) ? path.substr(slashPos + 1) : path;
    candidates.push_back("assets/shaders/" + filename);
    candidates.push_back("build/assets/shaders/" + filename);
    candidates.push_back("shaders/" + filename);
    candidates.push_back("build/shaders/" + filename);

    // Locate directory containing the executable
    std::string exeDir;
#ifdef _WIN32
    char exeBuf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string p(exeBuf, len);
        size_t lastSlash = p.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = p.substr(0, lastSlash);
        }
    }
#elif defined(__linux__)
    char exeBuf[1024];
    ssize_t len = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (len > 0) {
        exeBuf[len] = '\0';
        std::string p(exeBuf);
        size_t lastSlash = p.find_last_of('/');
        if (lastSlash != std::string::npos) {
            exeDir = p.substr(0, lastSlash);
        }
    }
#endif

    if (!exeDir.empty()) {
        candidates.push_back(exeDir + "/" + path);
        if (path.rfind("build/", 0) == 0) {
            candidates.push_back(exeDir + "/" + path.substr(6));
        }
        candidates.push_back(exeDir + "/assets/shaders/" + filename);
        candidates.push_back(exeDir + "/../assets/shaders/" + filename);
        candidates.push_back(exeDir + "/" + filename);
        candidates.push_back(exeDir + "/shaders/" + filename);
        candidates.push_back(exeDir + "/build/assets/shaders/" + filename);
        candidates.push_back(exeDir + "/../" + path);
        candidates.push_back(exeDir + "/../shaders/" + filename);
    }

    // Attempt to open and read from candidates on disk
    for (const auto& candidate : candidates) {
        std::ifstream file(candidate, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t fileSize = static_cast<size_t>(file.tellg());
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
            return buffer;
        }
    }

    // 2. Fallback to embedded SPIR-V bytecode if file is missing on disk
    std::vector<char> embeddedCode;
    if (embedded::getEmbeddedShader(filename, embeddedCode)) {
        return embeddedCode;
    }

    throw std::runtime_error("Failed to open SPIR-V shader file: " + path +
                             " (run from project root or ensure assets/shaders/ directory exists)");
}

} // namespace umbra
