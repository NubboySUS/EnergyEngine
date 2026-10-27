//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Declare renderer and asset registry //
//                                              //
//==============================================//

#pragma once
#include <vulkan/vulkan.h>
#include "../ImGui/imgui_impl_vulkan.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cctype>
#include <sstream>
#include "../World/Level.h"

namespace Engine {
    class Window;
    class Camera;

    class Vulkan {
    public:
        struct VulkanInitInfo {
            bool IsSuccess = false;
            std::string ErrorDescription = "";
            int ErrorCode = 0;
        };

        struct MeshVertex {
            float position[3]{};
            float uv[2]{};          // UV0 -> MODEL DEFAULT UVS
            float lightmapUV[2]{};  // UV1 -> LIGHTMAP UVS
        };
        struct SubMeshData {
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            std::string materialName;
            std::array<float, 4> defaultMaterialColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            std::string defaultMaterialTexture;
            bool IsValid() const noexcept {
                if (vertices.empty() || indices.empty() || (indices.size() % 3u) != 0u) return false;
                for (const std::uint32_t index : indices) if (index >= vertices.size()) return false;
                return true;
            }
        };
        struct MeshData {
            std::vector<SubMeshData> subMeshes;
            bool hasLightmapUVs = false;
            bool IsValid() const noexcept {
                if (subMeshes.empty()) return false;
                for (const SubMeshData& subMesh : subMeshes) if (!subMesh.IsValid()) return false;
                return true;
            }
        };

        using MaterialHandle = std::uint32_t;
        using TextureHandle = std::uint32_t;
        using MeshHandle = std::uint32_t;
        static constexpr std::uint32_t MaxMaterialTextures = 8;
        static constexpr std::uint32_t MaxMaterialParameters = 32;

        struct MaterialDesc {
            std::string name;
            std::string fragmentShaderSpv = "Engine/Shaders/default.frag.spv";
            std::array<float, 4> matUV{ 1.0f, 1.0f, 0.0f, 0.0f };
            std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
            bool lit = true;
            std::string colorMap;
            std::array<float, 4> colorMapUV{ 1.0f, 1.0f, 0.0f, 0.0f };
            bool twoSided = false;
            VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            bool enableDepthBias = false;
            float lineWidth = 1.0f;
            std::unordered_map<std::string, std::uint32_t> parameterSlots;
            std::unordered_map<std::string, std::array<float, 4>> vec4Parameters;
            std::unordered_map<std::string, float> floatParameters;
            std::unordered_map<std::string, std::uint32_t> textureSlots;
            std::unordered_map<std::string, std::string> textures;
        };

