#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "hud.hpp"
#include "mesher.hpp"
#include "texture_loader.hpp"
#include "types.hpp"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace umbra {

class VulkanEngine {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    void init(GLFWwindow* window);
    void cleanup();

    // Non-blocking batched chunk GPU upload & deferred eviction (ZERO vkDeviceWaitIdle!)
    void syncChunkMeshes(const std::vector<ChunkCoord>& toUnload,
                         const std::vector<std::pair<ChunkCoord, MeshData>>& toUpload);
    void uploadChunkMesh(ChunkCoord coord, const MeshData& mesh);
    void unloadChunkMesh(ChunkCoord coord);
    size_t loadedChunkMeshCount() const noexcept { return chunkMeshes_.size(); }
    size_t lastVisibleChunkCount() const noexcept { return lastVisibleChunks_; }
    uint32_t lastOpaqueTriCount() const noexcept { return lastOpaqueTris_; }
    uint32_t lastTransTriCount() const noexcept { return lastTransTris_; }

    // Queues a zero-stall 32 KB GPU 3D texture upload for occluded held-block dynamic lighting
    void updateDynamicLightVolume(const uint8_t* volume32x32x32);

    // Renders frustum-culled voxel chunks + clear 360 sky dome + alpha-blended 2D HUD/Menus
    void drawFrame(const FrameUBO& ubo, const std::vector<HudVertex>& hudVerts);

    void markFramebufferResized() { framebufferResized_ = true; }

    VkExtent2D extent() const noexcept { return swapChainExtent_; }

    float aspectRatio() const {
        if (swapChainExtent_.height == 0) return 16.0f / 9.0f;
        return static_cast<float>(swapChainExtent_.width) /
               static_cast<float>(swapChainExtent_.height);
    }

private:
    struct ChunkGpuMesh {
        VkBuffer       vertexBuffer      = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory      = VK_NULL_HANDLE;
        VkBuffer       indexBuffer       = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory       = VK_NULL_HANDLE;
        uint32_t       indexCount        = 0;
        VkBuffer       transVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory transVertexMemory = VK_NULL_HANDLE;
        VkBuffer       transIndexBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory transIndexMemory  = VK_NULL_HANDLE;
        uint32_t       transIndexCount   = 0;
    };

    struct DeferredDestroy {
        ChunkGpuMesh gpu{};
        uint32_t     framesRemaining = MAX_FRAMES_IN_FLIGHT + 1;
    };

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR        capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR>   presentModes;
    };

    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipelines();
    void createCommandPool();
    void createDepthResources();
    void createDynamicLightResources();
    void createTextureResources();
    void uploadTextureArray2D(const TextureArrayData& tex,
                              VkSamplerAddressMode addressMode,
                              VkImage& outImage,
                              VkDeviceMemory& outMemory,
                              VkImageView& outView,
                              VkSampler& outSampler);
    void createFramebuffers();
    void createUniformBuffers();
    void createHudBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();

    void recreateSwapChain();
    void cleanupSwapChain();
    void destroyChunkGpuMesh(ChunkGpuMesh& gpu);
    void destroyAllChunkMeshes();
    void flushDeferredDestroys(bool forceAll);

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice dev) const;
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice dev) const;
    bool isDeviceSuitable(VkPhysicalDevice dev) const;
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkFormat findDepthFormat() const;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageTiling tiling, VkImageUsageFlags usage,
                     VkMemoryPropertyFlags properties,
                     VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    VkShaderModule createShaderModule(const std::vector<char>& code);
    static std::vector<char> readBinaryFile(const std::string& path);

    void recordDynamicLightUpload(VkCommandBuffer cmd);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex,
                             const Frustum& frustum, const vec3& cameraPos,
                             uint32_t hudVertexCount);

    GLFWwindow*      window_         = nullptr;
    VkInstance       instance_       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          graphicsQueue_  = VK_NULL_HANDLE;
    VkQueue          presentQueue_   = VK_NULL_HANDLE;

    VkSwapchainKHR             swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage>       swapChainImages_;
    VkFormat                   swapChainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D                 swapChainExtent_{};
    std::vector<VkImageView>   swapChainImageViews_;
    std::vector<VkFramebuffer> swapChainFramebuffers_;

    VkImage        depthImage_       = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView    depthImageView_   = VK_NULL_HANDLE;

    // 32x32x32 R8_UNORM 3D GPU texture + per-frame staging for zero-stall dynamic light updates
    VkImage        dynLightImage_       = VK_NULL_HANDLE;
    VkDeviceMemory dynLightImageMemory_ = VK_NULL_HANDLE;
    VkImageView    dynLightImageView_   = VK_NULL_HANDLE;
    VkSampler      dynLightSampler_     = VK_NULL_HANDLE;
    std::vector<VkBuffer>       dynStagingBuffers_;
    std::vector<VkDeviceMemory> dynStagingMemory_;
    std::vector<void*>          dynStagingMapped_;
    std::array<uint8_t, DYN_TEX_VOLUME> dynCpuCopy_{};
    bool dynUploadNeeded_     = true;
    bool dynImageInitialized_ = false;

    // Real PNG Texture Arrays:
    //  - blockTex* (18 layers from assets/texture/block/*.png)
    //  - itemTex*  (14 layers from assets/texture/items/*.png)
    VkImage        blockTexImage_   = VK_NULL_HANDLE;
    VkDeviceMemory blockTexMemory_  = VK_NULL_HANDLE;
    VkImageView    blockTexView_    = VK_NULL_HANDLE;
    VkSampler      blockTexSampler_ = VK_NULL_HANDLE;

    VkImage        itemTexImage_    = VK_NULL_HANDLE;
    VkDeviceMemory itemTexMemory_   = VK_NULL_HANDLE;
    VkImageView    itemTexView_     = VK_NULL_HANDLE;
    VkSampler      itemTexSampler_  = VK_NULL_HANDLE;

    VkRenderPass          renderPass_             = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_    = VK_NULL_HANDLE;
    VkDescriptorSetLayout hudDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout_         = VK_NULL_HANDLE;
    VkPipelineLayout      hudPipelineLayout_      = VK_NULL_HANDLE;
    VkPipeline            voxelPipeline_          = VK_NULL_HANDLE;
    VkPipeline            translucentPipeline_    = VK_NULL_HANDLE;
    VkPipeline            skyPipeline_            = VK_NULL_HANDLE;
    VkPipeline            hudPipeline_            = VK_NULL_HANDLE;

    VkCommandPool                commandPool_   = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkFence                      transferFence_ = VK_NULL_HANDLE;

    std::vector<VkBuffer>       uniformBuffers_;
    std::vector<VkDeviceMemory> uniformBuffersMemory_;
    std::vector<void*>          uniformBuffersMapped_;

    std::vector<VkBuffer>       hudVertexBuffers_;
    std::vector<VkDeviceMemory> hudVertexBuffersMemory_;
    std::vector<void*>          hudVertexBuffersMapped_;

    VkDescriptorPool             descriptorPool_  = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    VkDescriptorSet              hudDescriptorSet_ = VK_NULL_HANDLE;

    std::unordered_map<ChunkCoord, ChunkGpuMesh, ChunkCoordHash> chunkMeshes_;
    std::vector<DeferredDestroy> deferredDestroys_;
    size_t   lastVisibleChunks_ = 0;
    uint32_t lastOpaqueTris_    = 0;
    uint32_t lastTransTris_     = 0;

    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence>     inFlightFences_;
    uint32_t                 currentFrame_       = 0;
    bool                     framebufferResized_ = false;
};

} // namespace umbra