        static Vulkan& Get() { static Vulkan instance; return instance; }
        Vulkan(const Vulkan&) = delete;
        Vulkan& operator=(const Vulkan&) = delete;
        VulkanInitInfo Initialize(Window* window);
        void Shutdown();
        bool IsInitialized() const { return Device != VK_NULL_HANDLE; }
        void SetVSync(bool enabled) { if (m_VSync != enabled) { m_VSync = enabled; RecreateSwapchain(); } }
        bool IsVSync() const { return m_VSync; }
        bool AcquireLevelAssets(Level& level);
        bool ReleaseLevelAssets(Level& level);
        void ReleaseSkyMaterialInstance(Level::Sky& sky);
        bool DrawFrame(Level& level, double deltaTime, const Camera& camera, const std::function<void(VkCommandBuffer)>& guiRecorder = {}, bool falseOnEveryFail = true);
        void AssignPrimaryLevel(const std::shared_ptr<Level>& level);
        bool DrawFrame(double deltaTime, const Camera& camera, const std::function<void(VkCommandBuffer)>& guiRecorder = {}, bool falseOnEveryFail = true);
        bool BakeLighting(const Level& level, std::uint32_t resolution = 4096, float texelsPerWorldUnit = 32.0f, std::uint64_t maxAtlasBytes = 256ull * 1024ull * 1024ull, std::uint32_t maxAtlasPages = 16, std::uint32_t samplesPerTexel = 512);
        bool BakeLighting(std::uint32_t resolution = 4096, float texelsPerWorldUnit = 32.0f, std::uint64_t maxAtlasBytes = 256ull * 1024ull * 1024ull, std::uint32_t maxAtlasPages = 16, std::uint32_t samplesPerTexel = 512);
        MeshHandle LoadMeshAsset(std::string_view filename);
        MeshHandle RegisterMesh(const MeshData& data);
        void UnregisterMesh(MeshHandle handle);
        TextureHandle LoadTexture(std::string_view filename);
        void ReleaseTexture(TextureHandle texture);
        MaterialHandle CreateMaterial(const MaterialDesc& desc);
        MaterialHandle LoadMaterialAsset(std::string_view filename);
        bool DoesMaterialHaveTextures(MaterialHandle material);
        TextureHandle GetMaterialTextureAtSlot(MaterialHandle material, std::uint32_t slot = 0);
        bool SetMaterialColor(MaterialHandle material, const std::array<float, 4>& color);
        bool SetMaterialFloat(MaterialHandle material, std::string_view parameter, float value);
        bool SetMaterialVec4(MaterialHandle material, std::string_view parameter, const std::array<float, 4>& value);
        bool SetMaterialTexture(MaterialHandle material, std::string_view textureName, std::string_view filename);
        bool SetMaterialTextureAtSlot(MaterialHandle material, std::uint32_t slot, std::string_view filename);
    private:
        struct QueueFamilyIndices {
            std::uint32_t graphics = UINT32_MAX;
            std::uint32_t present = UINT32_MAX;
            bool Complete() const { return graphics != UINT32_MAX && present != UINT32_MAX; }
        };
        struct SwapchainSupport {
            VkSurfaceCapabilitiesKHR capabilities{};
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };
        struct GpuBuffer { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; };
        struct GpuSubMesh {
            std::uint32_t indexOffset = 0;
            std::uint32_t indexCount = 0;
            MaterialHandle defaultMaterial = 0;
        };
        struct GpuMesh {
            GpuBuffer vertexBuffer{};
            GpuBuffer indexBuffer{};
            std::uint32_t indexCount = 0;
            std::vector<GpuSubMesh> subMeshes;
            std::vector<SubMeshData> cpuSubMeshes;
            std::vector<MaterialHandle> ownedMaterials;
        };
        struct GpuTexture {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t layers = 1;
            std::uint32_t referenceCount = 0;
            bool permanent = false;
        };
        struct GpuImage {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
        };
        struct SceneTarget {
            GpuImage color;
            GpuImage worldPosition;
            GpuImage skyColor;
            GpuImage depth;
            GpuImage lightmapInfo;
            GpuImage lightmapCompositeColor;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            VkFramebuffer lightmapInfoFramebuffer = VK_NULL_HANDLE;
            VkFramebuffer lightmapCompositeFramebuffer = VK_NULL_HANDLE;
            VkDescriptorSet sceneInputDescriptorSet = VK_NULL_HANDLE;
            VkDescriptorSet postDescriptorSet = VK_NULL_HANDLE;
        };
        struct MaterialGpuData {
            std::array<float, 4> matUV{ 1.0f, 1.0f, 0.0f, 0.0f };
            std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
            std::array<float, 4> colorMapUV{ 1.0f, 1.0f, 0.0f, 0.0f };
            std::array<std::array<float, 4>, MaxMaterialTextures / 4> textureFlags{};
            std::array<std::array<float, 4>, MaxMaterialParameters> params{};
        };
        struct MaterialPipelineConfig {
            bool isSky = false;
            bool flatSky = false;
            VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            float lineWidth = 1.0f;
            bool enableDepthBias = false; // COSTLY BUT FIXES Z-FIGHTING AND SHADOW ACNE
        };
        struct MaterialFrameResources {
            GpuBuffer uniformBuffer{};
            void* mapped = nullptr;
            VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        };
        struct MaterialResource {
            std::string name;
            bool lit = true;
            std::string fragmentShaderSpv;
            MaterialGpuData gpuData{};
            MaterialPipelineConfig pipelineConfig{};
            std::unordered_map<std::string, std::uint32_t> parameterSlots;
            std::unordered_map<std::string, std::uint32_t> textureSlots;
            std::array<TextureHandle, MaxMaterialTextures> textures{};
            std::array<MaterialFrameResources, 2> frames{};
        };
        static constexpr std::uint32_t MaxRealtimeLights = 16;
        struct alignas(16) RealtimeLightingGpuData {
            std::array<std::array<float, 4>, MaxRealtimeLights> positionRadius{};
            std::array<std::array<float, 4>, MaxRealtimeLights> colorIntensity{};
            std::array<float, 4> sunDirectionIntensity{};
            std::array<float, 4> sunColor{};
            std::array<float, 4> metadata{}; // X = ACTIVE LIGHT COUNT, Y = USE REAL-TIME SUN
        };
        struct FrameSync {
            VkSemaphore imageAvailable = VK_NULL_HANDLE;
            VkSemaphore renderFinished = VK_NULL_HANDLE;
            VkFence inFlight = VK_NULL_HANDLE;
            GpuBuffer instanceBuffer{};
            void* mappedInstanceData = nullptr;
            VkDeviceSize instanceCapacity = 0;
            GpuBuffer lightmapParamsBuffer{};
            void* mappedLightmapParams = nullptr;
            VkDescriptorSet lightmapRuntimeDescriptorSet = VK_NULL_HANDLE;
            GpuBuffer realtimeLightingBuffer{};
            void* mappedRealtimeLighting = nullptr;
            VkDescriptorSet realtimeLightingDescriptorSet = VK_NULL_HANDLE;
        };
        struct RenderPushConstants {
            float viewProjection[16]{};
            float cameraPosition[4]{};
            float directionAndSize[4]{};
            float timeData[4]{};
            float screenData[4]{};
            float frameData[4]{};
        };
        struct PostProcessPushConstants {
            float cameraPosition[4]{};
            float fogParams[4]{};
            float fogColor[4]{};
            float outputParams[4]{};
        };
        struct InstanceData {
            float position[3]{};
            float size[3]{};
            float rotation[3]{};
            float lightmapScaleOffset[4]{};
            float lightmapPage = 0.0f;
        };
        struct PendingInstance {
            MeshHandle mesh = 0;
            std::uint32_t subMeshIndex = 0;
            MaterialHandle material = 0;
            InstanceData instance{};
        };
        struct Batch {
            MeshHandle mesh = 0;
            std::uint32_t subMeshIndex = 0;
            MaterialHandle material = 0;
            std::uint32_t firstInstance = 0;
            std::uint32_t instanceCount = 0;
        };
        struct BatchKey {
            MeshHandle mesh = 0;
            MaterialHandle material = 0;
            bool operator==(const BatchKey& other) const noexcept { return mesh == other.mesh && material == other.material; }
        };
        struct BatchKeyHash {
            std::size_t operator()(const BatchKey& key) const noexcept {
                const std::size_t a = std::hash<MeshHandle>{}(key.mesh);
                const std::size_t b = std::hash<MaterialHandle>{}(key.material);
                return a ^ (b + static_cast<std::size_t>(0x9e3779b9u) + (a << 6) + (a >> 2));
            }
        };
        struct BakedLightmapAssignment {
            std::uint32_t page = 0;
            std::array<float, 4> scaleOffset{ 1.0f, 1.0f, 0.0f, 0.0f };
        };
        struct BakedStaticLightInfo {
            const Level::Light* light = nullptr;
            std::array<float, 3> bakedPosition{};
        };
        static constexpr std::uint32_t MaxBakedStaticLights = 15;

        bool TryLoadBakedLighting(const Level& level);
        std::uint64_t ComputeBakedLightingSourceSignature(const Level& level) const;
        void CleanupSwapchain();
        VulkanInitInfo RecreateSwapchain();
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
        SwapchainSupport QuerySwapchainSupport(VkPhysicalDevice device) const;
        void DestroyDepthResources();
        std::uint32_t FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, GpuBuffer& outBuffer);
        void DestroyBuffer(GpuBuffer& buffer);
        bool CopyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size);
        void DestroyMeshGpuResource(GpuMesh& mesh);
        MeshHandle RegisterMeshInternal(const MeshData& data);
        Vulkan() = default;
        bool CreateTextureFromPixels(const unsigned char* rgba, std::uint32_t width, std::uint32_t height, GpuTexture& outTexture);
        bool CreateImageResource(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, GpuImage& outImage);
        bool CreateTextureArrayFromGeneratedPixels(std::uint32_t width, std::uint32_t height, std::uint32_t layers, std::uint64_t maxAllocationBytes, const std::function<void(std::uint32_t, unsigned char*)>& generateLayer, GpuTexture& outTexture);
        void DestroyTexture(GpuTexture& texture);
        void ReleaseTextureReference(TextureHandle texture);
        void DestroyUnreferencedMeshAsset(const std::string& path);
        void DestroyUnreferencedMaterialAsset(const std::string& path);
        void DestroyImageResource(GpuImage& image);
        void DestroySceneTargets();
        void UpdateMaterialDescriptorSet(MaterialResource& material, std::uint32_t frameIndex);
        VkPipeline CreateMaterialPipeline(const std::string& fragmentShaderPath, MaterialPipelineConfig config);
        void DestroyMaterialResource(MaterialResource& material);
        MaterialHandle CloneMaterial(MaterialHandle source, const std::string& uniqueName);

        Window* GameWindow = nullptr;
        VkInstance Instance = VK_NULL_HANDLE;
        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
        VkDevice Device = VK_NULL_HANDLE;
        VkQueue GraphicsQ = VK_NULL_HANDLE;
        VkQueue PresentQ = VK_NULL_HANDLE;
        VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
        VkFormat SwapchainFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D SwapchainExtent{};
        std::vector<VkImage> SwapchainImages;
        std::vector<VkImageView> SwapchainImageViews;
        std::vector<VkFramebuffer> SwapchainFramebuffers;
        VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
        VkImage DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory DepthImageMemory = VK_NULL_HANDLE;
        VkImageView DepthImageView = VK_NULL_HANDLE;
        VkRenderPass RenderPass = VK_NULL_HANDLE;
        VkRenderPass LightmapInfoRenderPass = VK_NULL_HANDLE;
        VkRenderPass LightmapCompositeRenderPass = VK_NULL_HANDLE;
        VkRenderPass PresentRenderPass = VK_NULL_HANDLE;
        std::array<VkPipeline, 4> LightmapInfoPipelines{};
        VkPipeline LightmapCompositePipeline = VK_NULL_HANDLE;
        VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
        VkPipeline GraphicsPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout MaterialDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool MaterialDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout PostDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool PostDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout LightmapRuntimeDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool LightmapRuntimeDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout RealtimeLightingDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool RealtimeLightingDescriptorPool = VK_NULL_HANDLE;
        VkPipelineLayout PostProcessPipelineLayout = VK_NULL_HANDLE;
        VkPipeline PostProcessPipeline = VK_NULL_HANDLE;
        VkSampler PostProcessSampler = VK_NULL_HANDLE;
        std::vector<SceneTarget> SceneTargets;
        VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat WorldPositionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat LightmapInfoFormat = VK_FORMAT_R16G16B16A16_UNORM;
        std::unordered_map<std::string, VkPipeline> ShaderPipelines;
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> CommandBuffers;
        static constexpr std::uint32_t MaxFramesInFlight = 2;
        std::array<FrameSync, MaxFramesInFlight> m_frames{};
        std::vector<VkFence> ImagesInFlight;
        std::uint32_t CurrentFrame = 0;
        std::uint64_t FrameCounter = 0;
        static constexpr std::size_t InitialInstanceCapacity = 4096;
        bool m_VSync = true;
        std::weak_ptr<Level> PrimaryLevel; // MAIN LEVEL
        MeshHandle NextMeshHandle = 0;
        MeshHandle CubeMeshHandle = 0;
        MeshHandle PlaneMeshHandle = 0;
        MeshHandle SphereMeshHandle = 0;
        MeshHandle CylinderMeshHandle = 0;
        MeshHandle WedgeMeshHandle = 0;
        MeshHandle CornerWedgeMeshHandle = 0;
        MeshHandle ConeMeshHandle = 0;
        MeshHandle PyramidMeshHandle = 0;
        std::unordered_map<MeshHandle, GpuMesh> Meshes;
        std::unordered_map<std::string, MeshHandle> FileMeshes;
        std::unordered_map<std::string, std::uint32_t> MeshAssetRefCounts;
        MaterialHandle NextMaterialHandle = 0;
        TextureHandle NextTextureHandle = 1;
        MaterialHandle ErrorMaterialHandle = 0;
        std::unordered_map<MaterialHandle, MaterialResource> Materials;
        std::unordered_map<TextureHandle, GpuTexture> Textures;
        std::unordered_map<std::string, TextureHandle> FileTextures;
        std::unordered_map<std::string, MaterialHandle> FileMaterials;
        std::unordered_map<std::string, std::uint32_t> MaterialAssetRefCounts;
        VkDescriptorSetLayout LightmapArrayDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool LightmapDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet LightmapArrayDescriptorSet = VK_NULL_HANDLE;
        GpuTexture LightmapFallbackArrayTexture{};
        GpuTexture BakedLightingAtlasArrayTexture{};
        std::uint32_t BakedLightingAtlasLayers = 0;
        std::uint32_t BakedLightingAtlasPages = 0;
        std::uint32_t BakedLightingLevelUID = 0;
        std::uint32_t BakedLightingCacheAttemptedLevelUID = 0;
        bool BakedLightingHasStaticSun = false;
        std::vector<BakedStaticLightInfo> BakedStaticLights;
        std::unordered_map<const Level::LevelObject*, BakedLightmapAssignment> BakedLightmapAssignments;
    };
}