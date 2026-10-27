//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Handle hard Vulkan stuff            //
//                                              //
//==============================================//

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(linux)
#define VK_USE_PLATFORM_XLIB_KHR
#elif defined(APPLE)
#define VK_USE_PLATFORM_METAL_EXT
#endif

#include "VulkanContext.h"
#include "PlatformWindow.h"
#include "Logger.h"
#include "../World/Camera.h"
#include "Console.h"
#include "ErrorMeshFallback.h"
#include "FileParsers/xatlas.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <limits>
#include <iterator>
#include <numeric>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <thread>
#include <cmath>
#if !defined(_WIN32) && __has_include("EmbeddedShaders.generated.h")
#include "EmbeddedShaders.generated.h"
#define ENERGYENGINE_HAS_GENERATED_EMBEDDED_SHADERS 1
#endif
#include "FileParsers/SaveLoadEnergyEngineFileFormats.h"

//#define VK_ENABLE_DEBUG_VALIDATION

static const auto engineStart = std::chrono::system_clock::now();
inline const char* VkResultToString(VkResult result) {
    switch (result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    default: return "VK_UNKNOWN_ERROR";
    }
}
namespace Engine
{
    namespace {
        template <typename Function>
        void ParallelForRanges(std::size_t count, std::size_t grainSize, Function&& function, std::size_t minParallelCount = 512u) {
            if (count == 0) return;
            grainSize = (std::max<std::size_t>)(grainSize, 1u);
            const unsigned int hardwareThreads = (std::max)(std::thread::hardware_concurrency(), 1u);
            const std::size_t taskCount = (count + grainSize - 1u) / grainSize;
            const std::size_t workerCount = (std::min)({ static_cast<std::size_t>((std::min)(hardwareThreads, 8u)), taskCount });
            if (workerCount <= 1u || count < minParallelCount) {
                function(0u, count);
                return;
            }
            std::atomic<std::size_t> next{ 0u };
            auto worker = [&]() {
                for (;;) {
                    const std::size_t begin = next.fetch_add(grainSize, std::memory_order_relaxed);
                    if (begin >= count) break;
                    function(begin, (std::min)(count, begin + grainSize));
                }
                };
            std::vector<std::thread> workers;
            workers.reserve(workerCount - 1u);
            for (std::size_t i = 1u; i < workerCount; ++i) workers.emplace_back(worker);
            worker();
            for (std::thread& thread : workers) thread.join();
        }

        bool GenerateCookedLightmapUVs(Vulkan::MeshData& data) {
            if (!data.IsValid()) return false;
            if (data.hasLightmapUVs) return true;
            struct PositionBitsKey {
                std::uint32_t x = 0, y = 0, z = 0;
                bool operator==(const PositionBitsKey& other) const noexcept {
                    return x == other.x && y == other.y && z == other.z;
                }
            };
            struct PositionBitsKeyHash {
                std::size_t operator()(const PositionBitsKey& key) const noexcept {
                    std::size_t h = std::hash<std::uint32_t>{}(key.x);
                    h ^= std::hash<std::uint32_t>{}(key.y) + static_cast<std::size_t>(0x9e3779b9u) + (h << 6) + (h >> 2);
                    h ^= std::hash<std::uint32_t>{}(key.z) + static_cast<std::size_t>(0x9e3779b9u) + (h << 6) + (h >> 2);
                    return h;
                }
            };
            std::size_t totalVertexCount = 0, totalIndexCount = 0;
            for (const auto& sub : data.subMeshes) { totalVertexCount += sub.vertices.size(); totalIndexCount += sub.indices.size(); }
            if (totalVertexCount == 0 || totalIndexCount == 0 || totalVertexCount > UINT32_MAX || totalIndexCount > UINT32_MAX) return false;
            std::vector<Vulkan::MeshVertex> sourceVertices;
            std::vector<std::size_t> submeshBaseVertices;
            sourceVertices.reserve(totalVertexCount);
            submeshBaseVertices.reserve(data.subMeshes.size());
            std::vector<std::array<float, 3>> geometryPositions;
            std::vector<std::uint32_t> geometryIndices;
            geometryPositions.reserve(totalVertexCount);
            geometryIndices.reserve(totalIndexCount);
            std::unordered_map<PositionBitsKey, std::uint32_t, PositionBitsKeyHash> weldedPositions;
            weldedPositions.reserve(totalVertexCount);
            for (const auto& sub : data.subMeshes) {
                const std::size_t baseVertex = sourceVertices.size();
                submeshBaseVertices.push_back(baseVertex);
                sourceVertices.insert(sourceVertices.end(), sub.vertices.begin(), sub.vertices.end());
                for (std::uint32_t idx : sub.indices) {
                    if (idx >= sub.vertices.size()) return false;
                    const std::size_t globalIndex = baseVertex + idx;
                    if (globalIndex > UINT32_MAX) return false;
                    const auto& pos = sourceVertices[globalIndex].position;
                    PositionBitsKey key{};
                    std::memcpy(&key.x, &pos[0], sizeof(float));
                    std::memcpy(&key.y, &pos[1], sizeof(float));
                    std::memcpy(&key.z, &pos[2], sizeof(float));
                    auto [it, inserted] = weldedPositions.try_emplace(key, static_cast<std::uint32_t>(geometryPositions.size()));
                    if (inserted) {
                        if (geometryPositions.size() >= UINT32_MAX) return false;
                        geometryPositions.push_back({ pos[0], pos[1], pos[2] });
                    }
                    geometryIndices.push_back(it->second);
                }
            }
            if (geometryPositions.empty() || geometryIndices.size() != totalIndexCount) return false;
            xatlas::Atlas* atlas = xatlas::Create();
            if (!atlas) {
                LOG_CATEGORY_ERROR("Lighting", "xatlas could not allocate an atlas while cooking UV1");
                return false;
            }
            xatlas::MeshDecl decl{};
            decl.vertexPositionData = geometryPositions.data();
            decl.vertexPositionStride = sizeof(std::array<float, 3>);
            decl.vertexCount = static_cast<std::uint32_t>(geometryPositions.size());
            decl.indexData = geometryIndices.data();
            decl.indexCount = static_cast<std::uint32_t>(geometryIndices.size());
            decl.faceCount = static_cast<std::uint32_t>(geometryIndices.size() / 3u);
            decl.indexFormat = xatlas::IndexFormat::UInt32;
            const xatlas::AddMeshError addResult = xatlas::AddMesh(atlas, decl);
            if (addResult != xatlas::AddMeshError::Success) {
                LOG_CATEGORY_ERROR("Lighting", "xatlas rejected mesh input (error {})", static_cast<int>(addResult));
                xatlas::Destroy(atlas);
                return false;
            }
            xatlas::PackOptions options{};
            options.resolution = 0;
            options.padding = 4;
            options.bilinear = true;
            xatlas::Generate(atlas, xatlas::ChartOptions{}, options);
            bool valid = atlas->meshCount == 1 && atlas->width > 0 && atlas->height > 0 && atlas->meshes[0].indexCount == geometryIndices.size() && atlas->meshes[0].vertexCount > 0;
            Vulkan::MeshData result{};
            result.hasLightmapUVs = true;
            result.subMeshes.reserve(data.subMeshes.size());
            if (valid) {
                const xatlas::Mesh& unwrapped = atlas->meshes[0];
                std::size_t globalIndexCursor = 0;
                for (std::size_t sm = 0; sm < data.subMeshes.size() && valid; ++sm) {
                    const auto& old = data.subMeshes[sm];
                    Vulkan::SubMeshData out{};
                    out.materialName = old.materialName;
                    out.defaultMaterialColor = old.defaultMaterialColor;
                    out.defaultMaterialTexture = old.defaultMaterialTexture;
                    out.indices.reserve(old.indices.size());
                    std::unordered_map<std::uint64_t, std::uint32_t> remap;
                    remap.reserve(old.indices.size());
                    for (std::size_t local = 0; local < old.indices.size(); ++local, ++globalIndexCursor) {
                        const std::uint32_t sourceLocalIndex = old.indices[local];
                        if (sourceLocalIndex >= old.vertices.size() || globalIndexCursor >= unwrapped.indexCount) {
                            valid = false;
                            break;
                        }
                        const std::uint32_t sourceGlobalIndex = static_cast<std::uint32_t>(submeshBaseVertices[sm] + sourceLocalIndex);
                        const std::uint32_t atlasIndex = unwrapped.indexArray[globalIndexCursor];
                        if (atlasIndex >= unwrapped.vertexCount) { valid = false; break; }
                        const xatlas::Vertex& xv = unwrapped.vertexArray[atlasIndex];
                        if (xv.xref >= geometryPositions.size() || xv.atlasIndex > 0) { valid = false; break; }
                        const std::uint64_t remapKey = (static_cast<std::uint64_t>(atlasIndex) << 32) | sourceGlobalIndex;
                        auto [remapIt, remapInserted] = remap.try_emplace(remapKey, static_cast<std::uint32_t>(out.vertices.size()));
                        if (remapInserted) {
                            if (out.vertices.size() >= UINT32_MAX) { valid = false; break; }
                            Vulkan::MeshVertex v = sourceVertices[sourceGlobalIndex];
                            if (xv.atlasIndex >= 0) {
                                v.lightmapUV[0] = xv.uv[0] / static_cast<float>(atlas->width);
                                v.lightmapUV[1] = xv.uv[1] / static_cast<float>(atlas->height);
                            }
                            else { v.lightmapUV[0] = 0.5f; v.lightmapUV[1] = 0.5f; }
                            out.vertices.push_back(v);
                            out.indices.push_back(remapIt->second);
                        }
                        else
                            out.indices.push_back(remapIt->second);
                    }
                    if (!out.IsValid()) valid = false;
                    result.subMeshes.push_back(std::move(out));
                }
            }
            const std::uint32_t chartCount = valid ? atlas->meshes[0].chartCount : 0;
            const std::uint32_t atlasWidth = valid ? atlas->width : 0;
            const std::uint32_t atlasHeight = valid ? atlas->height : 0;
            xatlas::Destroy(atlas);
            if (!valid || !result.IsValid()) {
                LOG_CATEGORY_ERROR("Lighting", "xatlas generated invalid UV1 output; the mesh will not be cached or uploaded");
                return false;
            }
            data = std::move(result);
            std::size_t finalVertexCount = 0;
            for (const auto& sub : data.subMeshes) finalVertexCount += sub.vertices.size();
            LOG_CATEGORY_INFO("Lighting", "Cooked UV1 with xatlas: {} charts, {} welded positions, {} output vertices, local atlas {}x{}", chartCount, geometryPositions.size(), finalVertexCount, atlasWidth, atlasHeight);
            return true;
        }

        bool PrepareBuiltInMeshData(std::string_view cacheName, Vulkan::MeshData& data) {
            const std::string cookedPath = FileParsers::BuiltInCookedMeshPath(cacheName);
            Vulkan::MeshData cooked{};
            if (FileParsers::LoadCookedMeshFile(cookedPath, cooked)) {
                data = std::move(cooked);
                LOG_CATEGORY_INFO("Vulkan", "Using cooked built-in mesh cache '{}'", cookedPath);
                return true;
            }
            if (!GenerateCookedLightmapUVs(data)) {
                LOG_CATEGORY_ERROR("Lighting", "Could not generate xatlas UV1 for built-in mesh '{}'", cacheName);
                return false;
            }
            if (!FileParsers::SaveCookedMeshFile(cookedPath, data)) {
                LOG_CATEGORY_WARN("Lighting", "Could not save cooked built-in mesh '{}' to '{}'", cacheName, cookedPath);
            }
            return true;
        }

#pragma pack(push, 1)
        struct LightmapCacheHeader {
            char magic[8];
            std::uint32_t version;
            std::uint32_t resolution;
            std::uint32_t pageCount;
            std::uint32_t layerCount;
            std::uint32_t assignmentCount;
            std::uint32_t staticLightCount;
            std::uint32_t hasStaticSun;
            std::uint32_t reserved;
            float sunDirection[3];
            std::uint64_t pixelBytes;
            std::uint64_t sourceSignature;
        };
        struct LightmapCacheAssignment {
            std::uint32_t objectIndex;
            std::uint32_t meshType;
            std::uint32_t mobility;
            std::uint32_t page;
            float scaleOffset[4];
            float position[3];
            float rotation[3];
            float size[3];
            std::uint64_t customMeshHash;
        };
        struct LightmapCacheLight {
            float position[3];
        };
#pragma pack(pop)

        std::uint64_t HashAssetString(std::string_view text) {
            std::uint64_t hash = 1469598103934665603ull;
            for (const unsigned char c : text) {
                hash ^= static_cast<std::uint64_t>(c);
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    Vulkan::VulkanInitInfo Vulkan::Initialize(Window* window) {
        if (!window)
            return { false, "No Window Found", __LINE__ };
        GameWindow = window;

        // CREATE INSTANCE
        LOG_CATEGORY_INFO("Vulkan", "Creating Instance...");
        std::vector<const char*> instanceExtensions = GameWindow->GetRequiredVulkanInstanceExtensions();
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        std::uint32_t extensionCount = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) == VK_SUCCESS) {
            std::vector<VkExtensionProperties> available(extensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, available.data());
            if (std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& extension) {return std::strcmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0; }))
                instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
#endif
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Energy Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
        appInfo.pEngineName = "Energy Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
        appInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instanceCreateInfo{};
#ifdef VK_ENABLE_DEBUG_VALIDATION
        const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
        instanceCreateInfo.enabledLayerCount = static_cast<std::uint32_t>(validationLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();
#endif
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pApplicationInfo = &appInfo;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        instanceCreateInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
        instanceCreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
        if (vkCreateInstance(&instanceCreateInfo, nullptr, &Instance) != VK_SUCCESS)
            return { false, "Vulkan Instance FAILED to create", __LINE__ };

        // CREATE SURFACE
        LOG_CATEGORY_INFO("Vulkan", "Required instance extensions:");
        for (const char* extension : instanceExtensions)
            LOG_CATEGORY_INFO("Vulkan", "  {}", extension);
        LOG_CATEGORY_INFO("Vulkan", "Creating Surface...");
        Surface = GameWindow->CreateVulkanSurface(Instance);
        if (Surface == VK_NULL_HANDLE)
            return { false, "Vulkan Surface FAILED to create", __LINE__ };

        // CREATE DEVICE
        LOG_CATEGORY_INFO("Vulkan", "Trying to pick a valid physical device...");
        std::uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(Instance, &deviceCount, nullptr);
        if (deviceCount == 0)
            return { false, "No Vulkan compatible GPU was found", __LINE__ };
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(Instance, &deviceCount, devices.data());
        Vulkan::QueueFamilyIndices selectedQFamilies{};
        for (VkPhysicalDevice device : devices) {
            const auto queueFamilies = FindQueueFamilies(device);
            if (!queueFamilies.Complete())
                continue;
            std::uint32_t deviceExtensionCount = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, nullptr);
            std::vector<VkExtensionProperties> available(deviceExtensionCount);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &deviceExtensionCount, available.data());
            bool hasSwapchain = false;
            for (const auto& extension : available)
                if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                    hasSwapchain = true;
                    break;
                }
            if (!hasSwapchain)
                continue;
            const auto swapchain = QuerySwapchainSupport(device);
            if (swapchain.formats.empty() || swapchain.presentModes.empty())
                continue;
            PhysicalDevice = device;
            selectedQFamilies = queueFamilies;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device, &properties);
            LOG_CATEGORY_INFO("Vulkan", "Valid GPU found: {}", properties.deviceName);
            break;
        }
        if (PhysicalDevice == VK_NULL_HANDLE)
            return { false, "No Vulkan compatible GPU was found", __LINE__ };
        VkFormatProperties hdrSceneFormatProperties{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &hdrSceneFormatProperties);
        constexpr VkFormatFeatureFlags RequiredHdrSceneFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((hdrSceneFormatProperties.optimalTilingFeatures & RequiredHdrSceneFeatures) == RequiredHdrSceneFeatures) {
            SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            LOG_CATEGORY_INFO("Vulkan", "Using RGBA16F linear-HDR scene buffers");
        }
        else {
            SceneColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
            LOG_CATEGORY_WARN("Vulkan", "RGBA16F scene buffers are unsupported; falling back to RGBA8 (HDR highlights will be limited)");
        }
        std::set<std::uint32_t> uniqueQueueFamilies = { selectedQFamilies.graphics,selectedQFamilies.present };
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        const float queuePriority = 1.0f;
        for (std::uint32_t family : uniqueQueueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueInfos.push_back(queueInfo);
        }
        std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        std::uint32_t deviceExtensionCount = 0;
        vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &deviceExtensionCount, nullptr);
        std::vector<VkExtensionProperties> available(deviceExtensionCount);
        vkEnumerateDeviceExtensionProperties(PhysicalDevice, nullptr, &deviceExtensionCount, available.data());
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        const bool hasPortabilitySubset = std::any_of(available.begin(), available.end(), [](const VkExtensionProperties& extension) {return std::strcmp(extension.extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0; });
        if (hasPortabilitySubset)
            deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
        VkPhysicalDeviceFeatures supportedFeatures{};
        vkGetPhysicalDeviceFeatures(PhysicalDevice, &supportedFeatures);
        if (supportedFeatures.independentBlend != VK_TRUE) {
            LOG_CATEGORY_ERROR("Vulkan", "The selected GPU does not support independentBlend");
            return { false, "Vulkan independentBlend feature is unsupported", __LINE__ };
        }
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.shaderSampledImageArrayDynamicIndexing = supportedFeatures.shaderSampledImageArrayDynamicIndexing;
        deviceFeatures.independentBlend = VK_TRUE;
        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
        deviceCreateInfo.pQueueCreateInfos = queueInfos.data();
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        deviceCreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if (vkCreateDevice(PhysicalDevice, &deviceCreateInfo, nullptr, &Device) != VK_SUCCESS)
            return { false, "Vulkan Device FAILED to Create", __LINE__ };

        // CREATE COMMAND POOL
        vkGetDeviceQueue(Device, selectedQFamilies.graphics, 0, &GraphicsQ);
        vkGetDeviceQueue(Device, selectedQFamilies.present, 0, &PresentQ);
        if (CommandPool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            createInfo.queueFamilyIndex = selectedQFamilies.graphics;
            createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if (vkCreateCommandPool(Device, &createInfo, nullptr, &CommandPool) != VK_SUCCESS)
                return { false, "Vulkan Command Pool FAILED to Create", __LINE__ };
        }

        // CREATE BUILT-IN MESHES
        // ERROR FALLBACK MESH
        {
            MeshData ErrorModel;
            ErrorModel.subMeshes.emplace_back();
            ErrorModel.subMeshes[0].vertices = {};
            ErrorModel.subMeshes[0].indices = {};
            std::stringstream ss(EmbeddedFallbackObj);
            std::string line;
            while (std::getline(ss, line)) {
                if (line.empty()) continue;
                std::stringstream line_ss(line);
                std::string prefix;
                line_ss >> prefix;
                if (prefix == "v") {
                    float x, y, z;
                    line_ss >> x >> z >> y;
                    ErrorModel.subMeshes[0].vertices.push_back({ { x, y, z }, { 0.0f, 0.0f } });
                }
                else if (prefix == "f") {
                    std::uint32_t idx1, idx2, idx3;
                    line_ss >> idx1 >> idx2 >> idx3;
                    ErrorModel.subMeshes[0].indices.push_back(idx1 - 1);
                    ErrorModel.subMeshes[0].indices.push_back(idx2 - 1);
                    ErrorModel.subMeshes[0].indices.push_back(idx3 - 1);
                }
            }
            if (!PrepareBuiltInMeshData("builtin_error_fallback", ErrorModel))
                return { false, "Could not prepare cooked error fallback mesh", __LINE__ };
            RegisterMeshInternal(ErrorModel);
        }
        // CUBE
        {
            MeshData CubeModel;
            CubeModel.subMeshes.emplace_back();
            CubeModel.subMeshes[0].vertices.reserve(24);
            CubeModel.subMeshes[0].indices.reserve(36);
            struct Face
            {
                std::array<float, 3> normal;
                std::array<float, 3> right;
                std::array<float, 3> up;
            };
            constexpr Face faces[] = {
                { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f } },
                { {-1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
                { { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { { 0.0f,-1.0f, 0.0f }, {-1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { { 0.0f, 0.0f,-1.0f }, {-1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }
            };
            constexpr std::array<std::array<float, 2>, 4> quadUV = { {
                { 0.0f, 0.0f },
                { 1.0f, 0.0f },
                { 1.0f, 1.0f },
                { 0.0f, 1.0f }
            } };
            for (std::uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
                const Face& face = faces[faceIndex];
                const std::uint32_t base = static_cast<std::uint32_t>(CubeModel.subMeshes[0].vertices.size());
                for (std::uint32_t corner = 0; corner < 4; ++corner) {
                    const float u = quadUV[corner][0];
                    const float v = quadUV[corner][1];
                    MeshVertex vertex{};
                    vertex.position[0] = face.normal[0] * 0.5f + face.right[0] * (u - 0.5f) + face.up[0] * (v - 0.5f);
                    vertex.position[1] = face.normal[1] * 0.5f + face.right[1] * (u - 0.5f) + face.up[1] * (v - 0.5f);
                    vertex.position[2] = face.normal[2] * 0.5f + face.right[2] * (u - 0.5f) + face.up[2] * (v - 0.5f);
                    vertex.uv[0] = u;
                    vertex.uv[1] = v;
                    CubeModel.subMeshes[0].vertices.push_back(vertex);
                }
                if (faceIndex == 2 || faceIndex == 3) {
                    CubeModel.subMeshes[0].indices.insert(CubeModel.subMeshes[0].indices.end(),
                        { base + 0, base + 2, base + 1, base + 0, base + 3, base + 2 });
                }
                else {
                    CubeModel.subMeshes[0].indices.insert(CubeModel.subMeshes[0].indices.end(),
                        { base + 0, base + 1, base + 2, base + 0, base + 2, base + 3 });
                }
            }
            if (!PrepareBuiltInMeshData("builtin_cube", CubeModel))
                return { false, "Could not prepare cooked cube mesh", __LINE__ };
            CubeMeshHandle = RegisterMeshInternal(CubeModel);
            if (CubeMeshHandle == 0)
                return { false, "Vulkan Cube Mesh Resource FAILED to Create", __LINE__ };
        }
        // PLANE
        {
            MeshData PlaneModel;
            PlaneModel.subMeshes.emplace_back();
            PlaneModel.subMeshes[0].vertices = {
                { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } },
                { {  0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f } },
                { {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f } },
                { { -0.5f,  0.5f, 0.0f }, { 0.0f, 1.0f } }
            };
            PlaneModel.subMeshes[0].indices = { 0, 2, 1,  0, 3, 2 };
            if (!PrepareBuiltInMeshData("builtin_plane", PlaneModel))
                return { false, "Could not prepare cooked plane mesh", __LINE__ };
            PlaneMeshHandle = RegisterMeshInternal(PlaneModel);
            if (PlaneMeshHandle == 0)
                return { false, "Vulkan Plane Mesh Resource FAILED to Create", __LINE__ };
        }
        // SPHERE
        {
            MeshData SphereModel;
            SphereModel.subMeshes.emplace_back();
            constexpr int sectors = 16;
            constexpr int stacks = 16;
            for (int i = 0; i <= stacks; ++i) {
                float v = static_cast<float>(i) / static_cast<float>(stacks);
                float phi = v * static_cast<float>(M_PI);
                for (int j = 0; j <= sectors; ++j) {
                    float u = static_cast<float>(j) / static_cast<float>(sectors);
                    float theta = u * 2.0f * static_cast<float>(M_PI);
                    MeshVertex vertex{};
                    vertex.position[0] = cosf(theta) * sinf(phi) * 0.5f;
                    vertex.position[1] = cosf(phi) * 0.5f;
                    vertex.position[2] = sinf(theta) * sinf(phi) * 0.5f;
                    vertex.uv[0] = u;
                    vertex.uv[1] = v;
                    SphereModel.subMeshes[0].vertices.push_back(vertex);
                }
            }
            for (int i = 0; i < stacks; ++i) {
                for (int j = 0; j < sectors; ++j) {
                    std::uint32_t first = (i * (sectors + 1)) + j;
                    std::uint32_t second = first + sectors + 1;
                    SphereModel.subMeshes[0].indices.push_back(first);
                    SphereModel.subMeshes[0].indices.push_back(first + 1);
                    SphereModel.subMeshes[0].indices.push_back(second);
                    SphereModel.subMeshes[0].indices.push_back(second);
                    SphereModel.subMeshes[0].indices.push_back(first + 1);
                    SphereModel.subMeshes[0].indices.push_back(second + 1);
                }
            }
            if (!PrepareBuiltInMeshData("builtin_sphere", SphereModel))
                return { false, "Could not prepare cooked sphere mesh", __LINE__ };
            SphereMeshHandle = RegisterMeshInternal(SphereModel);
            if (SphereMeshHandle == 0)
                return { false, "Vulkan Sphere Mesh Resource FAILED to Create", __LINE__ };
        }
        // CYLINDER
        {
            MeshData CylinderModel;
            CylinderModel.subMeshes.emplace_back();
            constexpr int segments = 16;
            for (int i = 0; i <= segments; ++i) {
                float u = static_cast<float>(i) / static_cast<float>(segments);
                float theta = u * 2.0f * static_cast<float>(M_PI);
                float c = cosf(theta);
                float s = sinf(theta);
                MeshVertex vBottom{};
                vBottom.position[0] = c * 0.5f;
                vBottom.position[1] = -0.5f;
                vBottom.position[2] = s * 0.5f;
                vBottom.uv[0] = u;
                vBottom.uv[1] = 0.0f;
                CylinderModel.subMeshes[0].vertices.push_back(vBottom);
                MeshVertex vTop{};
                vTop.position[0] = c * 0.5f;
                vTop.position[1] = 0.5f;
                vTop.position[2] = s * 0.5f;
                vTop.uv[0] = u;
                vTop.uv[1] = 1.0f;
                CylinderModel.subMeshes[0].vertices.push_back(vTop);
            }
            for (int i = 0; i < segments; ++i) {
                CylinderModel.subMeshes[0].indices.push_back(i * 2);
                CylinderModel.subMeshes[0].indices.push_back(i * 2 + 1);
                CylinderModel.subMeshes[0].indices.push_back((i + 1) * 2);
                CylinderModel.subMeshes[0].indices.push_back(i * 2 + 1);
                CylinderModel.subMeshes[0].indices.push_back((i + 1) * 2 + 1);
                CylinderModel.subMeshes[0].indices.push_back((i + 1) * 2);
            }
            std::uint32_t botCenterIdx = static_cast<std::uint32_t>(CylinderModel.subMeshes[0].vertices.size());
            CylinderModel.subMeshes[0].vertices.push_back({ { 0.0f, -0.5f, 0.0f }, { 0.5f, 0.5f } });
            for (int i = 0; i <= segments; ++i) {
                float theta = (static_cast<float>(i) / segments) * 2.0f * static_cast<float>(M_PI);
                CylinderModel.subMeshes[0].vertices.push_back({ { cosf(theta) * 0.5f, -0.5f, sinf(theta) * 0.5f }, { cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f } });
            }
            for (int i = 0; i < segments; ++i) {
                CylinderModel.subMeshes[0].indices.push_back(botCenterIdx);
                CylinderModel.subMeshes[0].indices.push_back(botCenterIdx + 1 + i);
                CylinderModel.subMeshes[0].indices.push_back(botCenterIdx + 1 + (i + 1) % (segments + 1));
            }
            std::uint32_t topCenterIdx = static_cast<std::uint32_t>(CylinderModel.subMeshes[0].vertices.size());
            CylinderModel.subMeshes[0].vertices.push_back({ { 0.0f, 0.5f, 0.0f }, { 0.5f, 0.5f } });
            for (int i = 0; i <= segments; ++i) {
                float theta = (static_cast<float>(i) / segments) * 2.0f * static_cast<float>(M_PI);
                CylinderModel.subMeshes[0].vertices.push_back({ { cosf(theta) * 0.5f, 0.5f, sinf(theta) * 0.5f }, { cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f } });
            }
            for (int i = 0; i < segments; ++i) {
                CylinderModel.subMeshes[0].indices.push_back(topCenterIdx);
                CylinderModel.subMeshes[0].indices.push_back(topCenterIdx + 1 + (i + 1) % (segments + 1));
                CylinderModel.subMeshes[0].indices.push_back(topCenterIdx + 1 + i);
            }
            if (!PrepareBuiltInMeshData("builtin_cylinder", CylinderModel))
                return { false, "Could not prepare cooked cylinder mesh", __LINE__ };
            CylinderMeshHandle = RegisterMeshInternal(CylinderModel);
            if (CylinderMeshHandle == 0)
                return { false, "Vulkan Cylinder Mesh Resource FAILED to Create", __LINE__ };
        }
        // WEDGE
        {
            MeshData WedgeModel;
            WedgeModel.subMeshes.emplace_back();
            WedgeModel.subMeshes[0].vertices = {
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } }, { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
                { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } }, { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f } }, { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f } },
                { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } }, { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f } }, { {  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f } },
                { {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f } }, { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f } },
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } }, { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } }, { {  0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
                { {  0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f } }
            };
            WedgeModel.subMeshes[0].indices = { 0, 1, 2,  0, 2, 3,  4, 6, 5,  4, 7, 6,  8, 9, 10,  8, 10, 11,  12, 14, 13,  15, 17, 16 };
            if (!PrepareBuiltInMeshData("builtin_wedge", WedgeModel))
                return { false, "Could not prepare cooked wedge mesh", __LINE__ };
            WedgeMeshHandle = RegisterMeshInternal(WedgeModel);
            if (WedgeMeshHandle == 0)
                return { false, "Vulkan Wedge Mesh Resource FAILED to Create", __LINE__ };
        }
        // CORNERWEDGE
        {
            MeshData CornerWedgeModel;
            CornerWedgeModel.subMeshes.emplace_back();
            CornerWedgeModel.subMeshes[0].vertices = {
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
                { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f } },
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f } },
                { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.5f } },
                { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f } }
            };
            CornerWedgeModel.subMeshes[0].indices = { 0, 1, 2,  3, 5, 4,  6, 7, 8,  9, 11, 10 };
            if (!PrepareBuiltInMeshData("builtin_corner_wedge", CornerWedgeModel))
                return { false, "Could not prepare cooked corner wedge mesh", __LINE__ };
            CornerWedgeMeshHandle = RegisterMeshInternal(CornerWedgeModel);
            if (CornerWedgeMeshHandle == 0)
                return { false, "Vulkan CornerWedge Mesh Resource FAILED to Create", __LINE__ };
        }
        // CONE
        {
            MeshData ConeModel;
            ConeModel.subMeshes.emplace_back();
            constexpr int segments = 16;
            ConeModel.subMeshes[0].vertices.push_back({ { 0.0f, 0.5f, 0.0f }, { 0.5f, 1.0f } });
            for (int i = 0; i <= segments; ++i) {
                float u = static_cast<float>(i) / static_cast<float>(segments);
                float theta = u * 2.0f * static_cast<float>(M_PI);
                ConeModel.subMeshes[0].vertices.push_back({ { cosf(theta) * 0.5f, -0.5f, sinf(theta) * 0.5f }, { u, 0.0f } });
            }
            for (int i = 0; i < segments; ++i) {
                ConeModel.subMeshes[0].indices.push_back(0);
                ConeModel.subMeshes[0].indices.push_back(1 + i + 1);
                ConeModel.subMeshes[0].indices.push_back(1 + i);
            }
            std::uint32_t baseCenterIdx = static_cast<std::uint32_t>(ConeModel.subMeshes[0].vertices.size());
            ConeModel.subMeshes[0].vertices.push_back({ { 0.0f, -0.5f, 0.0f }, { 0.5f, 0.5f } });
            for (int i = 0; i <= segments; ++i) {
                float theta = (static_cast<float>(i) / segments) * 2.0f * static_cast<float>(M_PI);
                ConeModel.subMeshes[0].vertices.push_back({ { cosf(theta) * 0.5f, -0.5f, sinf(theta) * 0.5f }, { cosf(theta) * 0.5f + 0.5f, sinf(theta) * 0.5f + 0.5f } });
            }
            for (int i = 0; i < segments; ++i) {
                ConeModel.subMeshes[0].indices.push_back(baseCenterIdx);
                ConeModel.subMeshes[0].indices.push_back(baseCenterIdx + 1 + i);
                ConeModel.subMeshes[0].indices.push_back(baseCenterIdx + 1 + (i + 1) % (segments + 1));
            }
            if (!PrepareBuiltInMeshData("builtin_cone", ConeModel))
                return { false, "Could not prepare cooked cone mesh", __LINE__ };
            ConeMeshHandle = RegisterMeshInternal(ConeModel);
            if (ConeMeshHandle == 0)
                return { false, "Vulkan Cone Mesh Resources FAILED to Create", __LINE__ };
        }
        // PYRAMID
        {
            MeshData PyramidModel;
            PyramidModel.subMeshes.emplace_back();
            PyramidModel.subMeshes[0].vertices = {
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
                { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f } },
                { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f } },
                { {  0.0f,  0.5f,  0.0f }, { 0.5f, 1.0f } },
                { {  0.5f, -0.5f,  0.5f }, { 0.0f, 0.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
                { {  0.0f,  0.5f,  0.0f }, { 0.5f, 1.0f } },
                { {  0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f } },
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { {  0.0f,  0.5f,  0.0f }, { 0.5f, 1.0f } },
                { { -0.5f, -0.5f, -0.5f }, { 0.0f, 0.0f } },
                { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f } },
                { {  0.0f,  0.5f,  0.0f }, { 0.5f, 1.0f } }
            };
            PyramidModel.subMeshes[0].indices = { 0, 2, 1,  0, 3, 2,  4, 6, 5,  7, 9, 8,  10, 12, 11,  13, 15, 14 };
            if (!PrepareBuiltInMeshData("builtin_pyramid", PyramidModel))
                return { false, "Could not prepare cooked pyramid mesh", __LINE__ };
            PyramidMeshHandle = RegisterMeshInternal(PyramidModel);
            if (PyramidMeshHandle == 0)
                return { false, "Vulkan Pyramid Mesh Resource FAILED to Create", __LINE__ };
        }

        // CREATE SWAPCHAIN
        Vulkan::VulkanInitInfo swapchainCreateResult = RecreateSwapchain();
        if (!swapchainCreateResult.IsSuccess)
            return swapchainCreateResult;
        if (LightmapRuntimeDescriptorPool == VK_NULL_HANDLE) {
            VkDescriptorPoolSize runtimePoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight };
            VkDescriptorPoolCreateInfo runtimePoolInfo{};
            runtimePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            runtimePoolInfo.maxSets = MaxFramesInFlight;
            runtimePoolInfo.poolSizeCount = 1;
            runtimePoolInfo.pPoolSizes = &runtimePoolSize;
            if (vkCreateDescriptorPool(Device, &runtimePoolInfo, nullptr, &LightmapRuntimeDescriptorPool) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Runtime Descriptor Pool FAILED to Create", __LINE__ };
        }
        ImagesInFlight.resize(SwapchainImages.size(), VK_NULL_HANDLE);
        if (Device == VK_NULL_HANDLE || MaterialDescriptorSetLayout == VK_NULL_HANDLE)
            return { false, "Vulkan Material Descriptor Pool FAILED to Create", __LINE__ };
        if (MaterialDescriptorPool == VK_NULL_HANDLE) {
            std::array<VkDescriptorPoolSize, 2> sizes{};
            sizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2048 };
            sizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048 * MaxMaterialTextures };
            VkDescriptorPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            info.maxSets = 2048;
            info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
            info.pPoolSizes = sizes.data();
            if (vkCreateDescriptorPool(Device, &info, nullptr, &MaterialDescriptorPool) != VK_SUCCESS)
                return { false, "Vulkan Material Descriptor Pool FAILED to Create", __LINE__ };
        }

        // CREATE BUILT-IN MATERIALS
        // WHITE TEXTURE
        {
            const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
            GpuTexture white{};
            if (!CreateTextureFromPixels(whitePixel, 1, 1, white))
                return { false, "Vulkan White Fallback Texture FAILED to Create", __LINE__ };
            white.permanent = true;
            Textures.emplace(0, std::move(white));
        }
        if (LightmapFallbackArrayTexture.image == VK_NULL_HANDLE) {
            const auto makeNeutralLayer = [](std::uint32_t, unsigned char* pixels) {pixels[0] = 16u; pixels[1] = 16u; pixels[2] = 16u; pixels[3] = 255u; };
            if (!CreateTextureArrayFromGeneratedPixels(1, 1, 1, 1024ull * 1024ull, makeNeutralLayer, LightmapFallbackArrayTexture))
                return { false, "Neutral lightmap-array fallback FAILED to Create", __LINE__ };
        }
        if (LightmapArrayDescriptorSet == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = LightmapDescriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &LightmapArrayDescriptorSetLayout;
            if (vkAllocateDescriptorSets(Device, &allocate, &LightmapArrayDescriptorSet) != VK_SUCCESS)
                return { false, "Lightmap-array fallback descriptor set FAILED to Create", __LINE__ };
        }
        {
            VkDescriptorImageInfo fallbackInfo{};
            fallbackInfo.sampler = LightmapFallbackArrayTexture.sampler;
            fallbackInfo.imageView = LightmapFallbackArrayTexture.view;
            fallbackInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet fallbackWrite{};
            fallbackWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            fallbackWrite.dstSet = LightmapArrayDescriptorSet;
            fallbackWrite.dstBinding = 0;
            fallbackWrite.descriptorCount = 1;
            fallbackWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            fallbackWrite.pImageInfo = &fallbackInfo;
            vkUpdateDescriptorSets(Device, 1, &fallbackWrite, 0, nullptr);
        }
        // MISSING MATERIAL
        {
            MaterialDesc missing{};
            missing.name = "__MissingMaterial";
            missing.fragmentShaderSpv = FileParsers::MissingFragmentShaderPath;
            missing.color = { 1.0f, 1.0f, 1.0f, 1.0f };
            CreateMaterial(missing);
        }
        // ERROR MATERIAL
        {
            MaterialDesc error{};
            error.name = "__ErrorMaterial";
            error.fragmentShaderSpv = FileParsers::ErrorModelFragmentShaderPath;
            error.color = { 1.0f, 0.0f, 0.0f, 1.0f };
            ErrorMaterialHandle = CreateMaterial(error);
            if (ErrorMaterialHandle == 0)
                return { false, "Vulkan Error Material FAILED to Create", __LINE__ };
        }
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (auto& frame : m_frames) {
            if (vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS)
                return { false, "Vulkan Semaphore FAILED to Create", __LINE__ };
            if (vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &frame.renderFinished) != VK_SUCCESS)
                return { false, "Vulkan Semaphore Finish FAILED to Create", __LINE__ };
            if (vkCreateFence(Device, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS)
                return { false, "Vulkan Frame Fence FAILED to Create", __LINE__ };
        }
        for (FrameSync& frame : m_frames) {
            const VkDeviceSize bytes = static_cast<VkDeviceSize>(InitialInstanceCapacity * sizeof(InstanceData));
            if (!CreateBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.instanceBuffer))
                return { false, "Vulkan Instance Buffers FAILED to Create", __LINE__ };
            void* mapped = nullptr;
            if (vkMapMemory(Device, frame.instanceBuffer.memory, 0, bytes, 0, &mapped) != VK_SUCCESS)
                return { false, "Vulkan Instance Buffers FAILED to Create", __LINE__ };
            frame.mappedInstanceData = mapped;
            frame.instanceCapacity = InitialInstanceCapacity;

            const VkDeviceSize lightmapParamsBytes = sizeof(MaterialGpuData);
            if (!CreateBuffer(lightmapParamsBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.lightmapParamsBuffer))
                return { false, "Vulkan Lightmap Runtime Uniform Buffer FAILED to Create", __LINE__ };
            if (vkMapMemory(Device, frame.lightmapParamsBuffer.memory, 0, lightmapParamsBytes, 0, &frame.mappedLightmapParams) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Runtime Uniform Buffer FAILED to Map", __LINE__ };
            MaterialGpuData neutralRuntimeData{};
            std::memcpy(frame.mappedLightmapParams, &neutralRuntimeData, sizeof(neutralRuntimeData));
            VkDescriptorSetAllocateInfo runtimeAllocate{};
            runtimeAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            runtimeAllocate.descriptorPool = LightmapRuntimeDescriptorPool;
            runtimeAllocate.descriptorSetCount = 1;
            runtimeAllocate.pSetLayouts = &LightmapRuntimeDescriptorSetLayout;
            if (vkAllocateDescriptorSets(Device, &runtimeAllocate, &frame.lightmapRuntimeDescriptorSet) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Runtime Descriptor Set FAILED to Allocate", __LINE__ };
            VkDescriptorBufferInfo runtimeBufferInfo{};
            runtimeBufferInfo.buffer = frame.lightmapParamsBuffer.buffer;
            runtimeBufferInfo.offset = 0;
            runtimeBufferInfo.range = lightmapParamsBytes;
            VkWriteDescriptorSet runtimeWrite{};
            runtimeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            runtimeWrite.dstSet = frame.lightmapRuntimeDescriptorSet;
            runtimeWrite.dstBinding = 0;
            runtimeWrite.descriptorCount = 1;
            runtimeWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            runtimeWrite.pBufferInfo = &runtimeBufferInfo;
            vkUpdateDescriptorSets(Device, 1, &runtimeWrite, 0, nullptr);

            const VkDeviceSize realtimeBytes = sizeof(RealtimeLightingGpuData);
            if (!CreateBuffer(realtimeBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.realtimeLightingBuffer))
                return { false, "Vulkan Realtime Lighting Uniform Buffer FAILED to Create", __LINE__ };
            if (vkMapMemory(Device, frame.realtimeLightingBuffer.memory, 0, realtimeBytes, 0, &frame.mappedRealtimeLighting) != VK_SUCCESS)
                return { false, "Vulkan Realtime Lighting Uniform Buffer FAILED to Map", __LINE__ };
            RealtimeLightingGpuData neutralRealtimeData{};
            std::memcpy(frame.mappedRealtimeLighting, &neutralRealtimeData, sizeof(neutralRealtimeData));
            VkDescriptorSetAllocateInfo realtimeAllocate{};
            realtimeAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            realtimeAllocate.descriptorPool = RealtimeLightingDescriptorPool;
            realtimeAllocate.descriptorSetCount = 1;
            realtimeAllocate.pSetLayouts = &RealtimeLightingDescriptorSetLayout;
            if (vkAllocateDescriptorSets(Device, &realtimeAllocate, &frame.realtimeLightingDescriptorSet) != VK_SUCCESS)
                return { false, "Vulkan Realtime Lighting Descriptor Set FAILED to Allocate", __LINE__ };
            VkDescriptorBufferInfo realtimeBufferInfo{};
            realtimeBufferInfo.buffer = frame.realtimeLightingBuffer.buffer;
            realtimeBufferInfo.offset = 0;
            realtimeBufferInfo.range = realtimeBytes;
            VkWriteDescriptorSet realtimeWrite{};
            realtimeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            realtimeWrite.dstSet = frame.realtimeLightingDescriptorSet;
            realtimeWrite.dstBinding = 0;
            realtimeWrite.descriptorCount = 1;
            realtimeWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            realtimeWrite.pBufferInfo = &realtimeBufferInfo;
            vkUpdateDescriptorSets(Device, 1, &realtimeWrite, 0, nullptr);
        }

        // INIT IMGUI
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = Instance;
        initInfo.PhysicalDevice = PhysicalDevice;
        initInfo.Device = Device;
        initInfo.QueueFamily = selectedQFamilies.graphics;
        initInfo.Queue = GraphicsQ;
        initInfo.DescriptorPoolSize = 8;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = static_cast<std::uint32_t>(SwapchainImages.size());
        initInfo.PipelineInfoMain.RenderPass = PresentRenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        if (!ImGui_ImplVulkan_Init(&initInfo))
            return { false, "ImGui Vulkan initialization FAILED", __LINE__ };
        if (auto level = PrimaryLevel.lock())
            AssignPrimaryLevel(level);
        for (const auto& loadedLevel : LevelManager::GetInstance().GetLoadedLevels()) {
            if (loadedLevel && !AcquireLevelAssets(*loadedLevel))
                LOG_CATEGORY_ERROR("Assets", "Could not acquire all runtime dependencies for level '{}' during Vulkan initialization", loadedLevel->GetName());
        }
        if (auto explicitlyAssigned = PrimaryLevel.lock())
            if (!AcquireLevelAssets(*explicitlyAssigned))
                LOG_CATEGORY_ERROR("Assets", "Could not acquire dependencies for the assigned level '{}'", explicitlyAssigned->GetName());
        return { true, "Vulkan Initialization SUCCESS", 0 };
    }

    void Vulkan::Shutdown() {
        LOG_CATEGORY_INFO("Vulkan", "Shutting down...");
        if (Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(Device);
            for (const auto& loadedLevel : LevelManager::GetInstance().GetLoadedLevels())
                if (loadedLevel) ReleaseLevelAssets(*loadedLevel);
            if (auto assignedLevel = PrimaryLevel.lock()) ReleaseLevelAssets(*assignedLevel);
            for (auto& [handle, mesh] : Meshes)
                DestroyMeshGpuResource(mesh);
            Meshes.clear();
            CubeMeshHandle = 0;
            PlaneMeshHandle = 0;
            SphereMeshHandle = 0;
            CylinderMeshHandle = 0;
            WedgeMeshHandle = 0;
            CornerWedgeMeshHandle = 0;
            ConeMeshHandle = 0;
            PyramidMeshHandle = 0;
            if (Device == VK_NULL_HANDLE) { for (auto& frame : m_frames) frame.instanceBuffer = {}; return; }
            for (FrameSync& frame : m_frames) {
                if (frame.mappedInstanceData && frame.instanceBuffer.memory != VK_NULL_HANDLE)
                    vkUnmapMemory(Device, frame.instanceBuffer.memory);
                frame.mappedInstanceData = nullptr;
                frame.instanceCapacity = 0;
                DestroyBuffer(frame.instanceBuffer);
            }
            for (auto& [materialId, material] : Materials) { (void)materialId; DestroyMaterialResource(material); }
            Materials.clear();
            if (MaterialDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(Device, MaterialDescriptorPool, nullptr); MaterialDescriptorPool = VK_NULL_HANDLE; }
            for (auto& [textureId, texture] : Textures) { (void)textureId; DestroyTexture(texture); }
            Textures.clear();
            FileTextures.clear();
            FileMaterials.clear();
            if (LightmapDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(Device, LightmapDescriptorPool, nullptr); LightmapDescriptorPool = VK_NULL_HANDLE; }
            LightmapArrayDescriptorSet = VK_NULL_HANDLE;
            if (BakedLightingAtlasArrayTexture.image != VK_NULL_HANDLE)DestroyTexture(BakedLightingAtlasArrayTexture);
            if (LightmapFallbackArrayTexture.image != VK_NULL_HANDLE)DestroyTexture(LightmapFallbackArrayTexture);
            BakedLightingAtlasLayers = 0;
            BakedLightingAtlasPages = 0;
            BakedLightingHasStaticSun = false;
            BakedStaticLights.clear();
            BakedLightmapAssignments.clear();
            BakedLightingLevelUID = 0;
            BakedLightingCacheAttemptedLevelUID = 0;
            ErrorMaterialHandle = 0;
            if (LightmapRuntimeDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(Device, LightmapRuntimeDescriptorPool, nullptr); LightmapRuntimeDescriptorPool = VK_NULL_HANDLE; }
            if (RealtimeLightingDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(Device, RealtimeLightingDescriptorPool, nullptr); RealtimeLightingDescriptorPool = VK_NULL_HANDLE; }
            for (auto& frame : m_frames) {
                if (frame.mappedLightmapParams && frame.lightmapParamsBuffer.memory != VK_NULL_HANDLE) vkUnmapMemory(Device, frame.lightmapParamsBuffer.memory);
                frame.mappedLightmapParams = nullptr;
                DestroyBuffer(frame.lightmapParamsBuffer);
                if (frame.mappedRealtimeLighting && frame.realtimeLightingBuffer.memory != VK_NULL_HANDLE) vkUnmapMemory(Device, frame.realtimeLightingBuffer.memory);
                frame.mappedRealtimeLighting = nullptr;
                DestroyBuffer(frame.realtimeLightingBuffer);
                if (frame.imageAvailable)vkDestroySemaphore(Device, frame.imageAvailable, nullptr);
                if (frame.renderFinished)vkDestroySemaphore(Device, frame.renderFinished, nullptr);
                if (frame.inFlight)vkDestroyFence(Device, frame.inFlight, nullptr);
                frame = {};
            }
            CleanupSwapchain();
            if (MaterialDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Device, MaterialDescriptorSetLayout, nullptr); MaterialDescriptorSetLayout = VK_NULL_HANDLE; }
            if (LightmapArrayDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Device, LightmapArrayDescriptorSetLayout, nullptr); LightmapArrayDescriptorSetLayout = VK_NULL_HANDLE; }
            if (LightmapRuntimeDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Device, LightmapRuntimeDescriptorSetLayout, nullptr); LightmapRuntimeDescriptorSetLayout = VK_NULL_HANDLE; }
            if (RealtimeLightingDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Device, RealtimeLightingDescriptorSetLayout, nullptr); RealtimeLightingDescriptorSetLayout = VK_NULL_HANDLE; }
            if (CommandPool != VK_NULL_HANDLE) { vkDestroyCommandPool(Device, CommandPool, nullptr); CommandPool = VK_NULL_HANDLE; }
            vkDestroyDevice(Device, nullptr);
            Device = VK_NULL_HANDLE;
        }
        if (Surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(Instance, Surface, nullptr);
            Surface = VK_NULL_HANDLE;
        }
        if (Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(Instance, nullptr);
            Instance = VK_NULL_HANDLE;
        }
        PhysicalDevice = VK_NULL_HANDLE;
        GraphicsQ = VK_NULL_HANDLE;
        PresentQ = VK_NULL_HANDLE;
        GameWindow = nullptr;
        PrimaryLevel.reset();
        FileMeshes.clear();
        MeshAssetRefCounts.clear();
        MaterialAssetRefCounts.clear();
        FileTextures.clear();
        FileMaterials.clear();
        NextMeshHandle = 1;
        NextMaterialHandle = 1;
        NextTextureHandle = 1;
        CubeMeshHandle = 0;
        PlaneMeshHandle = 0;
        CurrentFrame = 0;
    }

    void Vulkan::DestroyDepthResources() {
        if (Device == VK_NULL_HANDLE)return;
        if (DepthImageView != VK_NULL_HANDLE) { vkDestroyImageView(Device, DepthImageView, nullptr); DepthImageView = VK_NULL_HANDLE; }
        if (DepthImage != VK_NULL_HANDLE) { vkDestroyImage(Device, DepthImage, nullptr); DepthImage = VK_NULL_HANDLE; }
        if (DepthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(Device, DepthImageMemory, nullptr); DepthImageMemory = VK_NULL_HANDLE; }
        DepthFormat = VK_FORMAT_UNDEFINED;
    }

    void Vulkan::CleanupSwapchain() {
        if (Device == VK_NULL_HANDLE)return;
        for (VkFramebuffer framebuffer : SwapchainFramebuffers)if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(Device, framebuffer, nullptr);
        SwapchainFramebuffers.clear();
        for (auto& target : SceneTargets)
            if (target.framebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(Device, target.framebuffer, nullptr); target.framebuffer = VK_NULL_HANDLE; }
        if (!CommandBuffers.empty()) { vkFreeCommandBuffers(Device, CommandPool, static_cast<std::uint32_t>(CommandBuffers.size()), CommandBuffers.data()); CommandBuffers.clear(); }
        for (auto& [shaderPath, pipeline] : ShaderPipelines) { (void)shaderPath; if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, pipeline, nullptr); }
        ShaderPipelines.clear();
        GraphicsPipeline = VK_NULL_HANDLE;
        for (VkPipeline& pipeline : LightmapInfoPipelines) {
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (LightmapCompositePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(Device, LightmapCompositePipeline, nullptr); LightmapCompositePipeline = VK_NULL_HANDLE; }
        if (PostProcessPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(Device, PostProcessPipeline, nullptr); PostProcessPipeline = VK_NULL_HANDLE; }
        if (PostProcessPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(Device, PostProcessPipelineLayout, nullptr); PostProcessPipelineLayout = VK_NULL_HANDLE; }
        if (PostDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(Device, PostDescriptorPool, nullptr); PostDescriptorPool = VK_NULL_HANDLE; }
        if (PostProcessSampler != VK_NULL_HANDLE) { vkDestroySampler(Device, PostProcessSampler, nullptr); PostProcessSampler = VK_NULL_HANDLE; }
        if (PostDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(Device, PostDescriptorSetLayout, nullptr); PostDescriptorSetLayout = VK_NULL_HANDLE; }
        for (auto& target : SceneTargets) {
            if (target.framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(Device, target.framebuffer, nullptr);
            if (target.lightmapInfoFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(Device, target.lightmapInfoFramebuffer, nullptr);
            if (target.lightmapCompositeFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(Device, target.lightmapCompositeFramebuffer, nullptr);
            if (target.color.view != VK_NULL_HANDLE) vkDestroyImageView(Device, target.color.view, nullptr);
            if (target.color.image != VK_NULL_HANDLE) vkDestroyImage(Device, target.color.image, nullptr);
            if (target.color.memory != VK_NULL_HANDLE) vkFreeMemory(Device, target.color.memory, nullptr);
            if (target.worldPosition.view != VK_NULL_HANDLE) vkDestroyImageView(Device, target.worldPosition.view, nullptr);
            if (target.worldPosition.image != VK_NULL_HANDLE) vkDestroyImage(Device, target.worldPosition.image, nullptr);
            if (target.worldPosition.memory != VK_NULL_HANDLE) vkFreeMemory(Device, target.worldPosition.memory, nullptr);
            if (target.skyColor.view != VK_NULL_HANDLE) vkDestroyImageView(Device, target.skyColor.view, nullptr);
            if (target.skyColor.image != VK_NULL_HANDLE) vkDestroyImage(Device, target.skyColor.image, nullptr);
            if (target.skyColor.memory != VK_NULL_HANDLE) vkFreeMemory(Device, target.skyColor.memory, nullptr);
            if (target.depth.view != VK_NULL_HANDLE) vkDestroyImageView(Device, target.depth.view, nullptr);
            if (target.depth.image != VK_NULL_HANDLE) vkDestroyImage(Device, target.depth.image, nullptr);
            if (target.depth.memory != VK_NULL_HANDLE) vkFreeMemory(Device, target.depth.memory, nullptr);
            if (target.lightmapInfo.view != VK_NULL_HANDLE) vkDestroyImageView(Device, target.lightmapInfo.view, nullptr);
            if (target.lightmapInfo.image != VK_NULL_HANDLE) vkDestroyImage(Device, target.lightmapInfo.image, nullptr);
            if (target.lightmapInfo.memory != VK_NULL_HANDLE) vkFreeMemory(Device, target.lightmapInfo.memory, nullptr);
            if (target.lightmapCompositeColor.view != VK_NULL_HANDLE) vkDestroyImageView(Device, target.lightmapCompositeColor.view, nullptr);
            if (target.lightmapCompositeColor.image != VK_NULL_HANDLE) vkDestroyImage(Device, target.lightmapCompositeColor.image, nullptr);
            if (target.lightmapCompositeColor.memory != VK_NULL_HANDLE) vkFreeMemory(Device, target.lightmapCompositeColor.memory, nullptr);
            target.sceneInputDescriptorSet = VK_NULL_HANDLE;
            target.postDescriptorSet = VK_NULL_HANDLE;
        }
        SceneTargets.clear();
        if (PipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(Device, PipelineLayout, nullptr); PipelineLayout = VK_NULL_HANDLE; }
        if (PresentRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(Device, PresentRenderPass, nullptr); PresentRenderPass = VK_NULL_HANDLE; }
        if (LightmapCompositeRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(Device, LightmapCompositeRenderPass, nullptr); LightmapCompositeRenderPass = VK_NULL_HANDLE; }
        if (LightmapInfoRenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(Device, LightmapInfoRenderPass, nullptr); LightmapInfoRenderPass = VK_NULL_HANDLE; }
        if (RenderPass != VK_NULL_HANDLE) { vkDestroyRenderPass(Device, RenderPass, nullptr); RenderPass = VK_NULL_HANDLE; }
        DestroyDepthResources();
        for (VkImageView imageView : SwapchainImageViews)if (imageView != VK_NULL_HANDLE) vkDestroyImageView(Device, imageView, nullptr);
        SwapchainImageViews.clear();
        if (Swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(Device, Swapchain, nullptr); Swapchain = VK_NULL_HANDLE; }
        SwapchainImages.clear();
        ImagesInFlight.clear();
    }

    Vulkan::VulkanInitInfo Vulkan::RecreateSwapchain() {
        if (!GameWindow || Device == VK_NULL_HANDLE)return { false, "Vulkan Swapchain FAILED to Create", __LINE__ };
        if (GameWindow->FramebufferWidth() == 0 || GameWindow->FramebufferHeight() == 0)return { true, "Vulkan Swapchain Created", 0 };
        if (vkDeviceWaitIdle(Device) != VK_SUCCESS)return { false, "Vulkan Device Driver Error", __LINE__ };
        CleanupSwapchain();
        const auto support = QuerySwapchainSupport(PhysicalDevice);
        if (support.formats.empty() || support.presentModes.empty())return { false, "Vulkan Swapchain FAILED to Create", __LINE__ };
        VkSurfaceFormatKHR surfaceFormat = support.formats[0];
        for (const auto& format : support.formats)if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { surfaceFormat = format; break; }
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        const auto supportsMode = [&support](VkPresentModeKHR wanted) { return std::find(support.presentModes.begin(), support.presentModes.end(), wanted) != support.presentModes.end(); };
        if (m_VSync)presentMode = VK_PRESENT_MODE_FIFO_KHR;
        else {
            if (supportsMode(VK_PRESENT_MODE_MAILBOX_KHR))presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if (supportsMode(VK_PRESENT_MODE_IMMEDIATE_KHR))presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            else presentMode = VK_PRESENT_MODE_FIFO_KHR;
        }
        const char* presentModeName = presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" : (presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO");
        LOG_CATEGORY_INFO("Vulkan", "Using {} presentation mode (VSync {})", presentModeName, m_VSync ? "on" : "off");
        VkExtent2D extent = support.capabilities.currentExtent;
        if (support.capabilities.currentExtent.width == (std::numeric_limits<std::uint32_t>::max)())
            extent = { std::clamp(GameWindow->FramebufferWidth(),support.capabilities.minImageExtent.width,support.capabilities.maxImageExtent.width),std::clamp(GameWindow->FramebufferHeight(),support.capabilities.minImageExtent.height,support.capabilities.maxImageExtent.height) };
        if (extent.width == 0 || extent.height == 0)
            return { false, "Vulkan Swapchain FAILED to Create", __LINE__ };
        std::uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
            imageCount = support.capabilities.maxImageCount;
        const auto indices = FindQueueFamilies(PhysicalDevice);
        const std::uint32_t queueFamilies[] = { indices.graphics, indices.present };
        VkSwapchainCreateInfoKHR swapchainCreateInfo{};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.surface = Surface;
        swapchainCreateInfo.minImageCount = imageCount;
        swapchainCreateInfo.imageFormat = surfaceFormat.format;
        swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainCreateInfo.imageExtent = extent;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (indices.graphics != indices.present) {
            swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainCreateInfo.queueFamilyIndexCount = 2;
            swapchainCreateInfo.pQueueFamilyIndices = queueFamilies;
        }
        else swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCreateInfo.preTransform = support.capabilities.currentTransform;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainCreateInfo.presentMode = presentMode;
        swapchainCreateInfo.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(Device, &swapchainCreateInfo, nullptr, &Swapchain) != VK_SUCCESS)
            return { false, "Vulkan Swapchain FAILED to Create", __LINE__ };
        vkGetSwapchainImagesKHR(Device, Swapchain, &imageCount, nullptr);
        SwapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(Device, Swapchain, &imageCount, SwapchainImages.data());
        SwapchainFormat = surfaceFormat.format;
        SwapchainExtent = extent;
        SwapchainImageViews.resize(SwapchainImages.size());
        for (std::size_t i = 0; i < SwapchainImages.size(); ++i) {
            VkImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.image = SwapchainImages[i];
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = SwapchainFormat;
            imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            imageViewCreateInfo.subresourceRange.levelCount = 1;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            imageViewCreateInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(Device, &imageViewCreateInfo, nullptr, &SwapchainImageViews[i]) != VK_SUCCESS)
                return { false, "Vulkan Image Views FAILED to Create", __LINE__ };
        }
        DepthFormat = VK_FORMAT_UNDEFINED;
        for (VkFormat format : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(PhysicalDevice, format, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) { DepthFormat = format; break; }
        }
        if (DepthFormat == VK_FORMAT_UNDEFINED)
            return { false, "Vulkan Depth Resources FAILED to Create", __LINE__ };
        VkFormatProperties worldPositionProperties{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, WorldPositionFormat, &worldPositionProperties);
        const VkFormatFeatureFlags worldPositionFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((worldPositionProperties.optimalTilingFeatures & worldPositionFeatures) != worldPositionFeatures) {
            LOG_CATEGORY_ERROR("Vulkan", "World-position render target format is unsupported");
            return { false, "Vulkan World Position Target FAILED to Create", __LINE__ };
        }
        constexpr VkFormatFeatureFlags LightmapInfoFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        VkFormatProperties lightmapInfoProperties{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, VK_FORMAT_R16G16B16A16_UNORM, &lightmapInfoProperties);
        if ((lightmapInfoProperties.optimalTilingFeatures & LightmapInfoFeatures) == LightmapInfoFeatures)
            LightmapInfoFormat = VK_FORMAT_R16G16B16A16_UNORM;
        else {
            vkGetPhysicalDeviceFormatProperties(PhysicalDevice, VK_FORMAT_R32G32B32A32_SFLOAT, &lightmapInfoProperties);
            if ((lightmapInfoProperties.optimalTilingFeatures & LightmapInfoFeatures) == LightmapInfoFeatures) {
                LightmapInfoFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
                LOG_CATEGORY_WARN("Vulkan", "RGBA16 UNORM metadata is unsupported; using RGBA32F for precise lightmap UVs");
            }
            else {
                LightmapInfoFormat = WorldPositionFormat;
                LOG_CATEGORY_WARN("Vulkan", "High-precision lightmap metadata formats are unsupported; falling back to the world-position format");
            }
        }

        // PASS 1: SCENE COLOR, WORLD POSITION, SKY COLOR, AND DEPTH.
        VkAttachmentDescription sceneAttachments[4]{};
        sceneAttachments[0].format = SceneColorFormat;
        sceneAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        sceneAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sceneAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sceneAttachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        sceneAttachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        sceneAttachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sceneAttachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneAttachments[1] = sceneAttachments[0];
        sceneAttachments[1].format = WorldPositionFormat;
        sceneAttachments[2] = sceneAttachments[0];
        sceneAttachments[2].format = SceneColorFormat;
        sceneAttachments[3].format = DepthFormat;
        sceneAttachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        sceneAttachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sceneAttachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sceneAttachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        sceneAttachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        sceneAttachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sceneAttachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference sceneColorRefs[3] = { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
        VkAttachmentReference sceneDepthRef{ 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sceneSubpass{};
        sceneSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sceneSubpass.colorAttachmentCount = 3;
        sceneSubpass.pColorAttachments = sceneColorRefs;
        sceneSubpass.pDepthStencilAttachment = &sceneDepthRef;
        VkSubpassDependency sceneDependencies[2]{};
        sceneDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        sceneDependencies[0].dstSubpass = 0;
        sceneDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        sceneDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        sceneDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sceneDependencies[1].srcSubpass = 0;
        sceneDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        sceneDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        sceneDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        sceneDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sceneDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        VkRenderPassCreateInfo scenePassInfo{};
        scenePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        scenePassInfo.attachmentCount = 4;
        scenePassInfo.pAttachments = sceneAttachments;
        scenePassInfo.subpassCount = 1;
        scenePassInfo.pSubpasses = &sceneSubpass;
        scenePassInfo.dependencyCount = 2;
        scenePassInfo.pDependencies = sceneDependencies;
        if (vkCreateRenderPass(Device, &scenePassInfo, nullptr, &RenderPass) != VK_SUCCESS)
            return { false, "Vulkan Scene Render Pass FAILED to Create", __LINE__ };
        VkAttachmentDescription infoAttachments[3]{};
        infoAttachments[0].format = LightmapInfoFormat;
        infoAttachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        infoAttachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        infoAttachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        infoAttachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        infoAttachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        infoAttachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        infoAttachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infoAttachments[1] = sceneAttachments[1];
        infoAttachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        infoAttachments[1].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infoAttachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        infoAttachments[2].format = DepthFormat;
        infoAttachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        infoAttachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        infoAttachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        infoAttachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        infoAttachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        infoAttachments[2].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        infoAttachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference infoColorRefs[2] = { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
        VkAttachmentReference infoDepthRef{ 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription infoSubpass{};
        infoSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        infoSubpass.colorAttachmentCount = 2;
        infoSubpass.pColorAttachments = infoColorRefs;
        infoSubpass.pDepthStencilAttachment = &infoDepthRef;
        VkSubpassDependency infoDependencies[2]{};
        infoDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        infoDependencies[0].dstSubpass = 0;
        infoDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        infoDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        infoDependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        infoDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        infoDependencies[1].srcSubpass = 0;
        infoDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        infoDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        infoDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        infoDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        infoDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo infoPassInfo{};
        infoPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        infoPassInfo.attachmentCount = 3;
        infoPassInfo.pAttachments = infoAttachments;
        infoPassInfo.subpassCount = 1;
        infoPassInfo.pSubpasses = &infoSubpass;
        infoPassInfo.dependencyCount = 2;
        infoPassInfo.pDependencies = infoDependencies;
        if (vkCreateRenderPass(Device, &infoPassInfo, nullptr, &LightmapInfoRenderPass) != VK_SUCCESS)
            return { false, "Vulkan Lightmap Metadata Render Pass FAILED to Create", __LINE__ };
        VkAttachmentDescription compositeAttachment{};
        compositeAttachment.format = SceneColorFormat;
        compositeAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        compositeAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        compositeAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        compositeAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        compositeAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        compositeAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        compositeAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference compositeColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription compositeSubpass{};
        compositeSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        compositeSubpass.colorAttachmentCount = 1;
        compositeSubpass.pColorAttachments = &compositeColorRef;
        VkSubpassDependency compositeDependencies[2]{};
        compositeDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        compositeDependencies[0].dstSubpass = 0;
        compositeDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        compositeDependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        compositeDependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        compositeDependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        compositeDependencies[1].srcSubpass = 0;
        compositeDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        compositeDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        compositeDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        compositeDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        compositeDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo compositePassInfo{};
        compositePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        compositePassInfo.attachmentCount = 1;
        compositePassInfo.pAttachments = &compositeAttachment;
        compositePassInfo.subpassCount = 1;
        compositePassInfo.pSubpasses = &compositeSubpass;
        compositePassInfo.dependencyCount = 2;
        compositePassInfo.pDependencies = compositeDependencies;
        if (vkCreateRenderPass(Device, &compositePassInfo, nullptr, &LightmapCompositeRenderPass) != VK_SUCCESS)
            return { false, "Vulkan Lightmap Composite Render Pass FAILED to Create", __LINE__ };

        // PASS 3: POST-PROCESSING & IMGUI
        VkAttachmentDescription presentAttachment{};
        presentAttachment.format = SwapchainFormat;
        presentAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        presentAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        presentAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        presentAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        presentAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        presentAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        presentAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference presentColorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription presentSubpass{};
        presentSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        presentSubpass.colorAttachmentCount = 1;
        presentSubpass.pColorAttachments = &presentColorRef;
        VkSubpassDependency presentDependency{};
        presentDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        presentDependency.dstSubpass = 0;
        presentDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        presentDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        presentDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo presentPassInfo{};
        presentPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        presentPassInfo.attachmentCount = 1;
        presentPassInfo.pAttachments = &presentAttachment;
        presentPassInfo.subpassCount = 1;
        presentPassInfo.pSubpasses = &presentSubpass;
        presentPassInfo.dependencyCount = 1;
        presentPassInfo.pDependencies = &presentDependency;
        if (vkCreateRenderPass(Device, &presentPassInfo, nullptr, &PresentRenderPass) != VK_SUCCESS)
            return { false, "Vulkan Present Render Pass FAILED to Create", __LINE__ };

        // STABILIZE MATERIAL DESCRIPTORS
        if (MaterialDescriptorSetLayout == VK_NULL_HANDLE) {
            VkDescriptorSetLayoutBinding materialBindings[2]{};
            materialBindings[0].binding = 0;
            materialBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            materialBindings[0].descriptorCount = 1;
            materialBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            materialBindings[1].binding = 1;
            materialBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            materialBindings[1].descriptorCount = MaxMaterialTextures;
            materialBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
            materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            materialLayoutInfo.bindingCount = 2;
            materialLayoutInfo.pBindings = materialBindings;
            if (vkCreateDescriptorSetLayout(Device, &materialLayoutInfo, nullptr, &MaterialDescriptorSetLayout) != VK_SUCCESS)
                return { false, "Vulkan Material Descriptor Set Layout FAILED to Create", __LINE__ };
        }
        if (LightmapArrayDescriptorSetLayout == VK_NULL_HANDLE) {
            VkDescriptorSetLayoutBinding lightmapArrayBinding{};
            lightmapArrayBinding.binding = 0;
            lightmapArrayBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            lightmapArrayBinding.descriptorCount = 1;
            lightmapArrayBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo lightmapArrayLayoutInfo{};
            lightmapArrayLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            lightmapArrayLayoutInfo.bindingCount = 1;
            lightmapArrayLayoutInfo.pBindings = &lightmapArrayBinding;
            if (vkCreateDescriptorSetLayout(Device, &lightmapArrayLayoutInfo, nullptr, &LightmapArrayDescriptorSetLayout) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Array Descriptor Set Layout FAILED to Create", __LINE__ };
        }
        if (LightmapDescriptorPool == VK_NULL_HANDLE) {
            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            if (vkCreateDescriptorPool(Device, &poolInfo, nullptr, &LightmapDescriptorPool) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Array Descriptor Pool FAILED to Create", __LINE__ };
        }

        if (RealtimeLightingDescriptorSetLayout == VK_NULL_HANDLE) {
            VkDescriptorSetLayoutBinding realtimeLightingBinding{};
            realtimeLightingBinding.binding = 0;
            realtimeLightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            realtimeLightingBinding.descriptorCount = 1;
            realtimeLightingBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo realtimeLayoutInfo{};
            realtimeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            realtimeLayoutInfo.bindingCount = 1;
            realtimeLayoutInfo.pBindings = &realtimeLightingBinding;
            if (vkCreateDescriptorSetLayout(Device, &realtimeLayoutInfo, nullptr, &RealtimeLightingDescriptorSetLayout) != VK_SUCCESS)
                return { false, "Vulkan Realtime Lighting Descriptor Layout FAILED to Create", __LINE__ };
        }
        if (RealtimeLightingDescriptorPool == VK_NULL_HANDLE) {
            VkDescriptorPoolSize realtimePoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<std::uint32_t>(m_frames.size()) };
            VkDescriptorPoolCreateInfo realtimePoolInfo{};
            realtimePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            realtimePoolInfo.maxSets = static_cast<std::uint32_t>(m_frames.size());
            realtimePoolInfo.poolSizeCount = 1;
            realtimePoolInfo.pPoolSizes = &realtimePoolSize;
            if (vkCreateDescriptorPool(Device, &realtimePoolInfo, nullptr, &RealtimeLightingDescriptorPool) != VK_SUCCESS)
                return { false, "Vulkan Realtime Lighting Descriptor Pool FAILED to Create", __LINE__ };
        }

        VkPushConstantRange pushRanges[2]{};
        pushRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRanges[0].offset = 0;
        pushRanges[0].size = static_cast<std::uint32_t>(sizeof(RenderPushConstants));
        pushRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRanges[1].offset = 0;
        pushRanges[1].size = static_cast<std::uint32_t>(sizeof(RenderPushConstants));
        VkPipelineLayoutCreateInfo materialPipelineLayoutInfo{};
        materialPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        const VkDescriptorSetLayout materialLayouts[] = { MaterialDescriptorSetLayout, LightmapArrayDescriptorSetLayout };
        materialPipelineLayoutInfo.setLayoutCount = 2;
        materialPipelineLayoutInfo.pSetLayouts = materialLayouts;
        materialPipelineLayoutInfo.pushConstantRangeCount = 2;
        materialPipelineLayoutInfo.pPushConstantRanges = pushRanges;
        if (vkCreatePipelineLayout(Device, &materialPipelineLayoutInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
            return { false, "Vulkan Material Pipeline Layout FAILED to Create", __LINE__ };

        // CREATE SEPARATE RENDER TARGET DEPTH BUFFER
        SceneTargets.resize(SwapchainImages.size());
        for (SceneTarget& target : SceneTargets) {
            if (!CreateImageResource(SceneColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, target.color) ||
                !CreateImageResource(WorldPositionFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, target.worldPosition) ||
                !CreateImageResource(SceneColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, target.skyColor) ||
                !CreateImageResource(LightmapInfoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, target.lightmapInfo) ||
                !CreateImageResource(SceneColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, target.lightmapCompositeColor) ||
                !CreateImageResource(DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, target.depth))
                return { false, "Vulkan Scene Attachments FAILED to Create", __LINE__ };
            VkImageView views[] = { target.color.view, target.worldPosition.view, target.skyColor.view, target.depth.view };
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = RenderPass;
            framebufferInfo.attachmentCount = 4;
            framebufferInfo.pAttachments = views;
            framebufferInfo.width = SwapchainExtent.width;
            framebufferInfo.height = SwapchainExtent.height;
            framebufferInfo.layers = 1;
            if (vkCreateFramebuffer(Device, &framebufferInfo, nullptr, &target.framebuffer) != VK_SUCCESS)
                return { false, "Vulkan Scene Framebuffer FAILED to Create", __LINE__ };
            VkImageView infoViews[] = { target.lightmapInfo.view, target.worldPosition.view, target.depth.view };
            VkFramebufferCreateInfo infoFramebufferInfo{};
            infoFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            infoFramebufferInfo.renderPass = LightmapInfoRenderPass;
            infoFramebufferInfo.attachmentCount = 3;
            infoFramebufferInfo.pAttachments = infoViews;
            infoFramebufferInfo.width = SwapchainExtent.width;
            infoFramebufferInfo.height = SwapchainExtent.height;
            infoFramebufferInfo.layers = 1;
            if (vkCreateFramebuffer(Device, &infoFramebufferInfo, nullptr, &target.lightmapInfoFramebuffer) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Metadata Framebuffer FAILED to Create", __LINE__ };
            VkImageView compositeViews[] = { target.lightmapCompositeColor.view };
            VkFramebufferCreateInfo compositeFramebufferInfo{};
            compositeFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            compositeFramebufferInfo.renderPass = LightmapCompositeRenderPass;
            compositeFramebufferInfo.attachmentCount = 1;
            compositeFramebufferInfo.pAttachments = compositeViews;
            compositeFramebufferInfo.width = SwapchainExtent.width;
            compositeFramebufferInfo.height = SwapchainExtent.height;
            compositeFramebufferInfo.layers = 1;
            if (vkCreateFramebuffer(Device, &compositeFramebufferInfo, nullptr, &target.lightmapCompositeFramebuffer) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Composite Framebuffer FAILED to Create", __LINE__ };
        }

        // CREATE LIGHTMAP INFO PIPELINES
        {
            for (VkPipeline& pipeline : LightmapInfoPipelines) {
                if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            const auto vertCode = FileParsers::ReadShaderFile(FileParsers::BaseVertexShaderPath);
            const auto fragCode = FileParsers::ReadShaderFile("Engine/Shaders/lightmap_info.frag.spv");
            if (vertCode.empty() || fragCode.empty())
                return { false, "Vulkan Lightmap Metadata Pipeline FAILED to Create", __LINE__ };
            VkShaderModuleCreateInfo shaderInfo{};
            shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderInfo.codeSize = vertCode.size();
            shaderInfo.pCode = reinterpret_cast<const std::uint32_t*>(vertCode.data());
            VkShaderModule vertShader = VK_NULL_HANDLE;
            if (vkCreateShaderModule(Device, &shaderInfo, nullptr, &vertShader) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Metadata Pipeline FAILED to Create", __LINE__ };
            shaderInfo.codeSize = fragCode.size();
            shaderInfo.pCode = reinterpret_cast<const std::uint32_t*>(fragCode.data());
            VkShaderModule fragShader = VK_NULL_HANDLE;
            if (vkCreateShaderModule(Device, &shaderInfo, nullptr, &fragShader) != VK_SUCCESS) {
                vkDestroyShaderModule(Device, vertShader, nullptr);
                return { false, "Vulkan Lightmap Metadata Pipeline FAILED to Create", __LINE__ };
            }
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertShader;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragShader;
            stages[1].pName = "main";
            VkVertexInputBindingDescription bindings[2]{};
            bindings[0] = { 0, static_cast<std::uint32_t>(sizeof(MeshVertex)), VK_VERTEX_INPUT_RATE_VERTEX };
            bindings[1] = { 1, static_cast<std::uint32_t>(sizeof(InstanceData)), VK_VERTEX_INPUT_RATE_INSTANCE };
            VkVertexInputAttributeDescription attributes[8]{};
            attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, position)) };
            attributes[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, uv)) };
            attributes[2] = { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, position)) };
            attributes[3] = { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, size)) };
            attributes[4] = { 4, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, rotation)) };
            attributes[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, lightmapScaleOffset)) };
            attributes[6] = { 6, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, lightmapUV)) };
            attributes[7] = { 7, 1, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, lightmapPage)) };
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 2;
            vertexInput.pVertexBindingDescriptions = bindings;
            vertexInput.vertexAttributeDescriptionCount = 8;
            vertexInput.pVertexAttributeDescriptions = attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{};
            viewport.width = static_cast<float>(SwapchainExtent.width);
            viewport.height = static_cast<float>(SwapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.extent = SwapchainExtent;
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;
            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = true;
            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_FALSE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blends[2]{};
            for (auto& blend : blends)
                blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blending{};
            blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blending.attachmentCount = 2;
            blending.pAttachments = blends;
            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = stages;
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &assembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &blending;
            pipelineInfo.layout = PipelineLayout;
            pipelineInfo.renderPass = LightmapInfoRenderPass;
            pipelineInfo.subpass = 0;
            constexpr std::array<VkCullModeFlags, 4> infoCullModes = {
                VK_CULL_MODE_NONE,
                VK_CULL_MODE_FRONT_BIT,
                VK_CULL_MODE_BACK_BIT,
                VK_CULL_MODE_FRONT_AND_BACK
            };
            for (std::size_t i = 0; i < infoCullModes.size(); ++i) {
                rasterizer.cullMode = infoCullModes[i];
                const VkResult result = vkCreateGraphicsPipelines(
                    Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &LightmapInfoPipelines[i]);
                if (result != VK_SUCCESS) {
                    for (VkPipeline& pipeline : LightmapInfoPipelines) {
                        if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, pipeline, nullptr);
                        pipeline = VK_NULL_HANDLE;
                    }
                    vkDestroyShaderModule(Device, fragShader, nullptr);
                    vkDestroyShaderModule(Device, vertShader, nullptr);
                    LOG_CATEGORY_ERROR("Vulkan", "Could not create lightmap metadata pipeline for cull mode {} ({})",
                        static_cast<std::uint32_t>(infoCullModes[i]), VkResultToString(result));
                    return { false, "Vulkan Lightmap Metadata Pipeline FAILED to Create", __LINE__ };
                }
            }
            vkDestroyShaderModule(Device, fragShader, nullptr);
            vkDestroyShaderModule(Device, vertShader, nullptr);
            SwapchainFramebuffers.resize(SwapchainImageViews.size());
            for (std::size_t i = 0; i < SwapchainImageViews.size(); ++i) {
                VkImageView views[] = { SwapchainImageViews[i] };
                VkFramebufferCreateInfo framebufferInfo{};
                framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass = PresentRenderPass;
                framebufferInfo.attachmentCount = 1;
                framebufferInfo.pAttachments = views;
                framebufferInfo.width = SwapchainExtent.width;
                framebufferInfo.height = SwapchainExtent.height;
                framebufferInfo.layers = 1;
                if (vkCreateFramebuffer(Device, &framebufferInfo, nullptr, &SwapchainFramebuffers[i]) != VK_SUCCESS)
                    return { false, "Vulkan Present Framebuffer FAILED to Create", __LINE__ };
            }
        }

        // CREATE GRAPHICS PIPELINE
        {
            MaterialPipelineConfig pipelineConfig{};
            GraphicsPipeline = CreateMaterialPipeline(FileParsers::BaseFragmentShaderPath, pipelineConfig);
            if (GraphicsPipeline == VK_NULL_HANDLE)
                return { false, "Vulkan Graphics Pipeline FAILED to Create", __LINE__ };
            for (const auto& [materialId, material] : Materials) {
                (void)materialId;
                if (material.fragmentShaderSpv != FileParsers::BaseFragmentShaderPath && ShaderPipelines.find(material.fragmentShaderSpv) == ShaderPipelines.end() && CreateMaterialPipeline(material.fragmentShaderSpv, material.pipelineConfig) == VK_NULL_HANDLE)
                    LOG_CATEGORY_ERROR("Vulkan", "Could not recreate material pipeline for '{}'", material.fragmentShaderSpv);
            }
        }

        // CREATE POST PROCESS DESCRIPTOR RESOURCES
        {
            if (SceneTargets.empty())
                return { false, "Vulkan Post-Processing Descriptor Resources FAILED to Create", __LINE__ };
            VkDescriptorSetLayoutBinding setLayoutBindings[4]{};
            setLayoutBindings[0].binding = 0;
            setLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            setLayoutBindings[0].descriptorCount = 1;
            setLayoutBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            setLayoutBindings[1] = setLayoutBindings[0];
            setLayoutBindings[1].binding = 1;
            setLayoutBindings[2] = setLayoutBindings[0];
            setLayoutBindings[2].binding = 2;
            setLayoutBindings[3] = setLayoutBindings[0];
            setLayoutBindings[3].binding = 3;
            VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
            setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            setLayoutInfo.bindingCount = 4;
            setLayoutInfo.pBindings = setLayoutBindings;
            if (vkCreateDescriptorSetLayout(Device, &setLayoutInfo, nullptr, &PostDescriptorSetLayout) != VK_SUCCESS)
                return { false, "Vulkan Post-Processing Descriptor Resources FAILED to Create", __LINE__ };
            VkSamplerCreateInfo samplerCreateInfo{};
            samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
            samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
            samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerCreateInfo.maxLod = 0.0f;
            if (vkCreateSampler(Device, &samplerCreateInfo, nullptr, &PostProcessSampler) != VK_SUCCESS)
                return { false, "Vulkan Post-Processing Descriptor Resources FAILED to Create", __LINE__ };
            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<std::uint32_t>(SceneTargets.size() * 8) };
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.maxSets = static_cast<std::uint32_t>(SceneTargets.size() * 2u);
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            if (vkCreateDescriptorPool(Device, &poolInfo, nullptr, &PostDescriptorPool) != VK_SUCCESS)
                return { false, "Vulkan Post-Processing Descriptor Resources FAILED to Create", __LINE__ };
            auto allocateSceneDescriptor = [&](SceneTarget& target, VkDescriptorSet& descriptorSet, VkImageView colorView) -> bool {
                VkDescriptorSetAllocateInfo allocate{};
                allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocate.descriptorPool = PostDescriptorPool;
                allocate.descriptorSetCount = 1;
                allocate.pSetLayouts = &PostDescriptorSetLayout;
                if (vkAllocateDescriptorSets(Device, &allocate, &descriptorSet) != VK_SUCCESS) return false;
                VkDescriptorImageInfo imageInfos[4]{};
                imageInfos[0].sampler = PostProcessSampler;
                imageInfos[0].imageView = colorView;
                imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos[1].sampler = PostProcessSampler;
                imageInfos[1].imageView = target.worldPosition.view;
                imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos[2].sampler = PostProcessSampler;
                imageInfos[2].imageView = target.skyColor.view;
                imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos[3].sampler = PostProcessSampler;
                imageInfos[3].imageView = target.lightmapInfo.view;
                imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet writes[4]{};
                for (std::uint32_t binding = 0; binding < 4; ++binding) {
                    writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[binding].dstSet = descriptorSet;
                    writes[binding].dstBinding = binding;
                    writes[binding].descriptorCount = 1;
                    writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[binding].pImageInfo = &imageInfos[binding];
                }
                vkUpdateDescriptorSets(Device, 4, writes, 0, nullptr);
                return true;
                };
            for (auto& target : SceneTargets) {
                if (!allocateSceneDescriptor(target, target.sceneInputDescriptorSet, target.color.view) ||
                    !allocateSceneDescriptor(target, target.postDescriptorSet, target.lightmapCompositeColor.view))
                    return { false, "Vulkan Post-Processing Descriptor Resources FAILED to Create", __LINE__ };
            }
            if (LightmapRuntimeDescriptorSetLayout == VK_NULL_HANDLE) {
                VkDescriptorSetLayoutBinding runtimeBinding{};
                runtimeBinding.binding = 0;
                runtimeBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                runtimeBinding.descriptorCount = 1;
                runtimeBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                VkDescriptorSetLayoutCreateInfo runtimeLayoutInfo{};
                runtimeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                runtimeLayoutInfo.bindingCount = 1;
                runtimeLayoutInfo.pBindings = &runtimeBinding;
                if (vkCreateDescriptorSetLayout(Device, &runtimeLayoutInfo, nullptr, &LightmapRuntimeDescriptorSetLayout) != VK_SUCCESS)
                    return { false, "Vulkan Lightmap Runtime Descriptor Layout FAILED to Create", __LINE__ };
            }
        }

        // CREATE POST-PROCESS PIPELINE
        {
            if (PostDescriptorSetLayout == VK_NULL_HANDLE || PresentRenderPass == VK_NULL_HANDLE || LightmapRuntimeDescriptorSetLayout == VK_NULL_HANDLE || RealtimeLightingDescriptorSetLayout == VK_NULL_HANDLE)
                return { false, "Vulkan Post-Processing Pipeline FAILED to Create", __LINE__ };
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            pushRange.offset = 0;
            pushRange.size = sizeof(PostProcessPushConstants);
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            const VkDescriptorSetLayout postLayouts[] = { PostDescriptorSetLayout, LightmapArrayDescriptorSetLayout, LightmapRuntimeDescriptorSetLayout, RealtimeLightingDescriptorSetLayout };
            layoutInfo.setLayoutCount = 4u;
            layoutInfo.pSetLayouts = postLayouts;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
            if (vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PostProcessPipelineLayout) != VK_SUCCESS)
                return { false, "Vulkan Post-Processing Pipeline FAILED to Create", __LINE__ };
            const auto vertCode = FileParsers::ReadShaderFile(FileParsers::PostVertexShaderPath);
            const auto fragCode = FileParsers::ReadShaderFile(FileParsers::PostFragmentShaderPath);
            if (vertCode.empty() || fragCode.empty())
                return { false, "Vulkan Post-Processing Pipeline FAILED to Create", __LINE__ };
            VkShaderModuleCreateInfo vertInfo{};
            vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vertInfo.codeSize = vertCode.size();
            vertInfo.pCode = reinterpret_cast<const std::uint32_t*>(vertCode.data());
            VkShaderModule vert = VK_NULL_HANDLE;
            if (vkCreateShaderModule(Device, &vertInfo, nullptr, &vert) != VK_SUCCESS)
                return { false, "Vulkan Post-Processing Pipeline FAILED to Create", __LINE__ };
            VkShaderModuleCreateInfo fragInfo{};
            fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            fragInfo.codeSize = fragCode.size();
            fragInfo.pCode = reinterpret_cast<const std::uint32_t*>(fragCode.data());
            VkShaderModule frag = VK_NULL_HANDLE;
            if (vkCreateShaderModule(Device, &fragInfo, nullptr, &frag) != VK_SUCCESS) {
                vkDestroyShaderModule(Device, vert, nullptr);
                return { false, "Vulkan Post-Processing Pipeline FAILED to Create", __LINE__ };
            }
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vert;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = frag;
            stages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{};
            viewport.width = static_cast<float>(SwapchainExtent.width);
            viewport.height = static_cast<float>(SwapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.extent = SwapchainExtent;
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;
            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blending{};
            blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blending.attachmentCount = 1;
            blending.pAttachments = &blend;
            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = stages;
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &assembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &blending;
            pipelineInfo.layout = PostProcessPipelineLayout;
            pipelineInfo.renderPass = PresentRenderPass;
            pipelineInfo.subpass = 0;
            const VkResult result = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &PostProcessPipeline);
            vkDestroyShaderModule(Device, frag, nullptr);
            vkDestroyShaderModule(Device, vert, nullptr);
            if (result != VK_SUCCESS)
                return { false, "Vulkan Post-Processing Pipeline FAILED to Create", __LINE__ };
        }

        // CREATE LIGHTMAP COMPOSITE PIPELINE
        {
            if (LightmapCompositePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(Device, LightmapCompositePipeline, nullptr);
                LightmapCompositePipeline = VK_NULL_HANDLE;
            }
            const auto vertCode = FileParsers::ReadShaderFile(FileParsers::PostVertexShaderPath);
            const auto fragCode = FileParsers::ReadShaderFile("Engine/Shaders/lightmap_composite.frag.spv");
            if (vertCode.empty() || fragCode.empty())
                return { false, "Vulkan Lightmap Composite Pipeline FAILED to Create", __LINE__ };
            VkShaderModuleCreateInfo shaderInfo{};
            shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderInfo.codeSize = vertCode.size();
            shaderInfo.pCode = reinterpret_cast<const std::uint32_t*>(vertCode.data());
            VkShaderModule vertShader = VK_NULL_HANDLE;
            if (vkCreateShaderModule(Device, &shaderInfo, nullptr, &vertShader) != VK_SUCCESS)
                return { false, "Vulkan Lightmap Composite Pipeline FAILED to Create", __LINE__ };
            shaderInfo.codeSize = fragCode.size();
            shaderInfo.pCode = reinterpret_cast<const std::uint32_t*>(fragCode.data());
            VkShaderModule fragShader = VK_NULL_HANDLE;
            if (vkCreateShaderModule(Device, &shaderInfo, nullptr, &fragShader) != VK_SUCCESS) {
                vkDestroyShaderModule(Device, vertShader, nullptr);
                return { false, "Vulkan Lightmap Composite Pipeline FAILED to Create", __LINE__ };
            }
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vertShader;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragShader;
            stages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{};
            viewport.width = static_cast<float>(SwapchainExtent.width);
            viewport.height = static_cast<float>(SwapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            VkRect2D scissor{};
            scissor.extent = SwapchainExtent;
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;
            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blending{};
            blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blending.attachmentCount = 1;
            blending.pAttachments = &blend;
            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = stages;
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &assembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &blending;
            pipelineInfo.layout = PostProcessPipelineLayout;
            pipelineInfo.renderPass = LightmapCompositeRenderPass;
            pipelineInfo.subpass = 0;
            const VkResult result = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &LightmapCompositePipeline);
            vkDestroyShaderModule(Device, fragShader, nullptr);
            vkDestroyShaderModule(Device, vertShader, nullptr);
            if (result != VK_SUCCESS) {
                LightmapCompositePipeline = VK_NULL_HANDLE;
                LOG_CATEGORY_ERROR("Vulkan", "Could not create the post-material lightmap composite pipeline ({})", VkResultToString(result));
                return { false, "Vulkan Lightmap Composite Pipeline FAILED to Create", __LINE__ };
            }
        }
        CommandBuffers.resize(SwapchainFramebuffers.size());
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = CommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = static_cast<std::uint32_t>(CommandBuffers.size());
        if (vkAllocateCommandBuffers(Device, &allocateInfo, CommandBuffers.data()) != VK_SUCCESS)
            return { false, "Vulkan Command Buffers FAILED to Create", __LINE__ };
        ImagesInFlight.resize(SwapchainImages.size(), VK_NULL_HANDLE);
        return { true, "Vulkan Swapchain Creation SUCCESS", 0 };
    }

    Vulkan::QueueFamilyIndices Vulkan::FindQueueFamilies(VkPhysicalDevice device) const {
        QueueFamilyIndices indices;
        std::uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& family = families[i];
            if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) indices.graphics = i;
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, Surface, &presentSupported);
            if (presentSupported == VK_TRUE)indices.present = i;
            if (indices.Complete())break;
        }
        return indices;
    }

    Vulkan::SwapchainSupport Vulkan::QuerySwapchainSupport(VkPhysicalDevice device) const {
        SwapchainSupport details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, Surface, &details.capabilities);
        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, Surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, Surface, &formatCount, details.formats.data());
        }
        std::uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, Surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, Surface, &presentModeCount, details.presentModes.data());
        }
        return details;
    }

    std::uint32_t Vulkan::FindMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memoryProperties);
        for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if ((typeFilter & (1u << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        return UINT32_MAX;
    }

    bool Vulkan::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, GpuBuffer& outBuffer) {
        outBuffer = {};
        if (Device == VK_NULL_HANDLE || size == 0)return false;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &bufferInfo, nullptr, &outBuffer.buffer) != VK_SUCCESS) { outBuffer = {}; return false; }
        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(Device, outBuffer.buffer, &memoryRequirements);
        const std::uint32_t memoryType = FindMemoryType(memoryRequirements.memoryTypeBits, properties);
        if (memoryType == UINT32_MAX) { DestroyBuffer(outBuffer); return false; }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(Device, &allocateInfo, nullptr, &outBuffer.memory) != VK_SUCCESS) { DestroyBuffer(outBuffer); return false; }
        if (vkBindBufferMemory(Device, outBuffer.buffer, outBuffer.memory, 0) != VK_SUCCESS) { DestroyBuffer(outBuffer); return false; }
        return true;
    }

    void Vulkan::DestroyBuffer(GpuBuffer& buffer) {
        if (Device != VK_NULL_HANDLE) {
            if (buffer.buffer != VK_NULL_HANDLE)vkDestroyBuffer(Device, buffer.buffer, nullptr);
            if (buffer.memory != VK_NULL_HANDLE)vkFreeMemory(Device, buffer.memory, nullptr);
        }
        buffer = {};
    }

    bool Vulkan::CopyBuffer(VkBuffer source, VkBuffer destination, VkDeviceSize size) {
        if (Device == VK_NULL_HANDLE || CommandPool == VK_NULL_HANDLE)return false;
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = CommandPool;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(Device, &allocInfo, &commandBuffer) != VK_SUCCESS)return false;
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) { vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer); return false; }
        if (commandBuffer == VK_NULL_HANDLE)return false;
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &copyRegion);
        if (commandBuffer == VK_NULL_HANDLE)return false;
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) { vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer); return false; }
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        const VkResult submitResult = vkQueueSubmit(GraphicsQ, 1, &submitInfo, VK_NULL_HANDLE);
        if (submitResult != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not submit one-time command buffer: {}", VkResultToString(submitResult));
            vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
            return false;
        }
        const VkResult waitResult = vkQueueWaitIdle(GraphicsQ);
        vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
        if (waitResult != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not wait for one-time command buffer: {}", VkResultToString(waitResult));
            return false;
        }
        return true;
    }

    void Vulkan::DestroyMeshGpuResource(GpuMesh& mesh) {
        DestroyBuffer(mesh.vertexBuffer);
        DestroyBuffer(mesh.indexBuffer);
        mesh = {};
    }

    VkPipeline Vulkan::CreateMaterialPipeline(const std::string& fragmentShaderPath, MaterialPipelineConfig config) {
        if (Device == VK_NULL_HANDLE || RenderPass == VK_NULL_HANDLE || PipelineLayout == VK_NULL_HANDLE)return VK_NULL_HANDLE;
        std::string pipelineKey = fragmentShaderPath;
        pipelineKey += "_cull" + std::to_string(config.cullMode);
        pipelineKey += "_poly" + std::to_string(static_cast<int>(config.polygonMode));
        pipelineKey += "_bias" + std::to_string(config.enableDepthBias);
        pipelineKey += "_width" + std::to_string(config.lineWidth);
        if (config.isSky) {
            pipelineKey = (config.flatSky ? "__sky_flat__:" : "__sky_surface__:") + pipelineKey;
        }
        const auto cached = ShaderPipelines.find(pipelineKey);
        if (cached != ShaderPipelines.end())return cached->second;
        const char* vertexShaderPath = config.isSky ? FileParsers::SkyVertexShaderPath : FileParsers::BaseVertexShaderPath;
        const auto vertCode = FileParsers::ReadShaderFile(vertexShaderPath);
        const auto fragCode = FileParsers::ReadShaderFile(fragmentShaderPath.c_str());
        if (vertCode.empty() || fragCode.empty())return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo vertInfo{};
        vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertInfo.codeSize = vertCode.size();
        vertInfo.pCode = reinterpret_cast<const std::uint32_t*>(vertCode.data());
        VkShaderModule vertShader = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &vertInfo, nullptr, &vertShader) != VK_SUCCESS)return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo fragInfo{};
        fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragInfo.codeSize = fragCode.size();
        fragInfo.pCode = reinterpret_cast<const std::uint32_t*>(fragCode.data());
        VkShaderModule fragShader = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &fragInfo, nullptr, &fragShader) != VK_SUCCESS) { vkDestroyShaderModule(Device, vertShader, nullptr); return VK_NULL_HANDLE; }
        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";
        VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };
        VkVertexInputBindingDescription meshBinding{};
        meshBinding.binding = 0;
        meshBinding.stride = sizeof(MeshVertex);
        meshBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputBindingDescription instanceBinding{};
        instanceBinding.binding = 1;
        instanceBinding.stride = sizeof(InstanceData);
        instanceBinding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        std::array<VkVertexInputBindingDescription, 2> bindings = { meshBinding, instanceBinding };
        std::array<VkVertexInputAttributeDescription, 8> attributes{};
        attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, position)) };
        attributes[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, uv)) };
        attributes[2] = { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, position)) };
        attributes[3] = { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, size)) };
        attributes[4] = { 4, 1, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, rotation)) };
        attributes[5] = { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, lightmapScaleOffset)) };
        attributes[6] = { 6, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<std::uint32_t>(offsetof(MeshVertex, lightmapUV)) };
        attributes[7] = { 7, 1, VK_FORMAT_R32_SFLOAT, static_cast<std::uint32_t>(offsetof(InstanceData, lightmapPage)) };
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (!config.flatSky) {
            vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
            vertexInput.pVertexBindingDescriptions = bindings.data();
            vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
            vertexInput.pVertexAttributeDescriptions = attributes.data();
        }
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{};
        viewport.width = static_cast<float>(SwapchainExtent.width);
        viewport.height = static_cast<float>(SwapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{};
        scissor.extent = SwapchainExtent;
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = config.polygonMode;
        rasterizer.lineWidth = config.lineWidth;
        rasterizer.cullMode = config.cullMode;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = config.enableDepthBias;
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = config.isSky ? VK_FALSE : VK_TRUE;
        depthStencil.depthWriteEnable = config.isSky ? VK_FALSE : VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
        std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachments{};
        for (auto& attachment : blendAttachments)
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachments[config.isSky ? 1 : 2].colorWriteMask = 0;
        VkPipelineColorBlendStateCreateInfo blending{};
        blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blending.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
        blending.pAttachments = blendAttachments.data();
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blending;
        pipelineInfo.layout = PipelineLayout;
        pipelineInfo.renderPass = RenderPass;
        pipelineInfo.subpass = 0;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        vkDestroyShaderModule(Device, fragShader, nullptr);
        vkDestroyShaderModule(Device, vertShader, nullptr);
        if (result != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not create graphics pipeline for shader '{}' ({})", fragmentShaderPath, VkResultToString(result));
            return VK_NULL_HANDLE;
        }
        ShaderPipelines.emplace(pipelineKey, pipeline);
        return pipeline;
    }

    bool Vulkan::CreateImageResource(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, GpuImage& outImage) {
        outImage = {};
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { SwapchainExtent.width, SwapchainExtent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(Device, &imageInfo, nullptr, &outImage.image) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(Device, outImage.image, &requirements);
        const auto memoryType = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == UINT32_MAX) { DestroyImageResource(outImage); return false; }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(Device, &allocation, nullptr, &outImage.memory) != VK_SUCCESS || vkBindImageMemory(Device, outImage.image, outImage.memory, 0) != VK_SUCCESS) { DestroyImageResource(outImage); return false; }
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outImage.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(Device, &viewInfo, nullptr, &outImage.view) != VK_SUCCESS) { DestroyImageResource(outImage); return false; }
        return true;
    }

    void Vulkan::DestroyImageResource(GpuImage& image) {
        if (Device != VK_NULL_HANDLE) {
            if (image.view != VK_NULL_HANDLE) vkDestroyImageView(Device, image.view, nullptr);
            if (image.image != VK_NULL_HANDLE) vkDestroyImage(Device, image.image, nullptr);
            if (image.memory != VK_NULL_HANDLE) vkFreeMemory(Device, image.memory, nullptr);
        }
        image = {};
    }

    void Vulkan::DestroyTexture(GpuTexture& texture) {
        if (Device != VK_NULL_HANDLE) {
            if (texture.sampler != VK_NULL_HANDLE) vkDestroySampler(Device, texture.sampler, nullptr);
            if (texture.view != VK_NULL_HANDLE) vkDestroyImageView(Device, texture.view, nullptr);
            if (texture.image != VK_NULL_HANDLE) vkDestroyImage(Device, texture.image, nullptr);
            if (texture.memory != VK_NULL_HANDLE) vkFreeMemory(Device, texture.memory, nullptr);
        }
        texture = {};
    }

    bool Vulkan::CreateTextureFromPixels(const unsigned char* rgba, std::uint32_t width, std::uint32_t height, GpuTexture& outTexture) {
        outTexture = {};
        if (Device == VK_NULL_HANDLE || !rgba || width == 0 || height == 0 || CommandPool == VK_NULL_HANDLE)return false;
        const VkDeviceSize imageBytes = static_cast<VkDeviceSize>(width) * height * 4;
        GpuBuffer staging{};
        if (!CreateBuffer(imageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging))return false;
        void* mapped = nullptr;
        if (vkMapMemory(Device, staging.memory, 0, imageBytes, 0, &mapped) != VK_SUCCESS) { DestroyBuffer(staging); return false; }
        std::memcpy(mapped, rgba, static_cast<std::size_t>(imageBytes));
        vkUnmapMemory(Device, staging.memory);
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(Device, &imageInfo, nullptr, &outTexture.image) != VK_SUCCESS) { DestroyBuffer(staging); return false; }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(Device, outTexture.image, &requirements);
        const auto memoryType = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == UINT32_MAX) { DestroyBuffer(staging); DestroyTexture(outTexture); return false; }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(Device, &alloc, nullptr, &outTexture.memory) != VK_SUCCESS || vkBindImageMemory(Device, outTexture.image, outTexture.memory, 0) != VK_SUCCESS) { DestroyBuffer(staging); DestroyTexture(outTexture); return false; }
        VkCommandBufferAllocateInfo commandAlloc{};
        commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandAlloc.commandPool = CommandPool;
        commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandAlloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(Device, &commandAlloc, &cmd) != VK_SUCCESS) { DestroyBuffer(staging); DestroyTexture(outTexture); return false; }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) { vkFreeCommandBuffers(Device, CommandPool, 1, &cmd); DestroyBuffer(staging); DestroyTexture(outTexture); return false; }
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = outTexture.image;
        toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, outTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = outTexture.image;
        toShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) { vkFreeCommandBuffers(Device, CommandPool, 1, &cmd); DestroyBuffer(staging); DestroyTexture(outTexture); return false; }
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        const VkResult submitted = vkQueueSubmit(GraphicsQ, 1, &submit, VK_NULL_HANDLE);
        const VkResult waited = submitted == VK_SUCCESS ? vkQueueWaitIdle(GraphicsQ) : submitted;
        vkFreeCommandBuffers(Device, CommandPool, 1, &cmd);
        DestroyBuffer(staging);
        if (waited != VK_SUCCESS) { DestroyTexture(outTexture); return false; }
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outTexture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(Device, &viewInfo, nullptr, &outTexture.view) != VK_SUCCESS) { DestroyTexture(outTexture); return false; }
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        if (vkCreateSampler(Device, &samplerInfo, nullptr, &outTexture.sampler) != VK_SUCCESS) {
            vkDestroyImageView(Device, outTexture.view, nullptr);
            outTexture.view = VK_NULL_HANDLE;
            DestroyTexture(outTexture);
            return false;
        }
        outTexture.width = width;
        outTexture.height = height;
        return true;
    }

    bool Vulkan::CreateTextureArrayFromGeneratedPixels(std::uint32_t width, std::uint32_t height, std::uint32_t layers, std::uint64_t maxAllocationBytes, const std::function<void(std::uint32_t, unsigned char*)>& generateLayer, GpuTexture& outTexture) {
        outTexture = {};
        if (Device == VK_NULL_HANDLE || CommandPool == VK_NULL_HANDLE || width == 0 || height == 0 || layers == 0 || !generateLayer)return false;
        const std::uint64_t layerBytes64 = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
        if (layerBytes64 == 0 || layerBytes64 > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))return false;
        const VkDeviceSize layerBytes = static_cast<VkDeviceSize>(layerBytes64);
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(PhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
        const VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures) {
            LOG_CATEGORY_ERROR("Lighting", "RGBA8 UNORM sampled texture arrays are not supported by this GPU");
            return false;
        }
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = layers;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(Device, &imageInfo, nullptr, &outTexture.image) != VK_SUCCESS) { outTexture = {}; return false; }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(Device, outTexture.image, &requirements);
        if (requirements.size > maxAllocationBytes) {
            LOG_CATEGORY_ERROR("Lighting", "Driver-reported atlas allocation ({} MiB) exceeds the configured budget ({} MiB)", requirements.size / (1024ull * 1024ull), maxAllocationBytes / (1024ull * 1024ull));
            DestroyTexture(outTexture);
            return false;
        }
        const std::uint32_t memoryType = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == UINT32_MAX) { DestroyTexture(outTexture); return false; }
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(Device, &allocation, nullptr, &outTexture.memory) != VK_SUCCESS || vkBindImageMemory(Device, outTexture.image, outTexture.memory, 0) != VK_SUCCESS) { DestroyTexture(outTexture); return false; }
        for (std::uint32_t layer = 0; layer < layers; ++layer) {
            std::vector<unsigned char> pixels;
            try { pixels.resize(static_cast<std::size_t>(layerBytes)); }
            catch (const std::bad_alloc&) {
                LOG_CATEGORY_ERROR("Lighting", "Could not allocate the {} MiB temporary CPU buffer for atlas layer {}", layerBytes / (1024ull * 1024ull), layer);
                DestroyTexture(outTexture);
                return false;
            }
            generateLayer(layer, pixels.data());
            GpuBuffer staging{};
            if (!CreateBuffer(layerBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging)) {
                DestroyTexture(outTexture);
                return false;
            }
            void* mapped = nullptr;
            if (vkMapMemory(Device, staging.memory, 0, layerBytes, 0, &mapped) != VK_SUCCESS) { DestroyBuffer(staging); DestroyTexture(outTexture); return false; }
            std::memcpy(mapped, pixels.data(), static_cast<std::size_t>(layerBytes));
            vkUnmapMemory(Device, staging.memory);
            pixels.clear();
            pixels.shrink_to_fit();
            VkCommandBufferAllocateInfo commandAllocation{};
            commandAllocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandAllocation.commandPool = CommandPool;
            commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandAllocation.commandBufferCount = 1;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(Device, &commandAllocation, &commandBuffer) != VK_SUCCESS) {
                DestroyBuffer(staging); DestroyTexture(outTexture); return false;
            }
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) {
                vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
                DestroyBuffer(staging);
                DestroyTexture(outTexture);
                return false;
            }
            if (layer == 0) {
                VkImageMemoryBarrier toTransfer{};
                toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toTransfer.image = outTexture.image;
                toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
                toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
            }
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = layer;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = { width, height, 1 };
            vkCmdCopyBufferToImage(commandBuffer, staging.buffer, outTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            if (layer + 1u == layers) {
                VkImageMemoryBarrier toShader{};
                toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShader.image = outTexture.image;
                toShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
                toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
            }
            const VkResult ended = vkEndCommandBuffer(commandBuffer);
            VkResult submitted = ended;
            if (ended == VK_SUCCESS) {
                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &commandBuffer;
                submitted = vkQueueSubmit(GraphicsQ, 1, &submit, VK_NULL_HANDLE);
                if (submitted == VK_SUCCESS)submitted = vkQueueWaitIdle(GraphicsQ);
            }
            vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
            DestroyBuffer(staging);
            if (submitted != VK_SUCCESS) {
                LOG_CATEGORY_ERROR("Lighting", "Failed to upload lightmap array layer {}: {}", layer, VkResultToString(submitted));
                DestroyTexture(outTexture);
                return false;
            }
        }
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = outTexture.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers };
        if (vkCreateImageView(Device, &viewInfo, nullptr, &outTexture.view) != VK_SUCCESS) { DestroyTexture(outTexture); return false; }
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        if (vkCreateSampler(Device, &samplerInfo, nullptr, &outTexture.sampler) != VK_SUCCESS) { DestroyTexture(outTexture); return false; }
        outTexture.width = width;
        outTexture.height = height;
        outTexture.layers = layers;
        return true;
    }

    Vulkan::TextureHandle Vulkan::LoadTexture(std::string_view filename) {
        if (!IsInitialized() || filename.empty()) return 0;
        const std::string key = FileParsers::NormalizeAssetKey(filename);
        const auto found = FileTextures.find(key);
        if (found != FileTextures.end()) {
            if (found->second == 0) return found->second;
            auto it = Textures.find(found->second);
            if (it == Textures.end() || it->second.permanent) return found->second;
            if (it->second.referenceCount != UINT32_MAX) ++it->second.referenceCount;
            return found->second;
        }
        FileParsers::DecodedTexture image{};
        std::string imageError;
        if (!FileParsers::LoadTextureImage(key, image, imageError)) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not load texture '{}': {}", key, imageError);
            return 0;
        }
        GpuTexture texture{};
        const bool uploaded = CreateTextureFromPixels(image.rgba.data(), image.width, image.height, texture);
        if (!uploaded) { LOG_CATEGORY_ERROR("Vulkan", "Could not upload texture '{}'", key); return 0; }
        const TextureHandle handle = NextTextureHandle++;
        texture.referenceCount = 1;
        Textures.emplace(handle, std::move(texture));
        FileTextures.emplace(key, handle);
        return handle;
    }

    void Vulkan::ReleaseTextureReference(TextureHandle texture) {
        if (texture == 0) return;
        auto it = Textures.find(texture);
        if (it == Textures.end() || it->second.permanent) return;
        if (it->second.referenceCount > 0) --it->second.referenceCount;
        if (it->second.referenceCount != 0) return;
        for (auto fileIt = FileTextures.begin(); fileIt != FileTextures.end();) {
            if (fileIt->second == texture) fileIt = FileTextures.erase(fileIt);
            else ++fileIt;
        }
        DestroyTexture(it->second);
        Textures.erase(it);
    }

    void Vulkan::ReleaseTexture(TextureHandle texture) {
        if (Device != VK_NULL_HANDLE && vkDeviceWaitIdle(Device) != VK_SUCCESS) return;
        ReleaseTextureReference(texture);
    }

    void Vulkan::UpdateMaterialDescriptorSet(MaterialResource& material, std::uint32_t frameIndex) {
        auto& frame = material.frames[frameIndex];
        if (frame.descriptorSet == VK_NULL_HANDLE) return;
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = frame.uniformBuffer.buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialGpuData);
        std::array<VkDescriptorImageInfo, MaxMaterialTextures> imageInfos{};
        for (std::uint32_t i = 0; i < MaxMaterialTextures; ++i) {
            TextureHandle textureHandle = material.textures[i] ? material.textures[i] : 0;
            auto textureIt = Textures.find(textureHandle);
            if (textureIt == Textures.end()) textureIt = Textures.find(0);
            if (textureIt == Textures.end()) continue;
            imageInfos[i].sampler = textureIt->second.sampler;
            imageInfos[i].imageView = textureIt->second.view;
            imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frame.descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frame.descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = MaxMaterialTextures;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = imageInfos.data();
        vkUpdateDescriptorSets(Device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void Vulkan::DestroyMaterialResource(MaterialResource& material) {
        for (auto& frame : material.frames) {
            if (MaterialDescriptorPool != VK_NULL_HANDLE && frame.descriptorSet != VK_NULL_HANDLE)vkFreeDescriptorSets(Device, MaterialDescriptorPool, 1, &frame.descriptorSet);
            if (Device != VK_NULL_HANDLE && frame.mapped && frame.uniformBuffer.memory != VK_NULL_HANDLE)vkUnmapMemory(Device, frame.uniformBuffer.memory);
            frame.mapped = nullptr;
            DestroyBuffer(frame.uniformBuffer);
            frame.descriptorSet = VK_NULL_HANDLE;
        }
        for (TextureHandle texture : material.textures) ReleaseTextureReference(texture);
        material.textures.fill(0);
    }

    Vulkan::MaterialHandle Vulkan::CreateMaterial(const MaterialDesc& desc) {
        if (!IsInitialized() || MaterialDescriptorPool == VK_NULL_HANDLE || MaterialDescriptorSetLayout == VK_NULL_HANDLE)return 0;
        MaterialResource material{};
        material.name = desc.name.empty() ? "Material" : desc.name;
        material.lit = desc.lit;
        material.fragmentShaderSpv = desc.fragmentShaderSpv.empty() ? FileParsers::BaseFragmentShaderPath : desc.fragmentShaderSpv;
        material.gpuData.color = desc.color;
        material.gpuData.matUV = desc.matUV;
        material.gpuData.colorMapUV = desc.colorMapUV;
        material.parameterSlots = desc.parameterSlots;
        material.textureSlots = desc.textureSlots;
        MaterialPipelineConfig pipelineConfig{};
        pipelineConfig.cullMode = desc.twoSided ? VK_CULL_MODE_NONE : desc.cullMode;
        pipelineConfig.polygonMode = desc.polygonMode;
        pipelineConfig.enableDepthBias = desc.enableDepthBias;
        pipelineConfig.lineWidth = desc.lineWidth;
        material.pipelineConfig = pipelineConfig;
        material.textures.fill(0);
        if (material.textureSlots.find("colorMap") == material.textureSlots.end()) material.textureSlots["colorMap"] = 0;
        if (!desc.colorMap.empty())material.textures[0] = LoadTexture(desc.colorMap);
        for (const auto& [name, path] : desc.textures) {
            auto slotIt = material.textureSlots.find(name);
            if (slotIt == material.textureSlots.end()) {
                std::uint32_t slot = 0;
                for (const auto& [usedName, usedSlot] : material.textureSlots) { (void)usedName; slot = (std::max)(slot, usedSlot + 1); }
                slotIt = material.textureSlots.emplace(name, slot).first;
            }
            if (slotIt->second >= MaxMaterialTextures) {
                LOG_CATEGORY_ERROR("Vulkan", "Texture '{}' in material '{}' has slot {} (max {})", name, material.name, slotIt->second, MaxMaterialTextures - 1);
                continue;
            }
            if (!path.empty()) {
                const TextureHandle replacement = LoadTexture(path);
                if (replacement != 0) { ReleaseTextureReference(material.textures[slotIt->second]); material.textures[slotIt->second] = replacement; }
            }
        }
        std::uint32_t nextSlot = 0;
        auto acquireSlot = [&](const std::string& name) -> std::uint32_t {
            auto found = material.parameterSlots.find(name);
            if (found != material.parameterSlots.end()) return found->second;
            while (nextSlot < MaxMaterialParameters) {
                bool occupied = false;
                for (const auto& [otherName, otherSlot] : material.parameterSlots) { (void)otherName; if (otherSlot == nextSlot) { occupied = true; break; } }
                if (!occupied) break;
                ++nextSlot;
            }
            const auto slot = nextSlot++;
            material.parameterSlots[name] = slot;
            return slot;
            };
        for (const auto& [name, value] : desc.vec4Parameters) {
            const auto slot = acquireSlot(name);
            if (slot < MaxMaterialParameters) material.gpuData.params[slot] = value;
        }
        for (const auto& [name, value] : desc.floatParameters) {
            const auto slot = acquireSlot(name);
            if (slot < MaxMaterialParameters) material.gpuData.params[slot][0] = value;
        } {
            constexpr const char* faceNames[] = { "skyPosX", "skyNegX", "skyPosY", "skyNegY", "skyPosZ", "skyNegZ" };
            std::uint32_t faceMask = 0;
            for (std::uint32_t face = 0; face < 6; ++face) {
                const auto textureIt = desc.textures.find(faceNames[face]);
                if (textureIt != desc.textures.end() && !textureIt->second.empty())
                    faceMask |= (1u << face);
            }
            material.gpuData.params[MaxMaterialParameters - 1][3] = static_cast<float>(faceMask);
        }
        for (std::uint32_t slot = 0; slot < MaxMaterialTextures; ++slot)material.gpuData.textureFlags[slot / 4][slot % 4] = material.textures[slot] == 0 ? 1.0f : 0.0f;
        if (CreateMaterialPipeline(material.fragmentShaderSpv, material.pipelineConfig) == VK_NULL_HANDLE) { for (TextureHandle texture : material.textures) ReleaseTextureReference(texture); return 0; }
        const MaterialHandle handle = NextMaterialHandle++;
        auto inserted = Materials.emplace(handle, std::move(material));
        MaterialResource& stored = inserted.first->second;
        for (std::uint32_t frame = 0; frame < MaxFramesInFlight; ++frame) {
            auto& frameIndex = stored.frames[frame];
            if (frameIndex.uniformBuffer.buffer == VK_NULL_HANDLE) {
                if (!CreateBuffer(sizeof(MaterialGpuData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frameIndex.uniformBuffer)) {
                    LOG_CATEGORY_ERROR("Vulkan", "Could not create descriptor resources for material '{}'", stored.name);
                    DestroyMaterialResource(stored);
                    Materials.erase(inserted.first);
                    return 0;
                }
                if (vkMapMemory(Device, frameIndex.uniformBuffer.memory, 0, sizeof(MaterialGpuData), 0, &frameIndex.mapped) != VK_SUCCESS) {
                    DestroyBuffer(frameIndex.uniformBuffer); frameIndex.mapped = nullptr;
                    LOG_CATEGORY_ERROR("Vulkan", "Could not create descriptor resources for material '{}'", stored.name);
                    DestroyMaterialResource(stored);
                    Materials.erase(inserted.first);
                    return 0;
                }
                std::memcpy(frameIndex.mapped, &stored.gpuData, sizeof(MaterialGpuData));
            }
            if (frameIndex.descriptorSet == VK_NULL_HANDLE) {
                VkDescriptorSetAllocateInfo alloc{};
                alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                alloc.descriptorPool = MaterialDescriptorPool;
                alloc.descriptorSetCount = 1;
                alloc.pSetLayouts = &MaterialDescriptorSetLayout;
                if (vkAllocateDescriptorSets(Device, &alloc, &frameIndex.descriptorSet) != VK_SUCCESS) {
                    LOG_CATEGORY_ERROR("Vulkan", "Could not create descriptor resources for material '{}'", stored.name);
                    DestroyMaterialResource(stored);
                    Materials.erase(inserted.first);
                    return 0;
                }
            }
            UpdateMaterialDescriptorSet(stored, frame);
        }
        return handle;
    }

    Vulkan::MaterialHandle Vulkan::CloneMaterial(MaterialHandle sourceHandle, const std::string& uniqueName) {
        const auto sourceIt = Materials.find(sourceHandle);
        if (sourceIt == Materials.end()) return 0;
        const MaterialResource& source = sourceIt->second;
        MaterialDesc desc{};
        desc.name = uniqueName;
        desc.fragmentShaderSpv = source.fragmentShaderSpv;
        desc.matUV = source.gpuData.matUV;
        desc.color = source.gpuData.color;
        desc.lit = source.lit;
        desc.colorMapUV = source.gpuData.colorMapUV;
        desc.parameterSlots = source.parameterSlots;
        desc.textureSlots = source.textureSlots;
        for (const auto& [name, slot] : source.parameterSlots)if (slot < MaxMaterialParameters) desc.vec4Parameters[name] = source.gpuData.params[slot];
        std::array<std::string, MaxMaterialTextures> paths{};
        for (std::uint32_t slot = 0; slot < MaxMaterialTextures; ++slot) {
            const TextureHandle texture = source.textures[slot];
            if (texture == 0) continue;
            for (const auto& [path, handle] : FileTextures)if (handle == texture) { paths[slot] = path; break; }
            if (paths[slot].empty()) {
                LOG_CATEGORY_WARN("Assets", "Could not clone material '{}' because texture slot {} has no reloadable asset path", source.name, slot);
                return 0;
            }
        }
        for (std::uint32_t slot = 0; slot < MaxMaterialTextures; ++slot) {
            if (paths[slot].empty()) continue;
            std::string slotName;
            for (const auto& [name, usedSlot] : desc.textureSlots)if (usedSlot == slot) { slotName = name; break; }
            if (slotName.empty()) {
                slotName = "__runtimeSlot" + std::to_string(slot);
                desc.textureSlots[slotName] = slot;
            }
            desc.textures[slotName] = paths[slot];
        }
        const MaterialGpuData sourceGpuData = source.gpuData;
        const MaterialHandle cloneHandle = CreateMaterial(desc);
        if (cloneHandle == 0) return 0;
        auto cloneIt = Materials.find(cloneHandle);
        if (cloneIt == Materials.end()) return 0;
        cloneIt->second.gpuData = sourceGpuData;
        for (std::uint32_t frame = 0; frame < MaxFramesInFlight; ++frame) {
            auto& resources = cloneIt->second.frames[frame];
            if (resources.mapped) std::memcpy(resources.mapped, &cloneIt->second.gpuData, sizeof(MaterialGpuData));
            UpdateMaterialDescriptorSet(cloneIt->second, frame);
        }
        return cloneHandle;
    }

    Vulkan::MaterialHandle Vulkan::LoadMaterialAsset(std::string_view filename) {
        if (!IsInitialized() || filename.empty()) return 0;
        const std::string key = FileParsers::NormalizeAssetKey(filename);
        const auto cached = FileMaterials.find(key);
        if (cached != FileMaterials.end()) return cached->second;
        MaterialDesc desc{};
        std::string parseError;
        if (!FileParsers::LoadMaterialAssetFile(key, desc, parseError)) {
            LOG_CATEGORY_ERROR("Vulkan", "{}", parseError);
            return 0;
        }
        const MaterialHandle handle = CreateMaterial(desc);
        if (handle != 0) FileMaterials.emplace(key, handle);
        return handle;
    }

    bool Vulkan::SetMaterialColor(MaterialHandle material, const std::array<float, 4>& color) {
        auto it = Materials.find(material);
        if (it == Materials.end() || material == 0 || material == ErrorMaterialHandle) return false;
        it->second.gpuData.color = color;
        return true;
    }

    bool Vulkan::SetMaterialFloat(MaterialHandle material, std::string_view parameter, float value) {
        auto it = Materials.find(material); if (it == Materials.end()) return false;
        auto slot = it->second.parameterSlots.find(std::string(parameter));
        if (slot == it->second.parameterSlots.end() || slot->second >= MaxMaterialParameters) return false;
        it->second.gpuData.params[slot->second][0] = value; return true;
    }

    bool Vulkan::SetMaterialVec4(MaterialHandle material, std::string_view parameter, const std::array<float, 4>& value) {
        auto it = Materials.find(material); if (it == Materials.end()) return false;
        auto slot = it->second.parameterSlots.find(std::string(parameter));
        if (slot == it->second.parameterSlots.end() || slot->second >= MaxMaterialParameters) return false;
        it->second.gpuData.params[slot->second] = value; return true;
    }

    bool Vulkan::SetMaterialTexture(MaterialHandle material, std::string_view textureName, std::string_view filename) {
        auto it = Materials.find(material);
        if (it == Materials.end())return false;
        auto slot = it->second.textureSlots.find(std::string(textureName));
        if (slot == it->second.textureSlots.end())return false;
        return SetMaterialTextureAtSlot(material, slot->second, filename);
    }

    bool Vulkan::SetMaterialTextureAtSlot(MaterialHandle material, std::uint32_t slot, std::string_view filename) {
        if (slot >= MaxMaterialTextures)return false;
        auto it = Materials.find(material);
        if (it == Materials.end())return false;
        const TextureHandle texture = filename.empty() ? 0 : LoadTexture(filename);
        if (!texture)return false;
        if (vkDeviceWaitIdle(Device) != VK_SUCCESS) { ReleaseTextureReference(texture); return false; }
        const TextureHandle previous = it->second.textures[slot];
        it->second.textures[slot] = texture;
        it->second.gpuData.textureFlags[slot / 4][slot % 4] = texture == 0 ? 1.0f : 0.0f;
        for (std::uint32_t frame = 0; frame < MaxFramesInFlight; ++frame)UpdateMaterialDescriptorSet(it->second, frame);
        ReleaseTextureReference(previous);
        return true;
    }

    bool Vulkan::DoesMaterialHaveTextures(MaterialHandle material) {
        const auto it = Materials.find(material);
        if (it == Materials.end())return false;
        for (TextureHandle texture : it->second.textures)if (texture != 0)return true;
        return false;
    }

    Vulkan::TextureHandle Vulkan::GetMaterialTextureAtSlot(MaterialHandle material, std::uint32_t slot) {
        if (slot >= MaxMaterialTextures)return 0;
        const auto it = Materials.find(material);
        if (it == Materials.end())return 0;
        return it->second.textures[slot];
    }

    std::uint32_t Vulkan::LoadMeshAsset(std::string_view filename) {
        if (!IsInitialized()) {
            LOG_CATEGORY_ERROR("Vulkan", "LoadMeshAsset called before Vulkan initialization");
            return 0;
        }
        if (filename.empty()) return 0;
        const std::string requestedKey = FileParsers::NormalizeAssetKey(filename);
        const auto existing = FileMeshes.find(requestedKey);
        if (existing != FileMeshes.end()) return existing->second;
        const std::string key = requestedKey;
        const std::string extension = FileParsers::GetFileExtension(key);
        LOG_CATEGORY_INFO("Vulkan", "Loading asset '{}' (type '{}')", key, extension);
        MeshData data{};
#if defined(NDEBUG) && !defined(_EDITOR)
        constexpr bool requireCookedMeshForObj = true;
#else
        constexpr bool requireCookedMeshForObj = false;
#endif
        const FileParsers::MeshFileLoadResult fileLoad = FileParsers::LoadMeshAssetFile(key, data, requireCookedMeshForObj);
        const std::string& cookedPath = fileLoad.cookedPath;
        if (fileLoad.loadedCooked)
            LOG_CATEGORY_INFO("Vulkan", "Using cooked mesh cache '{}'", cookedPath);
        if (!fileLoad.Succeeded()) {
            switch (fileLoad.status) {
            case FileParsers::MeshFileLoadStatus::InvalidCookedFile:
                LOG_CATEGORY_ERROR("Vulkan", "Cooked mesh '{}' is invalid or has an unsupported version", key);
                break;
            case FileParsers::MeshFileLoadStatus::ReleaseRequiresCookedMesh:
                LOG_CATEGORY_ERROR("Vulkan", "Release build requires a current cooked mesh '{}'. Rebuild it with Debug/Editor before packaging.", cookedPath);
                break;
            case FileParsers::MeshFileLoadStatus::UnsupportedFormat:
                LOG_CATEGORY_ERROR("Vulkan", "Unsupported mesh asset type '{}' for '{}'", extension, key);
                break;
            case FileParsers::MeshFileLoadStatus::SourceParseFailed:
                break;
            case FileParsers::MeshFileLoadStatus::Success:
                break;
            }
            return 0;
        }
        if (!data.hasLightmapUVs) {
            if (!GenerateCookedLightmapUVs(data)) {
                LOG_CATEGORY_ERROR("Lighting", "Could not generate lightmap UVs while cooking '{}'.", key);
                return 0;
            }
#if !defined(NDEBUG) || defined(_EDITOR)
            if (!FileParsers::SaveCookedMeshFile(cookedPath, data))
                LOG_CATEGORY_WARN("Lighting", "Mesh '{}' was loaded but its cooked .eemf cache could not be saved to '{}'.", key, cookedPath);
#endif
        }
        const std::uint32_t assetId = RegisterMesh(data);
        if (assetId == 0) {
            LOG_CATEGORY_ERROR("Vulkan", "Failed to create GPU asset for '{}'", filename);
            return 0;
        }
        std::unordered_map<std::string, MaterialHandle> materialHandles;
        auto meshIt = Meshes.find(assetId);
        if (meshIt != Meshes.end()) {
            GpuMesh& gpuMesh = meshIt->second;
            for (std::size_t subIdx = 0; subIdx < data.subMeshes.size() && subIdx < gpuMesh.subMeshes.size(); ++subIdx) {
                const SubMeshData& subMesh = data.subMeshes[subIdx];
                if (subMesh.materialName.empty()) continue;
                auto materialIt = materialHandles.find(subMesh.materialName);
                if (materialIt == materialHandles.end()) {
                    MaterialDesc desc{};
                    desc.name = key + "::" + subMesh.materialName;
                    desc.color = subMesh.defaultMaterialColor;
                    desc.colorMap = subMesh.defaultMaterialTexture;
                    const MaterialHandle materialHandle = CreateMaterial(desc);
                    if (materialHandle == 0) {
                        LOG_CATEGORY_WARN("Vulkan", "Could not create cooked default material '{}' from '{}'.", subMesh.materialName, key);
                        continue;
                    }
                    materialIt = materialHandles.emplace(subMesh.materialName, materialHandle).first;
                    gpuMesh.ownedMaterials.push_back(materialHandle);
                }
                gpuMesh.subMeshes[subIdx].defaultMaterial = materialIt->second;
            }
        }
        FileMeshes.emplace(key, assetId);
        std::size_t totalVertices = 0;
        std::size_t totalIndices = 0;
        for (const SubMeshData& subMesh : data.subMeshes) { totalVertices += subMesh.vertices.size(); totalIndices += subMesh.indices.size(); }
        LOG_CATEGORY_INFO("Vulkan", "Loaded mesh '{}': {} submeshes, {} vertices, {} indices", key, data.subMeshes.size(), totalVertices, totalIndices);
        LOG_CATEGORY_INFO("Vulkan", "Asset '{}' loaded successfully with ID {}", filename, assetId);
        return assetId;
    }

    std::uint32_t Vulkan::RegisterMeshInternal(const MeshData& data) {
        if (!data.IsValid() || Device == VK_NULL_HANDLE) {
            LOG_CATEGORY_ERROR("Vulkan", "Failed to upload mesh resource: invalid mesh data or device");
            return 0;
        }
        MeshData prepared = data;
        if (!data.hasLightmapUVs) {
#if defined(NDEBUG) && !defined(_EDITOR)
            LOG_CATEGORY_ERROR("Lighting", "Runtime mesh registration requires cooked UV1; xatlas must run in Debug/Editor, not during shipping gameplay");
            return 0;
#else
            if (!GenerateCookedLightmapUVs(prepared)) {
                LOG_CATEGORY_ERROR("Lighting", "Could not generate UV1 with xatlas for a mesh registered at runtime");
                return 0;
            }
#endif
        }
        if (!prepared.IsValid() || !prepared.hasLightmapUVs || Device == VK_NULL_HANDLE) {
            LOG_CATEGORY_ERROR("Vulkan", "Failed to upload mesh resource: mesh is invalid, UV1 is missing, or Vulkan device is unavailable");
            return 0;
        }
        std::size_t totalVertexCount = 0;
        std::size_t totalIndexCount = 0;
        for (const SubMeshData& subMesh : prepared.subMeshes) {
            if (subMesh.vertices.size() > (std::numeric_limits<std::uint32_t>::max)() - totalVertexCount || subMesh.indices.size() > (std::numeric_limits<std::uint32_t>::max)() - totalIndexCount) {
                LOG_CATEGORY_ERROR("Vulkan", "Mesh is too large for the current renderer index/count format");
                return 0;
            }
            totalVertexCount += subMesh.vertices.size();
            totalIndexCount += subMesh.indices.size();
        }
        if (totalVertexCount == 0 || totalIndexCount == 0) return 0;
        std::vector<MeshVertex> packedVertices;
        std::vector<std::uint32_t> packedIndices;
        packedVertices.reserve(totalVertexCount);
        packedIndices.reserve(totalIndexCount);
        GpuMesh gpuMesh{};
        gpuMesh.cpuSubMeshes = prepared.subMeshes;
        gpuMesh.subMeshes.reserve(prepared.subMeshes.size());
        for (const SubMeshData& subMesh : prepared.subMeshes) {
            if (!subMesh.IsValid()) {
                LOG_CATEGORY_ERROR("Vulkan", "Mesh contains an invalid submesh");
                return 0;
            }
            const std::uint32_t baseVertex = static_cast<std::uint32_t>(packedVertices.size());
            GpuSubMesh gpuSubMesh{};
            gpuSubMesh.indexOffset = static_cast<std::uint32_t>(packedIndices.size());
            gpuSubMesh.indexCount = static_cast<std::uint32_t>(subMesh.indices.size());
            packedVertices.insert(packedVertices.end(), subMesh.vertices.begin(), subMesh.vertices.end());
            for (const std::uint32_t index : subMesh.indices) {
                if (index >= subMesh.vertices.size() || index > (std::numeric_limits<std::uint32_t>::max)() - baseVertex) {
                    LOG_CATEGORY_ERROR("Vulkan", "Mesh contains an invalid submesh index");
                    return 0;
                }
                packedIndices.push_back(baseVertex + index);
            }
            gpuMesh.subMeshes.push_back(gpuSubMesh);
        }
        gpuMesh.indexCount = static_cast<std::uint32_t>(packedIndices.size());
        const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(packedVertices.size() * sizeof(MeshVertex));
        const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(packedIndices.size() * sizeof(std::uint32_t));
        GpuBuffer vertexStaging{};
        if (!CreateBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexStaging)) {
            LOG_CATEGORY_ERROR("Vulkan", "Failed to create vertex staging buffer");
            return 0;
        }
        void* vertexMapped = nullptr;
        if (vkMapMemory(Device, vertexStaging.memory, 0, vertexBytes, 0, &vertexMapped) != VK_SUCCESS) {
            DestroyBuffer(vertexStaging);
            LOG_CATEGORY_ERROR("Vulkan", "Failed to map vertex staging buffer");
            return 0;
        }
        std::memcpy(vertexMapped, packedVertices.data(), static_cast<std::size_t>(vertexBytes));
        vkUnmapMemory(Device, vertexStaging.memory);
        if (!CreateBuffer(vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gpuMesh.vertexBuffer) ||
            !CopyBuffer(vertexStaging.buffer, gpuMesh.vertexBuffer.buffer, vertexBytes)) {
            DestroyBuffer(vertexStaging);
            DestroyMeshGpuResource(gpuMesh);
            LOG_CATEGORY_ERROR("Vulkan", "Failed to upload vertex buffer");
            return 0;
        }
        DestroyBuffer(vertexStaging);
        GpuBuffer indexStaging{};
        if (!CreateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexStaging)) {
            DestroyMeshGpuResource(gpuMesh);
            LOG_CATEGORY_ERROR("Vulkan", "Failed to create index staging buffer");
            return 0;
        }
        void* indexMapped = nullptr;
        if (vkMapMemory(Device, indexStaging.memory, 0, indexBytes, 0, &indexMapped) != VK_SUCCESS) {
            DestroyBuffer(indexStaging);
            DestroyMeshGpuResource(gpuMesh);
            LOG_CATEGORY_ERROR("Vulkan", "Failed to map index staging buffer");
            return 0;
        }
        std::memcpy(indexMapped, packedIndices.data(), static_cast<std::size_t>(indexBytes));
        vkUnmapMemory(Device, indexStaging.memory);
        if (!CreateBuffer(indexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gpuMesh.indexBuffer) ||
            !CopyBuffer(indexStaging.buffer, gpuMesh.indexBuffer.buffer, indexBytes)) {
            DestroyBuffer(indexStaging);
            DestroyMeshGpuResource(gpuMesh);
            LOG_CATEGORY_ERROR("Vulkan", "Failed to upload index buffer");
            return 0;
        }
        DestroyBuffer(indexStaging);
        const std::uint32_t handle = NextMeshHandle++;
        Meshes.emplace(handle, std::move(gpuMesh));
        return handle;
    }

    std::uint32_t Vulkan::RegisterMesh(const MeshData& data) {
        if (!IsInitialized()) {
            LOG_CATEGORY_ERROR("Vulkan", "RegisterMesh called before Vulkan initialization");
            return 0;
        }
        if (!data.IsValid()) {
            LOG_CATEGORY_ERROR("Vulkan", "RegisterMesh called with invalid mesh data");
            return 0;
        }
        std::size_t totalVertices = 0;
        std::size_t totalIndices = 0;
        for (const SubMeshData& subMesh : data.subMeshes) {
            if (subMesh.vertices.size() > (std::numeric_limits<std::uint32_t>::max)() - totalVertices ||
                subMesh.indices.size() > (std::numeric_limits<std::uint32_t>::max)() - totalIndices) {
                LOG_CATEGORY_ERROR("Vulkan", "Mesh is too large for the current renderer index/count format");
                return 0;
            }
            totalVertices += subMesh.vertices.size();
            totalIndices += subMesh.indices.size();
        }
        return RegisterMeshInternal(data);
    }

    void Vulkan::UnregisterMesh(Vulkan::MeshHandle handle) {
        if (handle == 0 || handle == CubeMeshHandle || handle == PlaneMeshHandle || handle == SphereMeshHandle || handle == CylinderMeshHandle || handle == WedgeMeshHandle || handle == CornerWedgeMeshHandle || handle == ConeMeshHandle || handle == PyramidMeshHandle)
            return;
        auto it = Meshes.find(handle);
        if (it == Meshes.end()) return;
        for (const auto& [path, cachedHandle] : FileMeshes) {
            if (cachedHandle == handle) {
                const auto refs = MeshAssetRefCounts.find(path);
                if (refs != MeshAssetRefCounts.end() && refs->second != 0) {
                    LOG_CATEGORY_WARN("Assets", "Refusing to unregister mesh '{}' while {} level(s) reference it", path, refs->second);
                    return;
                }
            }
        }
        if (Device != VK_NULL_HANDLE && vkDeviceWaitIdle(Device) != VK_SUCCESS) return;
        const auto ownedMaterials = it->second.ownedMaterials;
        DestroyMeshGpuResource(it->second);
        Meshes.erase(it);
        for (auto fileIt = FileMeshes.begin(); fileIt != FileMeshes.end();) {
            if (fileIt->second == handle) fileIt = FileMeshes.erase(fileIt);
            else ++fileIt;
        }
        for (MaterialHandle materialHandle : ownedMaterials) {
            auto matIt = Materials.find(materialHandle);
            if (matIt == Materials.end()) continue;
            DestroyMaterialResource(matIt->second);
            Materials.erase(matIt);
            for (auto fileIt = FileMaterials.begin(); fileIt != FileMaterials.end();) {
                if (fileIt->second == materialHandle) fileIt = FileMaterials.erase(fileIt);
                else ++fileIt;
            }
        }
    }

    void Vulkan::DestroyUnreferencedMeshAsset(const std::string& path) {
        auto fileIt = FileMeshes.find(path);
        if (fileIt == FileMeshes.end()) { MeshAssetRefCounts.erase(path); return; }
        const MeshHandle handle = fileIt->second;
        auto meshIt = Meshes.find(handle);
        if (meshIt != Meshes.end()) {
            const auto ownedMaterials = meshIt->second.ownedMaterials;
            DestroyMeshGpuResource(meshIt->second);
            Meshes.erase(meshIt);
            for (MaterialHandle materialHandle : ownedMaterials) {
                auto materialIt = Materials.find(materialHandle);
                if (materialIt != Materials.end()) {
                    DestroyMaterialResource(materialIt->second);
                    Materials.erase(materialIt);
                }
            }
        }
        for (auto it = FileMeshes.begin(); it != FileMeshes.end();) { if (it->second == handle) it = FileMeshes.erase(it); else ++it; }
        MeshAssetRefCounts.erase(path);
    }

    void Vulkan::DestroyUnreferencedMaterialAsset(const std::string& path) {
        auto fileIt = FileMaterials.find(path);
        if (fileIt == FileMaterials.end()) { MaterialAssetRefCounts.erase(path); return; }
        const MaterialHandle handle = fileIt->second;
        auto materialIt = Materials.find(handle);
        if (materialIt != Materials.end()) { DestroyMaterialResource(materialIt->second); Materials.erase(materialIt); }
        for (auto it = FileMaterials.begin(); it != FileMaterials.end();) { if (it->second == handle) it = FileMaterials.erase(it); else ++it; }
        MaterialAssetRefCounts.erase(path);
    }

    void Vulkan::AssignPrimaryLevel(const std::shared_ptr<Level>& level) {
        PrimaryLevel = level;
        if (level && IsInitialized() && !AcquireLevelAssets(*level))
            LOG_CATEGORY_ERROR("Assets", "Could not acquire dependencies for assigned level '{}'", level->GetName());
    }

    bool Vulkan::AcquireLevelAssets(Level& level) {
        if (!IsInitialized()) {
            LOG_CATEGORY_ERROR("Vulkan", "AcquireLevelAssets('{}') called before Vulkan initialization", level.GetName());
            return false;
        }
        std::unordered_set<std::string> desiredMeshes;
        std::unordered_set<std::string> desiredMaterials;
        std::unordered_set<std::string> desiredTextures;
        for (const std::string& path : level.DeclaredMeshAssetPaths) if (!path.empty()) desiredMeshes.emplace(FileParsers::NormalizeAssetKey(path));
        for (const std::string& path : level.DeclaredMaterialAssetPaths) if (!path.empty()) desiredMaterials.emplace(FileParsers::NormalizeAssetKey(path));
        for (const std::string& path : level.DeclaredTextureAssetPaths) if (!path.empty()) desiredTextures.emplace(FileParsers::NormalizeAssetKey(path));
        for (const auto& object : level.Objects) {
            if (!object) continue;
            if (const auto* mesh = dynamic_cast<const Level::Mesh*>(object.get())) {
                if (mesh->GetMeshType() == Level::Mesh::Custom && !mesh->CustomMesh.empty())desiredMeshes.emplace(FileParsers::NormalizeAssetKey(mesh->CustomMesh));
                for (const std::string& materialPath : mesh->MaterialAssets)if (!materialPath.empty()) desiredMaterials.emplace(FileParsers::NormalizeAssetKey(materialPath));
            }
        }
        if (level.CurrentSky) {
            if (!level.CurrentSky->MaterialAsset.empty()) desiredMaterials.emplace(FileParsers::NormalizeAssetKey(level.CurrentSky->MaterialAsset));
            if (!level.CurrentSky->SunTextureAsset.empty()) desiredTextures.emplace(FileParsers::NormalizeAssetKey(level.CurrentSky->SunTextureAsset));
        }
        std::vector<std::string> newlyAcquiredMeshes;
        std::vector<std::string> newlyAcquiredMaterials;
        std::vector<std::pair<std::string, TextureHandle>> newlyAcquiredTextures;
        bool acquisitionFailed = false;
        for (const std::string& path : desiredMeshes) {
            if (path.empty() || level.AcquiredMeshAssetPaths.find(path) != level.AcquiredMeshAssetPaths.end()) continue;
            const MeshHandle handle = LoadMeshAsset(path);
            if (handle == 0) {
                LOG_CATEGORY_ERROR("Assets", "Level '{}' could not load mesh dependency '{}'", level.GetName(), path);
                acquisitionFailed = true;
                continue;
            }
            ++MeshAssetRefCounts[path];
            level.AcquiredMeshAssetPaths.emplace(path);
            newlyAcquiredMeshes.push_back(path);
        }
        for (const std::string& path : desiredMaterials) {
            if (path.empty() || level.AcquiredMaterialAssetPaths.find(path) != level.AcquiredMaterialAssetPaths.end()) continue;
            const MaterialHandle handle = LoadMaterialAsset(path);
            if (handle == 0) {
                LOG_CATEGORY_ERROR("Assets", "Level '{}' could not load material dependency '{}'", level.GetName(), path);
                acquisitionFailed = true;
                continue;
            }
            ++MaterialAssetRefCounts[path];
            level.AcquiredMaterialAssetPaths.emplace(path);
            newlyAcquiredMaterials.push_back(path);
        }
        for (const std::string& path : desiredTextures) {
            if (path.empty() || level.AcquiredTextureHandles.find(path) != level.AcquiredTextureHandles.end()) continue;
            const TextureHandle handle = LoadTexture(path);
            if (handle == 0) {
                LOG_CATEGORY_ERROR("Assets", "Level '{}' could not load texture dependency '{}'", level.GetName(), path);
                acquisitionFailed = true;
                continue;
            }
            level.AcquiredTextureHandles.emplace(path, handle);
            newlyAcquiredTextures.emplace_back(path, handle);
        }
        if (acquisitionFailed) {
            for (const auto& [path, handle] : newlyAcquiredTextures) { ReleaseTextureReference(handle); level.AcquiredTextureHandles.erase(path); }
            for (const std::string& path : newlyAcquiredMaterials) {
                auto refs = MaterialAssetRefCounts.find(path);
                if (refs != MaterialAssetRefCounts.end() && refs->second > 1) --refs->second;
                else DestroyUnreferencedMaterialAsset(path);
                level.AcquiredMaterialAssetPaths.erase(path);
            }
            for (const std::string& path : newlyAcquiredMeshes) {
                auto refs = MeshAssetRefCounts.find(path);
                if (refs != MeshAssetRefCounts.end() && refs->second > 1) --refs->second;
                else DestroyUnreferencedMeshAsset(path);
                level.AcquiredMeshAssetPaths.erase(path);
            }
            LOG_CATEGORY_ERROR("Assets", "Asset acquisition for level '{}' failed; new references were rolled back", level.GetName());
            return false;
        }
        if (level.AssetsAcquired && (std::any_of(level.AcquiredMeshAssetPaths.begin(), level.AcquiredMeshAssetPaths.end(), [&](const std::string& path) { return desiredMeshes.find(path) == desiredMeshes.end(); }) ||
            std::any_of(level.AcquiredMaterialAssetPaths.begin(), level.AcquiredMaterialAssetPaths.end(), [&](const std::string& path) { return desiredMaterials.find(path) == desiredMaterials.end(); }) ||
            std::any_of(level.AcquiredTextureHandles.begin(), level.AcquiredTextureHandles.end(), [&](const auto& entry) { return desiredTextures.find(entry.first) == desiredTextures.end(); }) ||
            (level.CurrentSky && (!level.CurrentSky->HasAppliedSunTextureState || level.CurrentSky->AppliedSunTextureAsset != level.CurrentSky->SunTextureAsset || (!level.CurrentSky->MaterialAsset.empty() && level.CurrentSky->MaterialId == 0)))) && vkDeviceWaitIdle(Device) != VK_SUCCESS) {
            for (const auto& [path, handle] : newlyAcquiredTextures) { ReleaseTextureReference(handle); level.AcquiredTextureHandles.erase(path); }
            for (const std::string& path : newlyAcquiredMaterials) {
                auto refs = MaterialAssetRefCounts.find(path);
                if (refs != MaterialAssetRefCounts.end() && refs->second > 1) --refs->second;
                else DestroyUnreferencedMaterialAsset(path);
                level.AcquiredMaterialAssetPaths.erase(path);
            }
            for (const std::string& path : newlyAcquiredMeshes) {
                auto refs = MeshAssetRefCounts.find(path);
                if (refs != MeshAssetRefCounts.end() && refs->second > 1) --refs->second;
                else DestroyUnreferencedMeshAsset(path);
                level.AcquiredMeshAssetPaths.erase(path);
            }
            LOG_CATEGORY_ERROR("Assets", "Could not synchronize before replacing dependencies for level '{}'; new references were rolled back", level.GetName());
            return false;
        }
        for (const auto& object : level.Objects) {
            if (!object) continue;
            auto* mesh = dynamic_cast<Level::Mesh*>(object.get());
            if (!mesh) continue;
            if (mesh->GetMeshType() == Level::Mesh::Custom && !mesh->CustomMesh.empty()) {
                const std::string path = FileParsers::NormalizeAssetKey(mesh->CustomMesh);
                const auto it = FileMeshes.find(path);
                mesh->MeshAssetId = it == FileMeshes.end() ? 0 : it->second;
                if (mesh->MeshAssetId != 0) level.EverUsedMeshAssetIds.emplace(mesh->MeshAssetId);
            }
            for (std::uint32_t i = 0; i < mesh->MaterialAssets.size(); ++i) {
                if (mesh->MaterialAssets[i].empty()) continue;
                const auto it = FileMaterials.find(FileParsers::NormalizeAssetKey(mesh->MaterialAssets[i]));
                mesh->SetMaterialAssetId(it == FileMaterials.end() ? 0 : it->second, i);
            }
        }
        if (level.CurrentSky) {
            MaterialHandle sourceMaterial = level.CurrentSky->RuntimeMaterialSourceId != 0 ? level.CurrentSky->RuntimeMaterialSourceId : level.CurrentSky->MaterialId;
            if (!level.CurrentSky->MaterialAsset.empty()) {
                const auto it = FileMaterials.find(FileParsers::NormalizeAssetKey(level.CurrentSky->MaterialAsset));
                sourceMaterial = it == FileMaterials.end() ? 0 : it->second;
            }
            const std::string& sunTexture = level.CurrentSky->SunTextureAsset;
            if (!sunTexture.empty() && sourceMaterial != 0) {
                if (level.CurrentSky->RuntimeMaterialSourceId != sourceMaterial || level.CurrentSky->RuntimeMaterialId == 0) {
                    if (level.CurrentSky->RuntimeMaterialId != 0) {
                        auto oldRuntime = Materials.find(level.CurrentSky->RuntimeMaterialId);
                        if (oldRuntime != Materials.end()) {
                            DestroyMaterialResource(oldRuntime->second);
                            Materials.erase(oldRuntime);
                        }
                    }
                    level.CurrentSky->RuntimeMaterialId = CloneMaterial(sourceMaterial, "__SkyMaterialInstance_Level" + std::to_string(level.GetId()));
                    level.CurrentSky->RuntimeMaterialSourceId = level.CurrentSky->RuntimeMaterialId != 0 ? sourceMaterial : 0;
                    level.CurrentSky->HasAppliedSunTextureState = false;
                }
                const MaterialHandle runtimeMaterial = level.CurrentSky->RuntimeMaterialId;
                level.CurrentSky->SetMaterialId(runtimeMaterial != 0 ? runtimeMaterial : sourceMaterial);
                if (runtimeMaterial != 0 && (!level.CurrentSky->HasAppliedSunTextureState ||
                    level.CurrentSky->AppliedSunTextureAsset != sunTexture || level.CurrentSky->AppliedSunTextureMaterial != runtimeMaterial)) {
                    const bool applied = SetMaterialTextureAtSlot(runtimeMaterial, 7, sunTexture);
                    level.CurrentSky->AppliedSunTextureAsset = sunTexture;
                    level.CurrentSky->AppliedSunTextureMaterial = runtimeMaterial;
                    level.CurrentSky->HasAppliedSunTextureState = applied;
                    level.CurrentSky->SunTextureOverrideApplied = applied;
                    if (!applied) LOG_CATEGORY_WARN("Assets", "Could not apply sun texture '{}' to the level-local sky material", sunTexture);
                }
                else if (runtimeMaterial == 0) {
                    level.CurrentSky->AppliedSunTextureAsset = sunTexture;
                    level.CurrentSky->AppliedSunTextureMaterial = sourceMaterial;
                    level.CurrentSky->HasAppliedSunTextureState = true;
                    level.CurrentSky->SunTextureOverrideApplied = false;
                    LOG_CATEGORY_WARN("Assets", "Could not create a level-local sky material for '{}'; its sun texture override is disabled", level.GetName());
                }
            }
            else {
                if (level.CurrentSky->RuntimeMaterialId != 0) {
                    auto oldRuntime = Materials.find(level.CurrentSky->RuntimeMaterialId);
                    if (oldRuntime != Materials.end()) { DestroyMaterialResource(oldRuntime->second); Materials.erase(oldRuntime); }
                    level.CurrentSky->RuntimeMaterialId = 0;
                    level.CurrentSky->RuntimeMaterialSourceId = 0;
                }
                level.CurrentSky->SetMaterialId(sourceMaterial);
                level.CurrentSky->AppliedSunTextureAsset.clear();
                level.CurrentSky->AppliedSunTextureMaterial = sourceMaterial;
                level.CurrentSky->HasAppliedSunTextureState = true;
                level.CurrentSky->SunTextureOverrideApplied = false;
            }
        }
        for (auto it = level.AcquiredMeshAssetPaths.begin(); it != level.AcquiredMeshAssetPaths.end();) {
            if (desiredMeshes.find(*it) != desiredMeshes.end()) { ++it; continue; }
            const std::string stale = *it;
            auto refs = MeshAssetRefCounts.find(stale);
            if (refs != MeshAssetRefCounts.end() && refs->second > 1) --refs->second;
            else DestroyUnreferencedMeshAsset(stale);
            it = level.AcquiredMeshAssetPaths.erase(it);
        }
        for (auto it = level.AcquiredMaterialAssetPaths.begin(); it != level.AcquiredMaterialAssetPaths.end();) {
            if (desiredMaterials.find(*it) != desiredMaterials.end()) { ++it; continue; }
            const std::string stale = *it;
            auto refs = MaterialAssetRefCounts.find(stale);
            if (refs != MaterialAssetRefCounts.end() && refs->second > 1) --refs->second;
            else DestroyUnreferencedMaterialAsset(stale);
            it = level.AcquiredMaterialAssetPaths.erase(it);
        }
        for (auto it = level.AcquiredTextureHandles.begin(); it != level.AcquiredTextureHandles.end();) {
            if (desiredTextures.find(it->first) != desiredTextures.end()) { ++it; continue; }
            ReleaseTextureReference(it->second);
            it = level.AcquiredTextureHandles.erase(it);
        }
        level.AssetsAcquired = true;
        return true;
    }

    void Vulkan::ReleaseSkyMaterialInstance(Level::Sky& sky) {
        if (sky.RuntimeMaterialId == 0) return;
        if (Device != VK_NULL_HANDLE && vkDeviceWaitIdle(Device) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Assets", "Could not synchronize before releasing a level-local sky material");
            return;
        }
        if (sky.MaterialId == sky.RuntimeMaterialId) sky.MaterialId = 0;
        const auto runtimeMaterial = Materials.find(sky.RuntimeMaterialId);
        if (runtimeMaterial != Materials.end()) { DestroyMaterialResource(runtimeMaterial->second); Materials.erase(runtimeMaterial); }
        sky.RuntimeMaterialId = 0;
        sky.RuntimeMaterialSourceId = 0;
        sky.AppliedSunTextureAsset.clear();
        sky.AppliedSunTextureMaterial = 0;
        sky.HasAppliedSunTextureState = false;
        sky.SunTextureOverrideApplied = false;
    }

    bool Vulkan::ReleaseLevelAssets(Level& level) {
        if (BakedLightingCacheAttemptedLevelUID == level.GetId() && BakedLightingLevelUID != level.GetId())
            BakedLightingCacheAttemptedLevelUID = 0;
        if (!level.AssetsAcquired && level.AcquiredMeshAssetPaths.empty() && level.AcquiredMaterialAssetPaths.empty() && level.AcquiredTextureHandles.empty())
            return true;
        if (Device != VK_NULL_HANDLE && vkDeviceWaitIdle(Device) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Assets", "Could not wait for GPU before releasing level '{}' assets", level.GetName());
            return false;
        }
        if (BakedLightingLevelUID == level.GetId()) {
            if (LightmapArrayDescriptorSet != VK_NULL_HANDLE) {
                if (LightmapFallbackArrayTexture.image != VK_NULL_HANDLE) {
                    VkDescriptorImageInfo fallbackInfo{};
                    fallbackInfo.sampler = LightmapFallbackArrayTexture.sampler;
                    fallbackInfo.imageView = LightmapFallbackArrayTexture.view;
                    fallbackInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    VkWriteDescriptorSet fallbackWrite{};
                    fallbackWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    fallbackWrite.dstSet = LightmapArrayDescriptorSet;
                    fallbackWrite.dstBinding = 0;
                    fallbackWrite.descriptorCount = 1;
                    fallbackWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    fallbackWrite.pImageInfo = &fallbackInfo;
                    vkUpdateDescriptorSets(Device, 1, &fallbackWrite, 0, nullptr);
                }
            }
            if (BakedLightingAtlasArrayTexture.image != VK_NULL_HANDLE)DestroyTexture(BakedLightingAtlasArrayTexture);
            BakedLightingAtlasLayers = 0;
            BakedLightingAtlasPages = 0;
            BakedLightingHasStaticSun = false;
            BakedStaticLights.clear();
            BakedLightmapAssignments.clear();
            BakedLightingLevelUID = 0;
            BakedLightingCacheAttemptedLevelUID = 0;
        }
        if (level.CurrentSky && level.CurrentSky->RuntimeMaterialId != 0) {
            if (level.CurrentSky->MaterialId == level.CurrentSky->RuntimeMaterialId) level.CurrentSky->MaterialId = 0;
            auto runtimeMaterial = Materials.find(level.CurrentSky->RuntimeMaterialId);
            if (runtimeMaterial != Materials.end()) { DestroyMaterialResource(runtimeMaterial->second); Materials.erase(runtimeMaterial); }
            level.CurrentSky->RuntimeMaterialId = 0;
            level.CurrentSky->RuntimeMaterialSourceId = 0;
            level.CurrentSky->HasAppliedSunTextureState = false;
        }
        for (const std::string& path : level.AcquiredMeshAssetPaths) {
            auto refs = MeshAssetRefCounts.find(path);
            if (refs != MeshAssetRefCounts.end() && refs->second > 1) --refs->second;
            else DestroyUnreferencedMeshAsset(path);
        }
        for (const std::string& path : level.AcquiredMaterialAssetPaths) {
            auto refs = MaterialAssetRefCounts.find(path);
            if (refs != MaterialAssetRefCounts.end() && refs->second > 1) --refs->second;
            else DestroyUnreferencedMaterialAsset(path);
        }
        for (const auto& [path, handle] : level.AcquiredTextureHandles) { (void)path; ReleaseTextureReference(handle); }
        level.AcquiredMeshAssetPaths.clear();
        level.AcquiredMaterialAssetPaths.clear();
        level.AcquiredTextureHandles.clear();
        level.AssetsAcquired = false;
        for (const auto& object : level.Objects) {
            if (auto* mesh = object ? dynamic_cast<Level::Mesh*>(object.get()) : nullptr) {
                if (!mesh->CustomMesh.empty()) mesh->MeshAssetId = 0;
                for (std::uint32_t i = 0; i < mesh->MaterialAssets.size(); ++i)if (!mesh->MaterialAssets[i].empty()) mesh->SetMaterialAssetId(0, i);
            }
        }
        if (level.CurrentSky && (!level.CurrentSky->MaterialAsset.empty() || level.CurrentSky->RuntimeMaterialId == 0))
            level.CurrentSky->MaterialId = level.CurrentSky->MaterialAsset.empty() ? level.CurrentSky->MaterialId : 0;
        return true;
    }

    bool Vulkan::BakeLighting(const Level& level, std::uint32_t resolution, float texelsPerWorldUnit, std::uint64_t maxAtlasBytes, std::uint32_t maxAtlasPages, std::uint32_t samplesPerTexel) {
        if (!IsInitialized()) {
            LOG_CATEGORY_ERROR("Lighting", "Vulkan is not initialized");
            return false;
        }
        if (!level.AssetsAcquired) {
            LOG_CATEGORY_ERROR("Lighting", "Level '{}' must acquire its assets before baking", level.GetName());
            return false;
        }
        constexpr std::uint32_t MinResolution = 8;
        constexpr std::uint64_t MiB = 1024ull * 1024ull;
        if (resolution < MinResolution || !std::isfinite(texelsPerWorldUnit) || texelsPerWorldUnit <= 0.0f ||
            maxAtlasBytes == 0 || maxAtlasPages == 0 || samplesPerTexel == 0 || samplesPerTexel > 65536u) {
            LOG_CATEGORY_ERROR("Lighting", "Invalid bake settings (resolution {}, texels/unit {}, budget {}, pages {}, samples/texel {} ; samples must be 1..65536)", resolution, texelsPerWorldUnit, maxAtlasBytes, maxAtlasPages, samplesPerTexel);
            return false;
        }
        const std::uint64_t bakeSourceSignature = ComputeBakedLightingSourceSignature(level);
        if (bakeSourceSignature == 0u) {
            LOG_CATEGORY_ERROR("Lighting", "Could not build a stable source signature for this level; refusing to bake an unverifiable cache");
            return false;
        }
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(PhysicalDevice, &deviceProperties);
        constexpr std::uint32_t MaxEncodedAtlasPages = 256u;
        if (maxAtlasPages > MaxEncodedAtlasPages) {
            LOG_CATEGORY_WARN("Lighting", "Requested page limit {} exceeds the lightmap metadata encoding range {}; using {} pages", maxAtlasPages, MaxEncodedAtlasPages, MaxEncodedAtlasPages);
            maxAtlasPages = MaxEncodedAtlasPages;
        }
        if (resolution > deviceProperties.limits.maxImageDimension2D) {
            LOG_CATEGORY_ERROR("Lighting", "Requested atlas resolution {} exceeds this GPU's maxImageDimension2D ({})", resolution, deviceProperties.limits.maxImageDimension2D);
            return false;
        }
        if (maxAtlasPages > deviceProperties.limits.maxImageArrayLayers) {
            LOG_CATEGORY_WARN("Lighting", "Requested page limit {} exceeds maxImageArrayLayers {}; using the GPU limit", maxAtlasPages, deviceProperties.limits.maxImageArrayLayers);
            maxAtlasPages = deviceProperties.limits.maxImageArrayLayers;
        }
        if (vkDeviceWaitIdle(Device) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Lighting", "Renderer refused to respond");
            return false;
        }
        if (LightmapArrayDescriptorSet != VK_NULL_HANDLE) {
            if (LightmapFallbackArrayTexture.image != VK_NULL_HANDLE) {
                VkDescriptorImageInfo fallbackInfo{};
                fallbackInfo.sampler = LightmapFallbackArrayTexture.sampler;
                fallbackInfo.imageView = LightmapFallbackArrayTexture.view;
                fallbackInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet fallbackWrite{};
                fallbackWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                fallbackWrite.dstSet = LightmapArrayDescriptorSet;
                fallbackWrite.dstBinding = 0;
                fallbackWrite.descriptorCount = 1;
                fallbackWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                fallbackWrite.pImageInfo = &fallbackInfo;
                vkUpdateDescriptorSets(Device, 1, &fallbackWrite, 0, nullptr);
            }
        }
        if (BakedLightingAtlasArrayTexture.image != VK_NULL_HANDLE)DestroyTexture(BakedLightingAtlasArrayTexture);
        BakedLightingAtlasLayers = 0;
        BakedLightingAtlasPages = 0;
        BakedLightingHasStaticSun = false;
        BakedStaticLights.clear();
        BakedLightmapAssignments.clear();
        BakedLightingLevelUID = 0;
        const bool hasStaticSun = level.GetSunType() == Level::SunType::Static;
        std::vector<const Level::Light*> staticLights;
        std::size_t skippedStaticLights = 0;
        for (const auto& ownedLight : level.GetLevelLights()) {
            if (!ownedLight || ownedLight->GetType() != Level::Light::Type::Static) continue;
            if (staticLights.size() < MaxBakedStaticLights) staticLights.push_back(ownedLight.get());
            else ++skippedStaticLights;
        }
        if (skippedStaticLights != 0)
            LOG_CATEGORY_WARN("Lighting", "{} Static point light(s) exceed the current limit of {}; they will be omitted from this bake", skippedStaticLights, MaxBakedStaticLights);
        const std::uint64_t layerGroupsForBudget = 1ull + static_cast<std::uint64_t>(staticLights.size()) + (hasStaticSun ? 1ull : 0ull);
        struct Rect { std::uint32_t x = 0, y = 0, w = 0, h = 0; };
        struct AtlasPagePacking { std::vector<Rect> freeRects; };
        struct MeshRequest {
            const Level::LevelObject* object = nullptr;
            std::uint32_t objectIndex = 0;
            std::uint32_t meshHandle = 0;
            std::uint32_t ownerId = 0;
            std::uint32_t outerSize = 0;
            std::uint32_t page = UINT32_MAX;
            Rect outer{};
        };
        const std::uint32_t gutter = resolution >= 1024u ? 8u : (resolution >= 256u ? 6u : (resolution >= 128u ? 4u : 2u));
        const std::uint32_t maxInnerSize = resolution > gutter * 2u ? resolution - gutter * 2u : 1u;
        const std::uint64_t bytesPerPage = static_cast<std::uint64_t>(resolution) * resolution * 4ull;
        if (bytesPerPage == 0 || bytesPerPage > maxAtlasBytes || layerGroupsForBudget > deviceProperties.limits.maxImageArrayLayers) {
            LOG_CATEGORY_ERROR("Lighting", "The requested resolution/baked-light count cannot fit the atlas budget or the GPU's image-array layer limit");
            return false;
        }
        const std::uint64_t bytesPerGroupedPage = bytesPerPage * layerGroupsForBudget;
        const std::size_t maxPagesByBudget = static_cast<std::size_t>(maxAtlasBytes / bytesPerGroupedPage);
        const std::size_t maxPagesByLayers = static_cast<std::size_t>(deviceProperties.limits.maxImageArrayLayers / layerGroupsForBudget);
        const std::size_t maxPages = (std::min)({ static_cast<std::size_t>(maxAtlasPages), maxPagesByBudget, maxPagesByLayers });
        if (maxPages == 0) {
            LOG_CATEGORY_ERROR("Lighting", "Atlas resolution exceeds the configured memory budget");
            return false;
        }
        auto resolveMeshHandle = [this](const Level::Mesh& mesh) -> MeshHandle {
            MeshHandle handle = 0;
            switch (mesh.GetMeshType()) {
            case Level::Mesh::Cube:        handle = CubeMeshHandle; break;
            case Level::Mesh::Plane:       handle = PlaneMeshHandle; break;
            case Level::Mesh::Sphere:      handle = SphereMeshHandle; break;
            case Level::Mesh::Cylinder:    handle = CylinderMeshHandle; break;
            case Level::Mesh::Wedge:       handle = WedgeMeshHandle; break;
            case Level::Mesh::CornerWedge: handle = CornerWedgeMeshHandle; break;
            case Level::Mesh::Cone:        handle = ConeMeshHandle; break;
            case Level::Mesh::Pyramid:     handle = PyramidMeshHandle; break;
            case Level::Mesh::Custom:      handle = mesh.GetMeshAssetId(); break;
            default: break;
            }
            return handle;
            };
        auto alignUp = [](std::uint32_t value, std::uint32_t alignment) -> std::uint32_t {
            if (alignment <= 1u) return value;
            const std::uint64_t aligned = (static_cast<std::uint64_t>(value) + alignment - 1u) / alignment * alignment;
            return static_cast<std::uint32_t>((std::min<std::uint64_t>)(aligned, (std::numeric_limits<std::uint32_t>::max)()));
            };
        std::vector<MeshRequest> requests;
        std::size_t undersampledInstances = 0;
        requests.reserve(level.GetObjects().size());
        const auto& levelObjectsForBake = level.GetObjects();
        for (std::size_t objectIndex = 0; objectIndex < levelObjectsForBake.size(); ++objectIndex) {
            const auto& object = levelObjectsForBake[objectIndex];
            if (!object) continue;
            const auto* mesh = dynamic_cast<const Level::Mesh*>(object.get());
            if (!mesh || mesh->GetMobility() == Level::Mesh::Mobility::Movable) continue;
            const MeshHandle meshHandle = resolveMeshHandle(*mesh);
            const auto gpuMeshIt = Meshes.find(meshHandle);
            if (meshHandle == 0 || gpuMeshIt == Meshes.end())continue;
            const Vector3 objectScale = mesh->GetSize();
            double surfaceArea = 0.0;
            for (const SubMeshData& subMesh : gpuMeshIt->second.cpuSubMeshes) {
                for (std::size_t i = 0; i + 2 < subMesh.indices.size(); i += 3) {
                    const std::uint32_t i0 = subMesh.indices[i + 0];
                    const std::uint32_t i1 = subMesh.indices[i + 1];
                    const std::uint32_t i2 = subMesh.indices[i + 2];
                    if (i0 >= subMesh.vertices.size() || i1 >= subMesh.vertices.size() || i2 >= subMesh.vertices.size()) continue;
                    const auto scaledPosition = [&objectScale](const MeshVertex& vertex) {
                        return Vector3(vertex.position[0] * objectScale.x,
                            vertex.position[1] * objectScale.y, vertex.position[2] * objectScale.z);
                        };
                    const Vector3 p0 = scaledPosition(subMesh.vertices[i0]);
                    const Vector3 p1 = scaledPosition(subMesh.vertices[i1]);
                    const Vector3 p2 = scaledPosition(subMesh.vertices[i2]);
                    surfaceArea += (p1 - p0).Cross(p2 - p0).Magnitude() * 0.5;
                }
            }
            if (!std::isfinite(surfaceArea) || surfaceArea < 1e-4) {
                const Vector3 objectSize = objectScale.Abs();
                surfaceArea = 2.0 * (objectSize.x * objectSize.y +
                    objectSize.y * objectSize.z + objectSize.z * objectSize.x);
            }
            const double desired = (std::max)(32.0,
                std::sqrt((std::max)(surfaceArea, 0.0001)) * static_cast<double>(texelsPerWorldUnit) * 1.35);
            if (desired > static_cast<double>(maxInnerSize)) ++undersampledInstances;
            MeshRequest request{};
            request.object = object.get();
            request.objectIndex = static_cast<std::uint32_t>(objectIndex);
            request.meshHandle = meshHandle;
            request.ownerId = static_cast<std::uint32_t>(requests.size() + 1u);
            request.outerSize = (std::min)(resolution, (std::min)(alignUp((std::max)(static_cast<std::uint32_t>((std::min)(desired, static_cast<double>(maxInnerSize))), 1u), 16u), maxInnerSize) + gutter * 2u);
            requests.push_back(request);
        }
        if (requests.empty()) {
            LOG_CATEGORY_WARN("Lighting", "No loaded Static/Stationary mesh instances were eligible for atlas packing");
            return false;
        }
        auto intersects = [](const Rect& a, const Rect& b) {return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y; };
        auto contains = [](const Rect& a, const Rect& b) {return b.x >= a.x && b.y >= a.y && b.x + b.w <= a.x + a.w && b.y + b.h <= a.y + a.h; };
        auto placeInPage = [&](AtlasPagePacking& page, std::uint32_t w, std::uint32_t h, Rect& placed) -> bool {
            std::size_t bestIndex = (std::numeric_limits<std::size_t>::max)();
            std::uint32_t bestShortSide = UINT32_MAX;
            std::uint32_t bestLongSide = UINT32_MAX;
            for (std::size_t i = 0; i < page.freeRects.size(); ++i) {
                const Rect& free = page.freeRects[i];
                if (w > free.w || h > free.h)continue;
                const std::uint32_t leftoverX = free.w - w;
                const std::uint32_t leftoverY = free.h - h;
                const std::uint32_t shortSide = (std::min)(leftoverX, leftoverY);
                const std::uint32_t longSide = (std::max)(leftoverX, leftoverY);
                if (shortSide < bestShortSide || (shortSide == bestShortSide && longSide < bestLongSide)) { bestIndex = i; bestShortSide = shortSide; bestLongSide = longSide; }
            }
            if (bestIndex == SIZE_MAX)return false;
            const Rect freeChosen = page.freeRects[bestIndex];
            placed = { freeChosen.x, freeChosen.y, w, h };
            std::vector<Rect> splitRects;
            splitRects.reserve(page.freeRects.size() * 2u + 4u);
            for (const Rect& free : page.freeRects) {
                if (!intersects(free, placed)) { splitRects.push_back(free); continue; }
                if (placed.x > free.x)splitRects.push_back({ free.x, free.y, placed.x - free.x, free.h });
                if (placed.x + placed.w < free.x + free.w)splitRects.push_back({ placed.x + placed.w, free.y, free.x + free.w - (placed.x + placed.w), free.h });
                if (placed.y > free.y)splitRects.push_back({ free.x, free.y, free.w, placed.y - free.y });
                if (placed.y + placed.h < free.y + free.h)splitRects.push_back({ free.x, placed.y + placed.h, free.w, free.y + free.h - (placed.y + placed.h) });
            }
            splitRects.erase(std::remove_if(splitRects.begin(), splitRects.end(), [](const Rect& r) {return r.w == 0 || r.h == 0; }), splitRects.end());
            for (std::size_t i = 0; i < splitRects.size();) {
                bool removed = false;
                for (std::size_t j = 0; j < splitRects.size(); ++j) {
                    if (i != j && contains(splitRects[j], splitRects[i])) {
                        splitRects.erase(splitRects.begin() + static_cast<std::ptrdiff_t>(i));
                        removed = true;
                        break;
                    }
                }
                if (!removed) ++i;
            }
            page.freeRects = std::move(splitRects);
            return true;
            };
        std::vector<std::size_t> packingOrder(requests.size());
        for (std::size_t i = 0; i < packingOrder.size(); ++i)packingOrder[i] = i;
        std::sort(packingOrder.begin(), packingOrder.end(), [&](std::size_t a, std::size_t b) {return requests[a].outerSize > requests[b].outerSize; });
        std::vector<AtlasPagePacking> pages;
        for (const std::size_t requestIndex : packingOrder) {
            MeshRequest& request = requests[requestIndex];
            Rect placement{};
            bool packed = false;
            for (std::size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
                if (placeInPage(pages[pageIndex], request.outerSize, request.outerSize, placement)) {
                    request.page = static_cast<std::uint32_t>(pageIndex);
                    request.outer = placement;
                    packed = true;
                    break;
                }
            if (!packed) {
                if (pages.size() >= maxPages) {
                    LOG_CATEGORY_ERROR("Lighting", "Atlas budget exceeded: {} mesh instances need more than {} pages at {}x{} (budget {} MiB)", requests.size(), maxPages, resolution, resolution, maxAtlasBytes / (1024ull * 1024ull));
                    return false;
                }
                AtlasPagePacking page{};
                page.freeRects.push_back({ 0, 0, resolution, resolution });
                pages.push_back(std::move(page));
                if (!placeInPage(pages.back(), request.outerSize, request.outerSize, placement)) {
                    LOG_CATEGORY_ERROR("Lighting", "Could not fit a {}x{} lightmap region into a {}x{} atlas page", request.outerSize, request.outerSize, resolution, resolution);
                    return false;
                }
                request.page = static_cast<std::uint32_t>(pages.size() - 1u);
                request.outer = placement;
            }
        }
        std::unordered_map<const Level::LevelObject*, BakedLightmapAssignment> newAssignments;
        newAssignments.reserve(requests.size());
        for (const MeshRequest& request : requests) {
            const std::uint32_t innerW = request.outer.w > gutter * 2u ? request.outer.w - gutter * 2u : 1u;
            const std::uint32_t innerH = request.outer.h > gutter * 2u ? request.outer.h - gutter * 2u : 1u;
            BakedLightmapAssignment assignment{};
            assignment.page = request.page;
            assignment.scaleOffset = {
                static_cast<float>(innerW > 1u ? innerW - 1u : 0u) / static_cast<float>(resolution),
                static_cast<float>(innerH > 1u ? innerH - 1u : 0u) / static_cast<float>(resolution),
                (static_cast<float>(request.outer.x + gutter) + 0.5f) / static_cast<float>(resolution),
                (static_cast<float>(request.outer.y + gutter) + 0.5f) / static_cast<float>(resolution)
            };
            newAssignments.emplace(request.object, assignment);
        }
        const std::uint32_t pageCount = static_cast<std::uint32_t>(pages.size());
        const std::uint32_t layerGroupCount = 1u + static_cast<std::uint32_t>(staticLights.size()) + (hasStaticSun ? 1u : 0u);
        const std::uint64_t layerCount64 = static_cast<std::uint64_t>(pageCount) * layerGroupCount;
        if (pageCount == 0 || layerCount64 == 0 || layerCount64 > deviceProperties.limits.maxImageArrayLayers || layerCount64 > UINT32_MAX) {
            LOG_CATEGORY_ERROR("Lighting", "Baked light groups require {} atlas layers, but this GPU supports at most {}", layerCount64, deviceProperties.limits.maxImageArrayLayers);
            return false;
        }
        const std::uint32_t layerCount = static_cast<std::uint32_t>(layerCount64);
        const std::uint64_t pagePixelCount64 = static_cast<std::uint64_t>(resolution) * resolution;
        const std::uint64_t pageBytes64 = pagePixelCount64 * 4ull;
        const std::uint64_t atlasBytes64 = pageBytes64 * layerCount64;
        if (pagePixelCount64 > UINT32_MAX || atlasBytes64 > maxAtlasBytes || atlasBytes64 > (std::numeric_limits<std::size_t>::max)()) {
            LOG_CATEGORY_ERROR("Lighting", "The actual baked array ({} layers at {}x{}) exceeds the atlas budget or this implementation's indexing limits", layerCount, resolution, resolution);
            return false;
        }
        const std::size_t pagePixelCount = static_cast<std::size_t>(pagePixelCount64);
        const std::size_t pageBytes = static_cast<std::size_t>(pageBytes64);
        const std::size_t atlasBytes = static_cast<std::size_t>(atlasBytes64);
        std::vector<unsigned char> atlasPixels;
        try { atlasPixels.assign(atlasBytes, 0u); }
        catch (const std::bad_alloc&) {
            LOG_CATEGORY_ERROR("Lighting", "Could not allocate {} MiB of CPU lightmap staging data", atlasBytes / MiB);
            return false;
        }
        for (std::size_t alpha = 3; alpha < atlasPixels.size(); alpha += 4) atlasPixels[alpha] = 255u;

        struct alignas(16) TraceSurfaceSample {
            std::array<float, 4> position{};
            std::array<float, 4> normal{};
            std::array<float, 4> worldDx{};
            std::array<float, 4> worldDy{};
            std::array<std::uint32_t, 4> target{};
        };
        struct alignas(16) TraceTriangleGpu {
            std::array<float, 4> p0{}, p1{}, p2{}, albedo{};
        };
        struct alignas(16) TraceBvhNodeGpu {
            std::array<float, 4> boundsMin{}, boundsMax{};
            std::array<std::uint32_t, 4> meta{};
        };
        struct alignas(16) TraceLightGpu {
            std::array<float, 4> positionRadius{}, colorIntensity{};
            std::array<std::uint32_t, 4> meta{};
        };
        struct alignas(16) TracePushConstants {
            std::array<std::uint32_t, 4> counts{};
            std::array<std::int32_t, 4> options{};
            std::array<std::uint32_t, 4> offsets{};
            std::array<float, 4> sunDirection{};
            std::array<float, 4> sunColorIntensity{};
        };
        static_assert(sizeof(TraceSurfaceSample) == 80, "GLSL SurfaceSample layout mismatch");
        static_assert(sizeof(TraceTriangleGpu) == 64, "GLSL TraceTriangle layout mismatch");
        static_assert(sizeof(TraceBvhNodeGpu) == 48, "GLSL BvhNode layout mismatch");
        static_assert(sizeof(TraceLightGpu) == 48, "GLSL TraceLight layout mismatch");
        static_assert(sizeof(TracePushConstants) == 80, "GLSL push-constant layout mismatch");

        struct CpuBakeMaterial {
            Vector3 color = Vector3::One();
            Vector3 averageAlbedo = Vector3::One();
        };
        struct CpuBakeTriangle {
            Vector3 p[3]{};
            Vector2 lightmapUV[3]{};
            std::uint32_t materialIndex = 0;
            std::uint32_t page = 0;
            std::uint32_t ownerId = 0;
            std::array<float, 4> lightmapScaleOffset{};
        };
        struct CpuBvhNode {
            Vector3 boundsMin = Vector3::Zero();
            Vector3 boundsMax = Vector3::Zero();
            std::uint32_t left = 0, right = 0, first = 0, count = 0;
        };
        struct TargetPixel { std::uint32_t page = 0, pixel = 0; };
        struct BakePass { std::int32_t activeGroup = -1; std::uint32_t outputGroup = 0; };

        auto srgbToLinear = [](float c) -> float {
            c = std::clamp(c, 0.0f, 1.0f);
            return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
        auto writeRadianceRGBM = [&atlasPixels](std::size_t offset, const float* radiance) {
            constexpr float Range = 16.0f;
            const float r = std::isfinite(radiance[0]) ? (std::max)(radiance[0], 0.0f) : 0.0f;
            const float g = std::isfinite(radiance[1]) ? (std::max)(radiance[1], 0.0f) : 0.0f;
            const float b = std::isfinite(radiance[2]) ? (std::max)(radiance[2], 0.0f) : 0.0f;
            const float maximum = (std::max)({ r, g, b });
            const float multiplier = std::clamp(std::ceil((maximum / Range) * 255.0f) / 255.0f, 1.0f / 255.0f, 1.0f);
            const float inverse = 1.0f / (multiplier * Range);
            atlasPixels[offset + 0u] = static_cast<unsigned char>(std::clamp(std::lround((std::min)(r * inverse, 1.0f) * 255.0f), 0l, 255l));
            atlasPixels[offset + 1u] = static_cast<unsigned char>(std::clamp(std::lround((std::min)(g * inverse, 1.0f) * 255.0f), 0l, 255l));
            atlasPixels[offset + 2u] = static_cast<unsigned char>(std::clamp(std::lround((std::min)(b * inverse, 1.0f) * 255.0f), 0l, 255l));
            atlasPixels[offset + 3u] = static_cast<unsigned char>(std::clamp(std::lround(multiplier * 255.0f), 1l, 255l));
            };

        std::unordered_map<MaterialHandle, std::uint32_t> materialIndices;
        std::vector<CpuBakeMaterial> bakeMaterials;
        auto getBakeMaterial = [&](MaterialHandle handle, const std::array<float, 4>& fallbackColor, const std::string& fallbackTexture) -> std::uint32_t {
            const auto existing = materialIndices.find(handle);
            if (handle != 0 && existing != materialIndices.end()) return existing->second;
            CpuBakeMaterial bakedMaterial{};
            std::string texturePath = fallbackTexture;
            const auto materialIt = Materials.find(handle);
            if (handle != 0 && materialIt != Materials.end()) {
                const MaterialResource& source = materialIt->second;
                bakedMaterial.color = Vector3(source.gpuData.color[0], source.gpuData.color[1], source.gpuData.color[2]);
                const TextureHandle textureHandle = source.textures[0];
                if (textureHandle != 0) {
                    for (const auto& [path, loadedHandle] : FileTextures) {
                        if (loadedHandle == textureHandle) { texturePath = path; break; }
                    }
                }
            }
            else
                bakedMaterial.color = Vector3(fallbackColor[0], fallbackColor[1], fallbackColor[2]);
            if (!texturePath.empty()) {
                std::string textureError;
                FileParsers::DecodedTexture decodedTexture{};
                if (FileParsers::LoadTextureImage(texturePath, decodedTexture, textureError) &&
                    decodedTexture.width > 0 && decodedTexture.height > 0 && !decodedTexture.rgba.empty()) {
                    long double sumR = 0.0L, sumG = 0.0L, sumB = 0.0L;
                    const std::size_t pixels = decodedTexture.rgba.size() / 4u;
                    for (std::size_t i = 0; i < pixels; ++i) {
                        sumR += srgbToLinear(static_cast<float>(decodedTexture.rgba[i * 4u + 0u]) / 255.0f);
                        sumG += srgbToLinear(static_cast<float>(decodedTexture.rgba[i * 4u + 1u]) / 255.0f);
                        sumB += srgbToLinear(static_cast<float>(decodedTexture.rgba[i * 4u + 2u]) / 255.0f);
                    }
                    if (pixels != 0) bakedMaterial.averageAlbedo = Vector3(
                        static_cast<double>(sumR / pixels), static_cast<double>(sumG / pixels), static_cast<double>(sumB / pixels));
                }
                else if (!textureError.empty()) {
                    LOG_CATEGORY_WARN("Lighting", "Could not decode material texture '{}' for light baking: {}; using material color only", texturePath, textureError);
                }
            }
            bakedMaterial.averageAlbedo *= bakedMaterial.color;
            const std::uint32_t index = static_cast<std::uint32_t>(bakeMaterials.size());
            bakeMaterials.push_back(std::move(bakedMaterial));
            if (handle != 0) materialIndices.emplace(handle, index);
            return index;
            };

        auto transformPosition = [](const MeshVertex& vertex, const Level::Mesh& mesh) -> Vector3 {
            const Vector3& size = mesh.GetSize();
            const Vector3& rotation = mesh.GetRotation();
            Vector3 p(vertex.position[0] * size.x, vertex.position[1] * size.y, vertex.position[2] * size.z);
            const double rx = DegreesToRadians(rotation.x), ry = DegreesToRadians(rotation.y), rz = DegreesToRadians(rotation.z);
            const double cx = std::cos(rx), sx = std::sin(rx);
            const double cy = std::cos(ry), sy = std::sin(ry);
            const double cz = std::cos(rz), sz = std::sin(rz);
            p = Vector3(p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx);
            p = Vector3(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);
            p = Vector3(p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z);
            return p + mesh.GetPosition();
            };

        std::vector<CpuBakeTriangle> cpuTriangles;
        std::vector<TraceTriangleGpu> traceTriangles;
        for (const MeshRequest& request : requests) {
            const auto* mesh = dynamic_cast<const Level::Mesh*>(request.object);
            const auto gpuMeshIt = Meshes.find(request.meshHandle);
            if (!mesh || gpuMeshIt == Meshes.end() || gpuMeshIt->second.cpuSubMeshes.empty()) continue;
            const bool orientBuiltinOutward = mesh->GetMeshType() != Level::Mesh::Custom;
            const GpuMesh& gpuMesh = gpuMeshIt->second;
            Vector3 meshInteriorPoint = Vector3::Zero();
            std::size_t meshInteriorPointCount = 0;
            if (orientBuiltinOutward) {
                for (const SubMeshData& sourceSubMesh : gpuMesh.cpuSubMeshes) {
                    for (const MeshVertex& vertex : sourceSubMesh.vertices) {
                        meshInteriorPoint += transformPosition(vertex, *mesh);
                        ++meshInteriorPointCount;
                    }
                }
                if (meshInteriorPointCount != 0)
                    meshInteriorPoint = meshInteriorPoint / static_cast<double>(meshInteriorPointCount);
            }
            for (std::size_t subIndex = 0; subIndex < gpuMesh.cpuSubMeshes.size(); ++subIndex) {
                const SubMeshData& subMesh = gpuMesh.cpuSubMeshes[subIndex];
                if (!subMesh.IsValid()) continue;
                MaterialHandle materialHandle = mesh->GetMaterialId(static_cast<std::uint32_t>(subIndex));
                if (materialHandle == 0 && subIndex < gpuMesh.subMeshes.size()) materialHandle = gpuMesh.subMeshes[subIndex].defaultMaterial;
                const std::uint32_t materialIndex = getBakeMaterial(materialHandle, subMesh.defaultMaterialColor, subMesh.defaultMaterialTexture);
                for (std::size_t index = 0; index + 2 < subMesh.indices.size(); index += 3) {
                    CpuBakeTriangle cpu{};
                    cpu.materialIndex = materialIndex;
                    cpu.page = request.page;
                    cpu.ownerId = request.ownerId;
                    const auto assignmentIt = newAssignments.find(request.object);
                    if (assignmentIt == newAssignments.end()) continue;
                    cpu.lightmapScaleOffset = assignmentIt->second.scaleOffset;
                    bool validIndices = true;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const std::uint32_t vertexIndex = subMesh.indices[index + corner];
                        if (vertexIndex >= subMesh.vertices.size()) { validIndices = false; break; }
                        const MeshVertex& vertex = subMesh.vertices[vertexIndex];
                        cpu.p[corner] = transformPosition(vertex, *mesh);
                        cpu.lightmapUV[corner] = Vector2(vertex.lightmapUV[0], vertex.lightmapUV[1]);
                    }
                    if (!validIndices) continue;
                    Vector3 faceNormal = (cpu.p[1] - cpu.p[0]).Cross(cpu.p[2] - cpu.p[0]).Unit();
                    if (faceNormal.Magnitude() < 1e-10) continue;
                    if (orientBuiltinOutward) {
                        const Vector3 centroid = (cpu.p[0] + cpu.p[1] + cpu.p[2]) / 3.0;
                        if (faceNormal.Dot(centroid - meshInteriorPoint) < 0.0) {
                            std::swap(cpu.p[1], cpu.p[2]);
                            std::swap(cpu.lightmapUV[1], cpu.lightmapUV[2]);
                            faceNormal = faceNormal * -1.0;
                        }
                    }
                    const Vector3 averageAlbedo = bakeMaterials[materialIndex].averageAlbedo;
                    TraceTriangleGpu gpu{};
                    gpu.p0 = { static_cast<float>(cpu.p[0].x), static_cast<float>(cpu.p[0].y), static_cast<float>(cpu.p[0].z), 0.0f };
                    gpu.p1 = { static_cast<float>(cpu.p[1].x), static_cast<float>(cpu.p[1].y), static_cast<float>(cpu.p[1].z), 0.0f };
                    gpu.p2 = { static_cast<float>(cpu.p[2].x), static_cast<float>(cpu.p[2].y), static_cast<float>(cpu.p[2].z), 0.0f };
                    gpu.albedo = { static_cast<float>(averageAlbedo.x), static_cast<float>(averageAlbedo.y), static_cast<float>(averageAlbedo.z), 1.0f };
                    cpuTriangles.push_back(cpu);
                    traceTriangles.push_back(gpu);
                }
            }
        }
        if (traceTriangles.empty() || traceTriangles.size() > UINT32_MAX) {
            LOG_CATEGORY_ERROR("Lighting", "No valid world-space triangles were available for the software ray tracer");
            return false;
        }
        std::vector<std::uint32_t> triangleOrder(traceTriangles.size());
        for (std::size_t i = 0; i < triangleOrder.size(); ++i) triangleOrder[i] = static_cast<std::uint32_t>(i);
        std::vector<CpuBvhNode> cpuNodes;
        cpuNodes.reserve(traceTriangles.size() / 2u + 1u);
        auto triangleCentroid = [&](std::uint32_t triIndex) {
            const CpuBakeTriangle& tri = cpuTriangles[triIndex];
            return (tri.p[0] + tri.p[1] + tri.p[2]) / 3.0;
            };
        std::function<std::uint32_t(std::uint32_t, std::uint32_t)> buildBvh;
        buildBvh = [&](std::uint32_t first, std::uint32_t count) -> std::uint32_t {
            CpuBvhNode node{};
            constexpr double huge = (std::numeric_limits<double>::max)() * 0.25;
            node.boundsMin = Vector3(huge, huge, huge);
            node.boundsMax = Vector3(-huge, -huge, -huge);
            Vector3 centroidMin(huge, huge, huge), centroidMax(-huge, -huge, -huge);
            for (std::uint32_t i = first; i < first + count; ++i) {
                const CpuBakeTriangle& tri = cpuTriangles[triangleOrder[i]];
                for (const Vector3& point : tri.p) { node.boundsMin = node.boundsMin.Min(point); node.boundsMax = node.boundsMax.Max(point); }
                const Vector3 centroid = triangleCentroid(triangleOrder[i]);
                centroidMin = centroidMin.Min(centroid);
                centroidMax = centroidMax.Max(centroid);
            }
            const std::uint32_t nodeIndex = static_cast<std::uint32_t>(cpuNodes.size());
            cpuNodes.push_back(node);
            if (count <= 4u) {
                cpuNodes[nodeIndex].first = first;
                cpuNodes[nodeIndex].count = count;
                return nodeIndex;
            }
            const Vector3 extent = centroidMax - centroidMin;
            std::size_t axis = extent.y > extent.x ? 1u : 0u;
            if ((axis == 0u ? extent.x : extent.y) < extent.z) axis = 2u;
            const std::uint32_t half = count / 2u;
            const std::uint32_t middle = first + half;
            std::nth_element(triangleOrder.begin() + first, triangleOrder.begin() + middle, triangleOrder.begin() + first + count,
                [&](std::uint32_t a, std::uint32_t b) {
                    const Vector3 ca = triangleCentroid(a), cb = triangleCentroid(b);
                    return axis == 0u ? ca.x < cb.x : (axis == 1u ? ca.y < cb.y : ca.z < cb.z);
                });
            const std::uint32_t left = buildBvh(first, half);
            const std::uint32_t right = buildBvh(middle, count - half);
            cpuNodes[nodeIndex].left = left;
            cpuNodes[nodeIndex].right = right;
            cpuNodes[nodeIndex].count = 0;
            return nodeIndex;
            };
        (void)buildBvh(0u, static_cast<std::uint32_t>(triangleOrder.size()));
        if (cpuNodes.empty() || cpuNodes.size() > UINT32_MAX) {
            LOG_CATEGORY_ERROR("Lighting", "Could not build the software BVH");
            return false;
        }
        std::vector<TraceBvhNodeGpu> traceNodes(cpuNodes.size());
        for (std::size_t i = 0; i < cpuNodes.size(); ++i) {
            const CpuBvhNode& src = cpuNodes[i];
            TraceBvhNodeGpu& dst = traceNodes[i];
            constexpr float BoundsPadding = 0.0001f;
            dst.boundsMin = { static_cast<float>(src.boundsMin.x) - BoundsPadding, static_cast<float>(src.boundsMin.y) - BoundsPadding, static_cast<float>(src.boundsMin.z) - BoundsPadding, 0.0f };
            dst.boundsMax = { static_cast<float>(src.boundsMax.x) + BoundsPadding, static_cast<float>(src.boundsMax.y) + BoundsPadding, static_cast<float>(src.boundsMax.z) + BoundsPadding, 0.0f };
            dst.meta = { src.left, src.right, src.first, src.count };
        }
        std::vector<TraceLightGpu> traceLights;
        traceLights.reserve(level.GetLevelLights().size());
        for (const auto& ownedLight : level.GetLevelLights()) {
            if (!ownedLight) continue;
            const Level::Light& light = *ownedLight;
            const bool isBaked = light.GetType() == Level::Light::Type::Baked;
            const bool isStatic = light.GetType() == Level::Light::Type::Static;
            if ((!isBaked && !isStatic) || (isBaked && !light.IsEnabled())) continue;
            std::uint32_t staticIndex = UINT32_MAX;
            if (isStatic) {
                const auto found = std::find(staticLights.begin(), staticLights.end(), &light);
                if (found == staticLights.end()) continue;
                staticIndex = static_cast<std::uint32_t>(std::distance(staticLights.begin(), found));
            }
            TraceLightGpu gpu{};
            const Vector3 position = light.GetPosition();
            const Vector3 color = isStatic ? Vector3::One() : light.GetColor();
            gpu.positionRadius = { static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z), (std::max)(light.GetEffectiveRadius(), 0.001f) };
            gpu.colorIntensity = { static_cast<float>(color.x), static_cast<float>(color.y), static_cast<float>(color.z), isStatic ? 1.0f : (std::max)(light.GetIntensity(), 0.0f) };
            gpu.meta = { isStatic ? 1u : 0u, staticIndex == UINT32_MAX ? 0u : staticIndex, 0u, 0u };
            traceLights.push_back(gpu);
        }
        const std::uint32_t actualLightCount = static_cast<std::uint32_t>(traceLights.size());
        if (traceLights.empty()) traceLights.emplace_back();
        const bool bakedSun = level.GetSunType() == Level::SunType::Baked;
        Vector3 sunDirection = level.GetSunDirection().Unit();
        if (sunDirection == Vector3::Zero()) sunDirection = Vector3::ZAxis();
        const Vector3 sunMultiplier = level.GetCurrentSky() ? level.GetCurrentSky()->GetSunColorMultiplier() : Vector3::One();
        const float sunAngularRadius = level.GetCurrentSky() ? (std::max)(level.GetCurrentSky()->GetSunSize(), 0.0f) : 0.0f;
        std::vector<BakePass> bakePasses;
        bakePasses.push_back({ -1, 0u });
        std::uint32_t nextGroup = 1u;
        if (hasStaticSun) bakePasses.push_back({ -2, nextGroup++ });
        for (std::uint32_t i = 0; i < staticLights.size(); ++i)
            bakePasses.push_back({ static_cast<std::int32_t>(i), nextGroup++ });
        if (bakePasses.size() != layerGroupCount) {
            LOG_CATEGORY_ERROR("Lighting", "Internal bake pass/layer group count mismatch");
            return false;
        }
        VkPhysicalDeviceProperties traceDeviceProperties{};
        vkGetPhysicalDeviceProperties(PhysicalDevice, &traceDeviceProperties);
        const VkDeviceSize maxStorageRange = traceDeviceProperties.limits.maxStorageBufferRange;
        const VkDeviceSize trianglesBytes = static_cast<VkDeviceSize>(traceTriangles.size()) * sizeof(TraceTriangleGpu);
        const VkDeviceSize nodesBytes = static_cast<VkDeviceSize>(traceNodes.size()) * sizeof(TraceBvhNodeGpu);
        const VkDeviceSize indicesBytes = static_cast<VkDeviceSize>(triangleOrder.size()) * sizeof(std::uint32_t);
        const VkDeviceSize lightsBytes = static_cast<VkDeviceSize>(traceLights.size()) * sizeof(TraceLightGpu);
        const VkDeviceSize sceneDataBytes = nodesBytes + indicesBytes + lightsBytes;
        constexpr std::uint32_t MaxChunkSamples = 32768u;
        const std::uint32_t SamplesPerTexel = samplesPerTexel;
        constexpr std::uint32_t MaxDiffuseBounces = 3u;
        const VkDeviceSize samplesBytes = static_cast<VkDeviceSize>(MaxChunkSamples) * sizeof(TraceSurfaceSample);
        const VkDeviceSize outputBytes = static_cast<VkDeviceSize>(MaxChunkSamples) * layerGroupCount * sizeof(std::array<float, 4>);
        if (trianglesBytes > maxStorageRange || sceneDataBytes > maxStorageRange || samplesBytes > maxStorageRange || outputBytes > maxStorageRange) {
            LOG_CATEGORY_ERROR("Lighting", "Software path tracer storage buffer exceeds maxStorageBufferRange (triangles {} B, combined BVH/index/light data {} B, samples {} B, output {} B; device limit {} B)",
                trianglesBytes, sceneDataBytes, samplesBytes, outputBytes, maxStorageRange);
            return false;
        }
        GpuBuffer sampleBuffer{}, triangleBuffer{}, sceneDataBuffer{}, outputBuffer{};
        void* sampleMapped = nullptr;
        void* triangleMapped = nullptr;
        void* sceneDataMapped = nullptr;
        void* outputMapped = nullptr;
        VkDescriptorSetLayout traceSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool traceDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet traceDescriptorSet = VK_NULL_HANDLE;
        VkPipelineLayout tracePipelineLayout = VK_NULL_HANDLE;
        VkPipeline tracePipeline = VK_NULL_HANDLE;
        auto cleanupTraceResources = [&]() {
            if (tracePipeline != VK_NULL_HANDLE) vkDestroyPipeline(Device, tracePipeline, nullptr);
            if (tracePipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(Device, tracePipelineLayout, nullptr);
            if (traceDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(Device, traceDescriptorPool, nullptr);
            if (traceSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Device, traceSetLayout, nullptr);
            if (sampleMapped && sampleBuffer.memory != VK_NULL_HANDLE) vkUnmapMemory(Device, sampleBuffer.memory);
            if (triangleMapped && triangleBuffer.memory != VK_NULL_HANDLE) vkUnmapMemory(Device, triangleBuffer.memory);
            if (sceneDataMapped && sceneDataBuffer.memory != VK_NULL_HANDLE) vkUnmapMemory(Device, sceneDataBuffer.memory);
            if (outputMapped && outputBuffer.memory != VK_NULL_HANDLE) vkUnmapMemory(Device, outputBuffer.memory);
            DestroyBuffer(sampleBuffer);
            DestroyBuffer(triangleBuffer);
            DestroyBuffer(sceneDataBuffer);
            DestroyBuffer(outputBuffer);
            };
        auto createMappedStorageBuffer = [&](VkDeviceSize bytes, GpuBuffer& buffer, void*& mapped) -> bool {
            if (bytes == 0 || !CreateBuffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer)) return false;
            if (vkMapMemory(Device, buffer.memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
                DestroyBuffer(buffer);
                mapped = nullptr;
                return false;
            }
            return true;
            };
        bool traceResourcesReady =
            createMappedStorageBuffer(samplesBytes, sampleBuffer, sampleMapped) &&
            createMappedStorageBuffer(trianglesBytes, triangleBuffer, triangleMapped) &&
            createMappedStorageBuffer(sceneDataBytes, sceneDataBuffer, sceneDataMapped) &&
            createMappedStorageBuffer(outputBytes, outputBuffer, outputMapped);
        if (!traceResourcesReady) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not allocate/map software path tracer buffers");
            return false;
        }
        std::memcpy(triangleMapped, traceTriangles.data(), static_cast<std::size_t>(trianglesBytes));
        std::memcpy(sceneDataMapped, traceNodes.data(), static_cast<std::size_t>(nodesBytes));
        std::memcpy(static_cast<unsigned char*>(sceneDataMapped) + nodesBytes, triangleOrder.data(), static_cast<std::size_t>(indicesBytes));
        std::memcpy(static_cast<unsigned char*>(sceneDataMapped) + nodesBytes + indicesBytes, traceLights.data(), static_cast<std::size_t>(lightsBytes));
        VkDescriptorSetLayoutBinding traceBindings[4]{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            traceBindings[i].binding = i;
            traceBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            traceBindings[i].descriptorCount = 1;
            traceBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo traceLayoutInfo{};
        traceLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        traceLayoutInfo.bindingCount = 4;
        traceLayoutInfo.pBindings = traceBindings;
        if (vkCreateDescriptorSetLayout(Device, &traceLayoutInfo, nullptr, &traceSetLayout) != VK_SUCCESS) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not create the software path tracer descriptor layout");
            return false;
        }
        VkDescriptorPoolSize tracePoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 };
        VkDescriptorPoolCreateInfo tracePoolInfo{};
        tracePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        tracePoolInfo.maxSets = 1;
        tracePoolInfo.poolSizeCount = 1;
        tracePoolInfo.pPoolSizes = &tracePoolSize;
        if (vkCreateDescriptorPool(Device, &tracePoolInfo, nullptr, &traceDescriptorPool) != VK_SUCCESS) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not create the software path tracer descriptor pool");
            return false;
        }
        VkDescriptorSetAllocateInfo traceSetAllocate{};
        traceSetAllocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        traceSetAllocate.descriptorPool = traceDescriptorPool;
        traceSetAllocate.descriptorSetCount = 1;
        traceSetAllocate.pSetLayouts = &traceSetLayout;
        if (vkAllocateDescriptorSets(Device, &traceSetAllocate, &traceDescriptorSet) != VK_SUCCESS) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not allocate the software path tracer descriptor set");
            return false;
        }
        const std::array<VkDescriptorBufferInfo, 4> traceBufferInfos = { {
            { sampleBuffer.buffer, 0, samplesBytes },
            { triangleBuffer.buffer, 0, trianglesBytes },
            { sceneDataBuffer.buffer, 0, sceneDataBytes },
            { outputBuffer.buffer, 0, outputBytes }
        } };
        std::array<VkWriteDescriptorSet, 4> traceWrites{};
        for (std::uint32_t i = 0; i < traceWrites.size(); ++i) {
            traceWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            traceWrites[i].dstSet = traceDescriptorSet;
            traceWrites[i].dstBinding = i;
            traceWrites[i].descriptorCount = 1;
            traceWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            traceWrites[i].pBufferInfo = &traceBufferInfos[i];
        }
        vkUpdateDescriptorSets(Device, static_cast<std::uint32_t>(traceWrites.size()), traceWrites.data(), 0, nullptr);
        VkPushConstantRange tracePushRange{};
        tracePushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        tracePushRange.offset = 0;
        tracePushRange.size = sizeof(TracePushConstants);
        VkPipelineLayoutCreateInfo tracePipelineLayoutInfo{};
        tracePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        tracePipelineLayoutInfo.setLayoutCount = 1;
        tracePipelineLayoutInfo.pSetLayouts = &traceSetLayout;
        tracePipelineLayoutInfo.pushConstantRangeCount = 1;
        tracePipelineLayoutInfo.pPushConstantRanges = &tracePushRange;
        if (vkCreatePipelineLayout(Device, &tracePipelineLayoutInfo, nullptr, &tracePipelineLayout) != VK_SUCCESS) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not create the software path tracer pipeline layout");
            return false;
        }
        const std::vector<char> computeCode = FileParsers::ReadShaderFile(FileParsers::LightBakeComputeShaderPath);
        if (computeCode.empty()) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not load '{}'; compile Engine/Shaders/light_bake.comp with CompileAllShaders before baking", FileParsers::LightBakeComputeShaderPath);
            return false;
        }
        VkShaderModuleCreateInfo computeModuleInfo{};
        computeModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        computeModuleInfo.codeSize = computeCode.size();
        computeModuleInfo.pCode = reinterpret_cast<const std::uint32_t*>(computeCode.data());
        VkShaderModule computeModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &computeModuleInfo, nullptr, &computeModule) != VK_SUCCESS) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not create the light-bake compute shader module");
            return false;
        }
        VkPipelineShaderStageCreateInfo computeStage{};
        computeStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeStage.module = computeModule;
        computeStage.pName = "main";
        VkComputePipelineCreateInfo computePipelineInfo{};
        computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineInfo.stage = computeStage;
        computePipelineInfo.layout = tracePipelineLayout;
        computePipelineInfo.basePipelineIndex = -1;
        const VkResult pipelineResult = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &tracePipeline);
        vkDestroyShaderModule(Device, computeModule, nullptr);
        if (pipelineResult != VK_SUCCESS) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not create the light-bake compute pipeline");
            return false;
        }
        std::vector<std::uint8_t> coverage;
        std::vector<std::uint32_t> normalGuide;
        std::vector<std::uint32_t> ownerGuide;
        try {
            coverage.assign(pagePixelCount * pageCount, 0u);
            normalGuide.assign(pagePixelCount * pageCount, 0u);
            ownerGuide.assign(pagePixelCount * pageCount, 0u);
        }
        catch (const std::bad_alloc&) {
            cleanupTraceResources();
            LOG_CATEGORY_ERROR("Lighting", "Could not allocate the lightmap coverage/normal/owner guides");
            return false;
        }
        auto encodeOctahedralNormal = [](const Vector3& input) -> std::uint32_t {
            Vector3 n = input.Unit();
            const double denominator = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
            if (denominator <= 1e-12) return 0x80008000u;
            n /= denominator;
            double x = n.x, y = n.y;
            if (n.z < 0.0) {
                const double oldX = x;
                x = (1.0 - std::abs(y)) * (oldX < 0.0 ? -1.0 : 1.0);
                y = (1.0 - std::abs(oldX)) * (y < 0.0 ? -1.0 : 1.0);
            }
            const auto pack = [](double v) -> std::uint32_t {
                return static_cast<std::uint32_t>(std::clamp(std::lround((v * 0.5 + 0.5) * 65535.0), 0l, 65535l));
                };
            return pack(x) | (pack(y) << 16u);
            };
        auto decodeOctahedralNormal = [](std::uint32_t packed) -> Vector3 {
            double x = (static_cast<double>(packed & 0xffffu) / 65535.0) * 2.0 - 1.0;
            double y = (static_cast<double>((packed >> 16u) & 0xffffu) / 65535.0) * 2.0 - 1.0;
            Vector3 n(x, y, 1.0 - std::abs(x) - std::abs(y));
            if (n.z < 0.0) {
                const double oldX = n.x;
                n.x = (1.0 - std::abs(n.y)) * (oldX < 0.0 ? -1.0 : 1.0);
                n.y = (1.0 - std::abs(oldX)) * (n.y < 0.0 ? -1.0 : 1.0);
            }
            return n.Unit();
            };
        std::vector<TraceSurfaceSample> workSamples;
        std::vector<TargetPixel> targetPixels;
        workSamples.reserve(MaxChunkSamples);
        targetPixels.reserve(MaxChunkSamples);
        auto dispatchChunk = [&]() -> bool {
            const std::uint32_t sampleCount = static_cast<std::uint32_t>(workSamples.size());
            if (sampleCount == 0) return true;
            std::memcpy(sampleMapped, workSamples.data(), static_cast<std::size_t>(sampleCount) * sizeof(TraceSurfaceSample));
            VkCommandBufferAllocateInfo commandAllocate{};
            commandAllocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandAllocate.commandPool = CommandPool;
            commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandAllocate.commandBufferCount = 1;
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(Device, &commandAllocate, &commandBuffer) != VK_SUCCESS) return false;
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) {
                vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
                return false;
            }
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, tracePipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, tracePipelineLayout, 0, 1, &traceDescriptorSet, 0, nullptr);
            for (const BakePass& pass : bakePasses) {
                TracePushConstants push{};
                push.counts = { sampleCount, static_cast<std::uint32_t>(traceTriangles.size()), static_cast<std::uint32_t>(traceNodes.size()), actualLightCount };
                push.options = { static_cast<std::int32_t>(SamplesPerTexel), static_cast<std::int32_t>(MaxDiffuseBounces), pass.activeGroup, static_cast<std::int32_t>(pass.outputGroup * sampleCount) };
                push.offsets = { 0u, static_cast<std::uint32_t>(nodesBytes / sizeof(std::uint32_t)), static_cast<std::uint32_t>((nodesBytes + indicesBytes) / sizeof(std::uint32_t)), 0u };
                push.sunDirection = { static_cast<float>(sunDirection.x), static_cast<float>(sunDirection.y), static_cast<float>(sunDirection.z), sunAngularRadius };
                if (pass.activeGroup == -1 && bakedSun)
                    push.sunColorIntensity = { static_cast<float>(sunMultiplier.x), static_cast<float>(sunMultiplier.y), static_cast<float>(sunMultiplier.z), 1.0f };
                else if (pass.activeGroup == -2)
                    push.sunColorIntensity = { 1.0f, 1.0f, 1.0f, 1.0f };
                else
                    push.sunColorIntensity = { 0.0f, 0.0f, 0.0f, 0.0f };
                vkCmdPushConstants(commandBuffer, tracePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
                vkCmdDispatch(commandBuffer, (sampleCount + 63u) / 64u, 1, 1);
            }
            VkMemoryBarrier outputToHost{};
            outputToHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            outputToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            outputToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &outputToHost, 0, nullptr, 0, nullptr);
            if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
                vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
                return false;
            }
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &commandBuffer;
            const VkResult submitted = vkQueueSubmit(GraphicsQ, 1, &submit, VK_NULL_HANDLE);
            const VkResult waited = submitted == VK_SUCCESS ? vkQueueWaitIdle(GraphicsQ) : submitted;
            vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
            if (waited != VK_SUCCESS) {
                LOG_CATEGORY_ERROR("Lighting", "The software path tracing dispatch failed: {}", VkResultToString(waited));
                return false;
            }
            const float* output = static_cast<const float*>(outputMapped);
            for (std::size_t passIndex = 0; passIndex < bakePasses.size(); ++passIndex) {
                const std::uint32_t layerGroup = bakePasses[passIndex].outputGroup;
                for (std::uint32_t i = 0; i < sampleCount; ++i) {
                    const TargetPixel& target = targetPixels[i];
                    const std::uint64_t layerIndex = static_cast<std::uint64_t>(layerGroup) * pageCount + target.page;
                    const std::uint64_t pixelOffset = (layerIndex * pagePixelCount + target.pixel) * 4ull;
                    const std::size_t outputIndex = (passIndex * sampleCount + i) * 4u;
                    writeRadianceRGBM(static_cast<std::size_t>(pixelOffset), &output[outputIndex]);
                }
            }
            workSamples.clear();
            targetPixels.clear();
            return true;
            };

        struct CpuRasterTriangle {
            const CpuBakeTriangle* triangle = nullptr;
            std::array<double, 3> u{};
            std::array<double, 3> v{};
            double denominator = 0.0;
            int minX = 0, maxX = -1, minY = 0, maxY = -1;
            Vector3 faceNormal = Vector3::Zero();
            Vector3 worldDx = Vector3::Zero();
            Vector3 worldDy = Vector3::Zero();
        };
        std::vector<std::vector<CpuRasterTriangle>> rasterTrianglesByPage(pageCount);
        for (const CpuBakeTriangle& tri : cpuTriangles) {
            if (tri.page >= pageCount) continue;
            CpuRasterTriangle raster{};
            raster.triangle = &tri;
            raster.u = {
                (tri.lightmapUV[0].x * tri.lightmapScaleOffset[0] + tri.lightmapScaleOffset[2]) * resolution - 0.5,
                (tri.lightmapUV[1].x * tri.lightmapScaleOffset[0] + tri.lightmapScaleOffset[2]) * resolution - 0.5,
                (tri.lightmapUV[2].x * tri.lightmapScaleOffset[0] + tri.lightmapScaleOffset[2]) * resolution - 0.5
            };
            raster.v = {
                (tri.lightmapUV[0].y * tri.lightmapScaleOffset[1] + tri.lightmapScaleOffset[3]) * resolution - 0.5,
                (tri.lightmapUV[1].y * tri.lightmapScaleOffset[1] + tri.lightmapScaleOffset[3]) * resolution - 0.5,
                (tri.lightmapUV[2].y * tri.lightmapScaleOffset[1] + tri.lightmapScaleOffset[3]) * resolution - 0.5
            };
            raster.denominator = (raster.v[1] - raster.v[2]) * (raster.u[0] - raster.u[2]) +
                (raster.u[2] - raster.u[1]) * (raster.v[0] - raster.v[2]);
            if (std::abs(raster.denominator) < 1e-10) continue;
            raster.minX = (std::max)(0, static_cast<int>(std::ceil((std::min)({ raster.u[0], raster.u[1], raster.u[2] }) - 1e-6)));
            raster.maxX = (std::min)(static_cast<int>(resolution) - 1, static_cast<int>(std::floor((std::max)({ raster.u[0], raster.u[1], raster.u[2] }) + 1e-6)));
            raster.minY = (std::max)(0, static_cast<int>(std::ceil((std::min)({ raster.v[0], raster.v[1], raster.v[2] }) - 1e-6)));
            raster.maxY = (std::min)(static_cast<int>(resolution) - 1, static_cast<int>(std::floor((std::max)({ raster.v[0], raster.v[1], raster.v[2] }) + 1e-6)));
            if (raster.minX > raster.maxX || raster.minY > raster.maxY) continue;
            raster.faceNormal = (tri.p[1] - tri.p[0]).Cross(tri.p[2] - tri.p[0]).Unit();
            const double du1 = raster.u[1] - raster.u[0];
            const double dv1 = raster.v[1] - raster.v[0];
            const double du2 = raster.u[2] - raster.u[0];
            const double dv2 = raster.v[2] - raster.v[0];
            const double uvDeterminant = du1 * dv2 - du2 * dv1;
            if (std::abs(uvDeterminant) > 1e-12) {
                raster.worldDx = ((tri.p[1] - tri.p[0]) * dv2 - (tri.p[2] - tri.p[0]) * dv1) / uvDeterminant;
                raster.worldDy = ((tri.p[2] - tri.p[0]) * du1 - (tri.p[1] - tri.p[0]) * du2) / uvDeterminant;
            }
            rasterTrianglesByPage[tri.page].push_back(raster);
        }

        bool rasterizeOk = true;
        constexpr std::size_t RowsPerTask = 16u;
        const std::size_t rasterWorkers = (std::max<std::size_t>)(1u,
            (std::min<std::size_t>)((std::max)(std::thread::hardware_concurrency(), 1u), 8u));
        const std::size_t rowsPerBatch = (std::min<std::size_t>)(resolution, rasterWorkers * RowsPerTask);
        struct RasterRowSamples {
            std::vector<TraceSurfaceSample> samples;
            std::vector<TargetPixel> targets;
        };
        for (std::uint32_t page = 0; page < pageCount && rasterizeOk; ++page) {
            const std::size_t pageCoverageOffset = static_cast<std::size_t>(page) * pagePixelCount;
            const auto& pageTriangles = rasterTrianglesByPage[page];
            for (std::size_t bandStart = 0; bandStart < resolution && rasterizeOk; bandStart += rowsPerBatch) {
                const std::size_t bandHeight = (std::min<std::size_t>)(rowsPerBatch, resolution - bandStart);
                std::vector<RasterRowSamples> rows(bandHeight);
                for (auto& row : rows) {
                    row.samples.reserve(resolution);
                    row.targets.reserve(resolution);
                }
                ParallelForRanges(bandHeight, RowsPerTask, [&](std::size_t rowBegin, std::size_t rowEnd) {
                    for (std::size_t rowIndex = rowBegin; rowIndex < rowEnd; ++rowIndex) {
                        const int y = static_cast<int>(bandStart + rowIndex);
                        RasterRowSamples& row = rows[rowIndex];
                        for (const CpuRasterTriangle& raster : pageTriangles) {
                            if (y < raster.minY || y > raster.maxY) continue;
                            const CpuBakeTriangle& tri = *raster.triangle;
                            for (int x = raster.minX; x <= raster.maxX; ++x) {
                                const double w0 = ((raster.v[1] - raster.v[2]) * (x - raster.u[2]) + (raster.u[2] - raster.u[1]) * (y - raster.v[2])) / raster.denominator;
                                const double w1 = ((raster.v[2] - raster.v[0]) * (x - raster.u[2]) + (raster.u[0] - raster.u[2]) * (y - raster.v[2])) / raster.denominator;
                                const double w2 = 1.0 - w0 - w1;
                                if (w0 < -1e-5 || w1 < -1e-5 || w2 < -1e-5) continue;
                                const double w0dx = (raster.v[1] - raster.v[2]) / raster.denominator;
                                const double w0dy = (raster.u[2] - raster.u[1]) / raster.denominator;
                                const double w1dx = (raster.v[2] - raster.v[0]) / raster.denominator;
                                const double w1dy = (raster.u[0] - raster.u[2]) / raster.denominator;
                                const double w2dx = -w0dx - w1dx;
                                const double w2dy = -w0dy - w1dy;
                                const bool safeSubtexelJitter = w0 >= 0.5 * (std::abs(w0dx) + std::abs(w0dy)) + 1e-5 && w1 >= 0.5 * (std::abs(w1dx) + std::abs(w1dy)) + 1e-5 && w2 >= 0.5 * (std::abs(w2dx) + std::abs(w2dy)) + 1e-5;
                                const std::size_t pixel = static_cast<std::size_t>(y) * resolution + static_cast<std::size_t>(x);
                                const std::size_t coverageIndex = pageCoverageOffset + pixel;
                                if (coverage[coverageIndex] != 0u) continue;
                                coverage[coverageIndex] = 1u;
                                normalGuide[coverageIndex] = encodeOctahedralNormal(raster.faceNormal);
                                ownerGuide[coverageIndex] = tri.ownerId;
                                const Vector3 worldPosition = tri.p[0] * w0 + tri.p[1] * w1 + tri.p[2] * w2;
                                TraceSurfaceSample sample{};
                                sample.position = { static_cast<float>(worldPosition.x), static_cast<float>(worldPosition.y), static_cast<float>(worldPosition.z), 1.0f };
                                sample.normal = { static_cast<float>(raster.faceNormal.x), static_cast<float>(raster.faceNormal.y), static_cast<float>(raster.faceNormal.z), 0.0f };
                                const Vector3 worldDx = safeSubtexelJitter ? raster.worldDx : Vector3::Zero();
                                const Vector3 worldDy = safeSubtexelJitter ? raster.worldDy : Vector3::Zero();
                                sample.worldDx = { static_cast<float>(worldDx.x), static_cast<float>(worldDx.y), static_cast<float>(worldDx.z), 0.0f };
                                sample.worldDy = { static_cast<float>(worldDy.x), static_cast<float>(worldDy.y), static_cast<float>(worldDy.z), 0.0f };
                                sample.target = { page, static_cast<std::uint32_t>(pixel), static_cast<std::uint32_t>(pixel) ^ (page * 1664525u), 0u };
                                row.samples.push_back(sample);
                                row.targets.push_back({ page, static_cast<std::uint32_t>(pixel) });
                            }
                        }
                    }
                    }, 1u);

                for (const RasterRowSamples& row : rows) {
                    for (std::size_t i = 0; i < row.samples.size(); ++i) {
                        workSamples.push_back(row.samples[i]);
                        targetPixels.push_back(row.targets[i]);
                        if (workSamples.size() >= MaxChunkSamples && !dispatchChunk()) {
                            rasterizeOk = false;
                            break;
                        }
                    }
                    if (!rasterizeOk) break;
                }
            }
        }
        if (rasterizeOk && !dispatchChunk()) rasterizeOk = false;
        cleanupTraceResources();
        if (!rasterizeOk) {
            LOG_CATEGORY_ERROR("Lighting", "GPU path tracing failed while baking '{}'", level.GetName());
            return false;
        }
        {
            constexpr int spatialKernel[5] = { 1, 4, 6, 4, 1 };
            std::vector<unsigned char> sourcePage;
            try { sourcePage.resize(pageBytes); }
            catch (const std::bad_alloc&) {
                LOG_CATEGORY_ERROR("Lighting", "Could not allocate the lightmap denoiser scratch page");
                return false;
            }
            auto decodeRGBM = [](const unsigned char* encoded, float out[3]) {
                const float multiplier = (static_cast<float>(encoded[3]) / 255.0f) * 16.0f;
                out[0] = (static_cast<float>(encoded[0]) / 255.0f) * multiplier;
                out[1] = (static_cast<float>(encoded[1]) / 255.0f) * multiplier;
                out[2] = (static_cast<float>(encoded[2]) / 255.0f) * multiplier;
                };
            for (std::uint32_t arrayLayer = 0; arrayLayer < layerCount; ++arrayLayer) {
                const std::uint32_t page = arrayLayer % pageCount;
                const std::size_t pageCoverageOffset = static_cast<std::size_t>(page) * pagePixelCount;
                const std::size_t pageAtlasOffset = static_cast<std::size_t>(arrayLayer) * pageBytes;
                std::memcpy(sourcePage.data(), atlasPixels.data() + pageAtlasOffset, pageBytes);
                ParallelForRanges(resolution, 4u, [&](std::size_t rowBegin, std::size_t rowEnd) {
                    for (std::size_t y = rowBegin; y < rowEnd; ++y) {
                        for (std::uint32_t x = 0; x < resolution; ++x) {
                            const std::size_t pixel = y * resolution + x;
                            if (coverage[pageCoverageOffset + pixel] == 0u) continue;
                            const Vector3 centerNormal = decodeOctahedralNormal(normalGuide[pageCoverageOffset + pixel]);
                            float centerRadiance[3];
                            decodeRGBM(sourcePage.data() + pixel * 4u, centerRadiance);
                            const float centerLuma = centerRadiance[0] * 0.2126f + centerRadiance[1] * 0.7152f + centerRadiance[2] * 0.0722f;
                            const float rangeSigma = (std::max)(0.05f, centerLuma * 0.35f);
                            float sum[3] = { 0.0f, 0.0f, 0.0f };
                            float weightSum = 0.0f;
                            for (int dy = -2; dy <= 2; ++dy) {
                                const int ny = static_cast<int>(y) + dy;
                                if (ny < 0 || ny >= static_cast<int>(resolution)) continue;
                                for (int dx = -2; dx <= 2; ++dx) {
                                    const int nx = static_cast<int>(x) + dx;
                                    if (nx < 0 || nx >= static_cast<int>(resolution)) continue;
                                    const std::size_t neighborPixel = static_cast<std::size_t>(ny) * resolution + static_cast<std::size_t>(nx);
                                    if (coverage[pageCoverageOffset + neighborPixel] == 0u) continue;
                                    if (ownerGuide[pageCoverageOffset + neighborPixel] != ownerGuide[pageCoverageOffset + pixel]) continue;
                                    const Vector3 neighborNormal = decodeOctahedralNormal(normalGuide[pageCoverageOffset + neighborPixel]);
                                    const double normalDot = centerNormal.Dot(neighborNormal);
                                    if (normalDot < 0.75) continue;
                                    const float normalWeight = static_cast<float>((normalDot - 0.75) / 0.25);
                                    const float spatialWeight = static_cast<float>(spatialKernel[dx + 2] * spatialKernel[dy + 2]);
                                    const float weight = spatialWeight * normalWeight * normalWeight;
                                    float radiance[3];
                                    decodeRGBM(sourcePage.data() + neighborPixel * 4u, radiance);
                                    const float neighborLuma = radiance[0] * 0.2126f + radiance[1] * 0.7152f + radiance[2] * 0.0722f;
                                    const float edgeWeight = std::exp(-std::abs(neighborLuma - centerLuma) / rangeSigma);
                                    const float finalWeight = weight * edgeWeight;
                                    sum[0] += radiance[0] * finalWeight;
                                    sum[1] += radiance[1] * finalWeight;
                                    sum[2] += radiance[2] * finalWeight;
                                    weightSum += finalWeight;
                                }
                            }
                            if (weightSum > 0.0f) {
                                const float filtered[3] = { sum[0] / weightSum, sum[1] / weightSum, sum[2] / weightSum };
                                writeRadianceRGBM(pageAtlasOffset + pixel * 4u, filtered);
                            }
                        }
                    }
                    }, 64u);
            }
        }
        {
            const int dilationRadius = static_cast<int>((std::max)(gutter, 2u));
            const int radiusSquared = dilationRadius * dilationRadius;
            const std::uint8_t invalidDistance = static_cast<std::uint8_t>(radiusSquared + 1);
            std::vector<std::uint32_t> dilationSource;
            std::vector<std::uint8_t> dilationDistance;
            std::vector<unsigned char> sourcePage;
            try {
                dilationSource.resize(pagePixelCount);
                dilationDistance.resize(pagePixelCount);
                sourcePage.resize(pageBytes);
            }
            catch (const std::bad_alloc&) {
                LOG_CATEGORY_ERROR("Lighting", "Could not allocate lightmap seam-dilation scratch buffers");
                return false;
            }
            const std::uint32_t groupCount = pageCount == 0u ? 0u : layerCount / pageCount;
            for (std::uint32_t page = 0; page < pageCount; ++page) {
                const std::size_t pageCoverageOffset = static_cast<std::size_t>(page) * pagePixelCount;
                std::fill(dilationSource.begin(), dilationSource.end(), UINT32_MAX);
                std::fill(dilationDistance.begin(), dilationDistance.end(), invalidDistance);
                ParallelForRanges(requests.size(), 1u, [&](std::size_t requestBegin, std::size_t requestEnd) {
                    for (std::size_t requestIndex = requestBegin; requestIndex < requestEnd; ++requestIndex) {
                        const MeshRequest& request = requests[requestIndex];
                        if (request.page != page) continue;
                        const int minX = static_cast<int>(request.outer.x);
                        const int minY = static_cast<int>(request.outer.y);
                        const int maxX = (std::min)(static_cast<int>(resolution), static_cast<int>(request.outer.x + request.outer.w));
                        const int maxY = (std::min)(static_cast<int>(resolution), static_cast<int>(request.outer.y + request.outer.h));
                        for (int y = minY; y < maxY; ++y) {
                            for (int x = minX; x < maxX; ++x) {
                                const std::uint32_t pixel = static_cast<std::uint32_t>(y * static_cast<int>(resolution) + x);
                                if (coverage[pageCoverageOffset + pixel] != 0u) {
                                    dilationSource[pixel] = pixel;
                                    dilationDistance[pixel] = 0u;
                                }
                                else {
                                    dilationSource[pixel] = UINT32_MAX;
                                    dilationDistance[pixel] = invalidDistance;
                                }
                            }
                        }
                        for (int y = minY; y < maxY; ++y) {
                            for (int x = minX; x < maxX; ++x) {
                                const std::uint32_t sourcePixel = static_cast<std::uint32_t>(y * static_cast<int>(resolution) + x);
                                if (coverage[pageCoverageOffset + sourcePixel] == 0u) continue;
                                const int scanMinX = (std::max)(minX, x - dilationRadius);
                                const int scanMaxX = (std::min)(maxX - 1, x + dilationRadius);
                                const int scanMinY = (std::max)(minY, y - dilationRadius);
                                const int scanMaxY = (std::min)(maxY - 1, y + dilationRadius);
                                for (int sy = scanMinY; sy <= scanMaxY; ++sy) {
                                    for (int sx = scanMinX; sx <= scanMaxX; ++sx) {
                                        const int dx = sx - x, dy = sy - y;
                                        const int distanceSquared = dx * dx + dy * dy;
                                        if (distanceSquared > radiusSquared) continue;
                                        const std::uint32_t targetPixel = static_cast<std::uint32_t>(sy * static_cast<int>(resolution) + sx);
                                        if (coverage[pageCoverageOffset + targetPixel] != 0u) continue;
                                        if (distanceSquared < dilationDistance[targetPixel]) {
                                            dilationDistance[targetPixel] = static_cast<std::uint8_t>(distanceSquared);
                                            dilationSource[targetPixel] = sourcePixel;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    }, 8u);

                for (std::uint32_t group = 0; group < groupCount; ++group) {
                    const std::uint32_t arrayLayer = group * pageCount + page;
                    const std::size_t pageAtlasOffset = static_cast<std::size_t>(arrayLayer) * pageBytes;
                    std::memcpy(sourcePage.data(), atlasPixels.data() + pageAtlasOffset, pageBytes);
                    ParallelForRanges(pagePixelCount, 4096u, [&](std::size_t begin, std::size_t end) {
                        for (std::size_t pixel = begin; pixel < end; ++pixel) {
                            if (coverage[pageCoverageOffset + pixel] != 0u || dilationSource[pixel] == UINT32_MAX) continue;
                            const std::size_t sourcePixel = dilationSource[pixel];
                            std::memcpy(atlasPixels.data() + pageAtlasOffset + pixel * 4u,
                                sourcePage.data() + sourcePixel * 4u, 4u);
                        }
                        }, 4096u);
                }
            }
        }

        GpuTexture newAtlasArray{};
        const auto generateLayer = [&atlasPixels, pageBytes](std::uint32_t layer, unsigned char* pixels) { std::memcpy(pixels, atlasPixels.data() + static_cast<std::size_t>(layer) * pageBytes, pageBytes); };
        if (!CreateTextureArrayFromGeneratedPixels(resolution, resolution, layerCount, maxAtlasBytes, generateLayer, newAtlasArray)) {
            LOG_CATEGORY_ERROR("Lighting", "Failed to upload the {}-layer {}x{} path-traced lightmap array (budget {} MiB)", layerCount, resolution, resolution, maxAtlasBytes / MiB);
            return false;
        }
        if (LightmapDescriptorPool == VK_NULL_HANDLE || LightmapArrayDescriptorSetLayout == VK_NULL_HANDLE) {
            DestroyTexture(newAtlasArray);
            LOG_CATEGORY_ERROR("Lighting", "Lightmap-array descriptor resources are not initialized");
            return false;
        }
        if (LightmapArrayDescriptorSet == VK_NULL_HANDLE) {
            VkDescriptorSetAllocateInfo allocate{};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = LightmapDescriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &LightmapArrayDescriptorSetLayout;
            if (vkAllocateDescriptorSets(Device, &allocate, &LightmapArrayDescriptorSet) != VK_SUCCESS) {
                DestroyTexture(newAtlasArray);
                LOG_CATEGORY_ERROR("Lighting", "Could not allocate the global lightmap-array descriptor set");
                return false;
            }
        }
        VkDescriptorImageInfo arrayImageInfo{};
        arrayImageInfo.sampler = newAtlasArray.sampler;
        arrayImageInfo.imageView = newAtlasArray.view;
        arrayImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet arrayWrite{};
        arrayWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        arrayWrite.dstSet = LightmapArrayDescriptorSet;
        arrayWrite.dstBinding = 0;
        arrayWrite.descriptorCount = 1;
        arrayWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        arrayWrite.pImageInfo = &arrayImageInfo;
        vkUpdateDescriptorSets(Device, 1, &arrayWrite, 0, nullptr);
        BakedLightingAtlasArrayTexture = newAtlasArray;
        newAtlasArray = {};
        BakedLightingAtlasLayers = layerCount;
        BakedLightingAtlasPages = pageCount;
        BakedLightingHasStaticSun = hasStaticSun;
        BakedStaticLights.clear();
        BakedStaticLights.reserve(staticLights.size());
        for (const Level::Light* light : staticLights) {
            BakedStaticLightInfo info{};
            info.light = light;
            const Vector3 bakedPosition = light ? light->GetPosition() : Vector3::Zero();
            info.bakedPosition = { static_cast<float>(bakedPosition.x), static_cast<float>(bakedPosition.y), static_cast<float>(bakedPosition.z) };
            BakedStaticLights.push_back(info);
        }
        BakedLightmapAssignments = std::move(newAssignments);
        BakedLightingLevelUID = level.GetId();
        BakedLightingCacheAttemptedLevelUID = level.GetId();
        {
            const std::filesystem::path cachePath =
                std::filesystem::path("Engine/BakedLights") / (level.GetName() + ".lightmapbin");
            std::error_code cacheDirectoryError;
            std::filesystem::create_directories(cachePath.parent_path(), cacheDirectoryError);
            std::vector<LightmapCacheAssignment> cacheAssignments;
            std::vector<LightmapCacheLight> cacheLights;
            if (!cacheDirectoryError) {
                try {
                    cacheAssignments.reserve(BakedLightmapAssignments.size());
                    cacheLights.reserve(staticLights.size());
                    for (const MeshRequest& request : requests) {
                        const auto* mesh = dynamic_cast<const Level::Mesh*>(request.object);
                        const auto assignmentIt = BakedLightmapAssignments.find(request.object);
                        if (!mesh || assignmentIt == BakedLightmapAssignments.end()) continue;
                        LightmapCacheAssignment record{};
                        record.objectIndex = request.objectIndex;
                        record.meshType = static_cast<std::uint32_t>(mesh->GetMeshType());
                        record.mobility = static_cast<std::uint32_t>(mesh->GetMobility());
                        record.page = assignmentIt->second.page;
                        std::copy(assignmentIt->second.scaleOffset.begin(), assignmentIt->second.scaleOffset.end(), record.scaleOffset);
                        const Vector3 position = mesh->GetPosition();
                        const Vector3 rotation = mesh->GetRotation();
                        const Vector3 size = mesh->GetSize();
                        record.position[0] = static_cast<float>(position.x);
                        record.position[1] = static_cast<float>(position.y);
                        record.position[2] = static_cast<float>(position.z);
                        record.rotation[0] = static_cast<float>(rotation.x);
                        record.rotation[1] = static_cast<float>(rotation.y);
                        record.rotation[2] = static_cast<float>(rotation.z);
                        record.size[0] = static_cast<float>(size.x);
                        record.size[1] = static_cast<float>(size.y);
                        record.size[2] = static_cast<float>(size.z);
                        record.customMeshHash = HashAssetString(mesh->GetCustomMesh());
                        cacheAssignments.push_back(record);
                    }
                    for (const BakedStaticLightInfo& light : BakedStaticLights) {
                        LightmapCacheLight record{};
                        std::copy(light.bakedPosition.begin(), light.bakedPosition.end(), record.position);
                        cacheLights.push_back(record);
                    }
                    LightmapCacheHeader header{};
                    std::memcpy(header.magic, "EELMAP01", 8);
                    header.version = 3u;
                    header.resolution = resolution;
                    header.pageCount = pageCount;
                    header.layerCount = layerCount;
                    header.assignmentCount = static_cast<std::uint32_t>(cacheAssignments.size());
                    header.staticLightCount = static_cast<std::uint32_t>(cacheLights.size());
                    header.hasStaticSun = hasStaticSun ? 1u : 0u;
                    const Vector3 cachedSunDirection = level.GetSunDirection();
                    header.sunDirection[0] = static_cast<float>(cachedSunDirection.x);
                    header.sunDirection[1] = static_cast<float>(cachedSunDirection.y);
                    header.sunDirection[2] = static_cast<float>(cachedSunDirection.z);
                    header.pixelBytes = static_cast<std::uint64_t>(atlasPixels.size());
                    header.sourceSignature = bakeSourceSignature;
                    std::filesystem::path temporaryPath = cachePath;
                    temporaryPath += ".tmp";
                    bool wroteCache = false;
                    {
                        std::ofstream cache(temporaryPath, std::ios::binary | std::ios::trunc);
                        if (cache.is_open()) {
                            cache.write(reinterpret_cast<const char*>(&header), sizeof(header));
                            if (!cacheAssignments.empty())
                                cache.write(reinterpret_cast<const char*>(cacheAssignments.data()),
                                    static_cast<std::streamsize>(cacheAssignments.size() * sizeof(LightmapCacheAssignment)));
                            if (!cacheLights.empty())
                                cache.write(reinterpret_cast<const char*>(cacheLights.data()),
                                    static_cast<std::streamsize>(cacheLights.size() * sizeof(LightmapCacheLight)));
                            if (!atlasPixels.empty())
                                cache.write(reinterpret_cast<const char*>(atlasPixels.data()),
                                    static_cast<std::streamsize>(atlasPixels.size()));
                            cache.close();
                            wroteCache = static_cast<bool>(cache);
                        }
                    }
                    if (wroteCache) {
                        std::error_code renameError;
                        std::filesystem::rename(temporaryPath, cachePath, renameError);
                        if (renameError) {
                            std::error_code removeError;
                            std::filesystem::remove(cachePath, removeError);
                            renameError.clear();
                            std::filesystem::rename(temporaryPath, cachePath, renameError);
                        }
                        if (renameError) {
                            std::filesystem::remove(temporaryPath, renameError);
                            LOG_CATEGORY_WARN("Lighting", "Could not install baked lightmap cache '{}'", cachePath.generic_string());
                        }
                        else
                            LOG_CATEGORY_INFO("Lighting", "Saved {} MiB of baked lighting to '{}'", static_cast<std::uint64_t>(atlasPixels.size()) / MiB, cachePath.generic_string());
                    }
                    else {
                        std::error_code removeError;
                        std::filesystem::remove(temporaryPath, removeError);
                        LOG_CATEGORY_WARN("Lighting", "Could not write baked lightmap cache '{}'", cachePath.generic_string());
                    }
                }
                catch (const std::bad_alloc&) {
                    LOG_CATEGORY_WARN("Lighting", "Could not allocate metadata for the baked-lighting cache; the bake remains active until shutdown");
                }
            }
            else
                LOG_CATEGORY_WARN("Lighting", "Could not create baked-lighting cache directory: {}", cacheDirectoryError.message());
        }
        if (undersampledInstances != 0)
            LOG_CATEGORY_WARN("Lighting", "{} mesh instance(s) were clamped to atlas resolution {}. Increase texelsPerWorldUnit with a larger page resolution, or split large surfaces into separate charts/pages.", undersampledInstances, resolution);
        const std::filesystem::path metadataPath = std::filesystem::path("Engine/BakedLights") / (level.GetName() + ".lightdata");
        std::error_code metadataError;
        std::filesystem::create_directories(metadataPath.parent_path(), metadataError);
        if (metadataError)
            LOG_CATEGORY_WARN("Lighting", "Could not create bake metadata directory: {}", metadataError.message());
        else {
            std::ofstream metadata(metadataPath, std::ios::out | std::ios::trunc);
            if (!metadata.is_open())
                LOG_CATEGORY_WARN("Lighting", "Could not write bake metadata: {}", metadataPath.generic_string());
            else {
                std::size_t bakedPointLights = 0;
                for (std::size_t i = 0; i < actualLightCount; ++i)
                    if (traceLights[i].meta[0] == 0u) ++bakedPointLights;
                metadata << "# ENERGY ENGINE PATH-TRACED LIGHT BAKE\n";
                metadata << "version = " << FileParsers::LightBakeVersion << "\n";
                metadata << "level = " << level.GetName() << "\n";
                metadata << "kind = software_bvh_path_trace\n";
                metadata << "format = rgbm_rgba8_unorm\n";
                metadata << "rgbm_range = 16.0\n";
                metadata << "atlas_resolution = " << resolution << "\n";
                metadata << "atlas_pages = " << pageCount << "\n";
                metadata << "atlas_layers = " << layerCount << "\n";
                metadata << "packed_instances = " << BakedLightmapAssignments.size() << "\n";
                metadata << "baked_point_lights = " << bakedPointLights << "\n";
                metadata << "static_point_lights = " << staticLights.size() << "\n";
                metadata << "sun_type = " << static_cast<unsigned int>(level.GetSunType()) << "\n";
                metadata << "samples_per_texel = " << SamplesPerTexel << "\n";
                metadata << "diffuse_bounces = " << MaxDiffuseBounces << "\n";
                metadata.close();
                if (!metadata) LOG_CATEGORY_WARN("Lighting", "Could not finish writing bake metadata: {}", metadataPath.generic_string());
            }
        }
        LOG_CATEGORY_INFO("Lighting", "Path-traced {} eligible mesh instances into {} atlas page(s), {} total array layers, {} triangles, {} Static point-light response(s), {} samples/texel and {} diffuse bounce(s); atlas budget <= {} MiB",
            BakedLightmapAssignments.size(), BakedLightingAtlasPages, BakedLightingAtlasLayers, traceTriangles.size(), staticLights.size(), SamplesPerTexel, MaxDiffuseBounces, maxAtlasBytes / MiB);
        return true;
    }

    bool Vulkan::BakeLighting(std::uint32_t resolution, float texelsPerWorldUnit, std::uint64_t maxAtlasBytes, std::uint32_t maxAtlasPages, std::uint32_t samplesPerTexel) {
        const auto level = PrimaryLevel.lock();
        if (!level) {
            LOG_CATEGORY_ERROR("Lighting", "No level was assigned for the compatibility BakeLighting overload");
            return false;
        }
        return BakeLighting(*level, resolution, texelsPerWorldUnit, maxAtlasBytes, maxAtlasPages, samplesPerTexel);
    }

    std::uint64_t Vulkan::ComputeBakedLightingSourceSignature(const Level& level) const {
        std::uint64_t hash = 1469598103934665603ull;
        auto mixBytes = [&hash](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i) {
                hash ^= static_cast<std::uint64_t>(bytes[i]);
                hash *= 1099511628211ull;
            }
            };
        auto mixScalar = [&mixBytes](const auto& value) {
            mixBytes(&value, sizeof(value));
            };
        auto mixString = [&mixBytes, &mixScalar](std::string_view value) {
            const std::uint64_t length = static_cast<std::uint64_t>(value.size());
            mixScalar(length);
            if (!value.empty()) mixBytes(value.data(), value.size());
            };
        std::unordered_map<std::string, std::uint64_t> textureFileSignatures;
        auto mixTextureFile = [&mixString, &mixScalar, &textureFileSignatures](const std::string& path) {
            mixString(path);
            if (path.empty()) return;
            const auto cached = textureFileSignatures.find(path);
            if (cached != textureFileSignatures.end()) {
                mixScalar(cached->second);
                return;
            }
            std::uint64_t fileHash = 1469598103934665603ull;
            auto hashFileBytes = [&fileHash](const void* data, std::size_t size) {
                const auto* bytes = static_cast<const unsigned char*>(data);
                for (std::size_t i = 0; i < size; ++i) {
                    fileHash ^= static_cast<std::uint64_t>(bytes[i]);
                    fileHash *= 1099511628211ull;
                }
                };
            std::ifstream file(path, std::ios::binary);
            const std::uint8_t present = file.is_open() ? 1u : 0u;
            hashFileBytes(&present, sizeof(present));
            if (file.is_open()) {
                std::error_code sizeError;
                const std::uint64_t fileSize = std::filesystem::file_size(path, sizeError);
                const std::uint64_t recordedSize = sizeError ? UINT64_MAX : fileSize;
                hashFileBytes(&recordedSize, sizeof(recordedSize));
                std::array<char, 8192> buffer{};
                while (file) {
                    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    const std::streamsize count = file.gcount();
                    if (count > 0) hashFileBytes(buffer.data(), static_cast<std::size_t>(count));
                }
                const std::uint8_t readOk = file.eof() ? 1u : 0u;
                hashFileBytes(&readOk, sizeof(readOk));
            }
            if (fileHash == 0u) fileHash = 1u;
            textureFileSignatures.emplace(path, fileHash);
            mixScalar(fileHash);
            };
        constexpr std::uint32_t signatureRevision = 3u;
        mixScalar(signatureRevision);
        const auto& objects = level.GetObjects();
        for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
            const auto* mesh = objects[objectIndex] ? dynamic_cast<const Level::Mesh*>(objects[objectIndex].get()) : nullptr;
            if (!mesh || mesh->GetMobility() == Level::Mesh::Mobility::Movable) continue;
            const std::uint64_t index64 = static_cast<std::uint64_t>(objectIndex);
            mixScalar(index64);
            const std::uint32_t meshType = static_cast<std::uint32_t>(mesh->GetMeshType());
            const std::uint32_t mobility = static_cast<std::uint32_t>(mesh->GetMobility());
            mixScalar(meshType);
            mixScalar(mobility);
            mixString(mesh->GetCustomMesh());
            const Vector3 position = mesh->GetPosition();
            const Vector3 rotation = mesh->GetRotation();
            const Vector3 size = mesh->GetSize();
            mixScalar(position.x); mixScalar(position.y); mixScalar(position.z);
            mixScalar(rotation.x); mixScalar(rotation.y); mixScalar(rotation.z);
            mixScalar(size.x); mixScalar(size.y); mixScalar(size.z);
            MeshHandle meshHandle = 0;
            switch (mesh->GetMeshType()) {
            case Level::Mesh::Cube:        meshHandle = CubeMeshHandle; break;
            case Level::Mesh::Plane:       meshHandle = PlaneMeshHandle; break;
            case Level::Mesh::Sphere:      meshHandle = SphereMeshHandle; break;
            case Level::Mesh::Cylinder:    meshHandle = CylinderMeshHandle; break;
            case Level::Mesh::Wedge:       meshHandle = WedgeMeshHandle; break;
            case Level::Mesh::CornerWedge: meshHandle = CornerWedgeMeshHandle; break;
            case Level::Mesh::Cone:        meshHandle = ConeMeshHandle; break;
            case Level::Mesh::Pyramid:     meshHandle = PyramidMeshHandle; break;
            case Level::Mesh::Custom:      meshHandle = mesh->GetMeshAssetId(); break;
            default: break;
            }
            const auto gpuMeshIt = Meshes.find(meshHandle);
            const bool hasMeshData = meshHandle != 0 && gpuMeshIt != Meshes.end();
            const std::uint8_t hasMeshDataByte = hasMeshData ? 1u : 0u;
            mixScalar(hasMeshDataByte);
            if (!hasMeshData) continue;
            const GpuMesh& gpuMesh = gpuMeshIt->second;
            const std::uint64_t subMeshCount = static_cast<std::uint64_t>(gpuMesh.cpuSubMeshes.size());
            mixScalar(subMeshCount);
            for (std::size_t subIndex = 0; subIndex < gpuMesh.cpuSubMeshes.size(); ++subIndex) {
                const SubMeshData& subMesh = gpuMesh.cpuSubMeshes[subIndex];
                const std::uint64_t vertexCount = static_cast<std::uint64_t>(subMesh.vertices.size());
                const std::uint64_t indexCount = static_cast<std::uint64_t>(subMesh.indices.size());
                mixScalar(vertexCount);
                mixScalar(indexCount);
                for (const MeshVertex& vertex : subMesh.vertices) {
                    for (float component : vertex.position) mixScalar(component);
                    for (float component : vertex.lightmapUV) mixScalar(component);
                }
                for (std::uint32_t index : subMesh.indices) mixScalar(index);
                MaterialHandle materialHandle = mesh->GetMaterialId(static_cast<std::uint32_t>(subIndex));
                if (materialHandle == 0 && subIndex < gpuMesh.subMeshes.size())
                    materialHandle = gpuMesh.subMeshes[subIndex].defaultMaterial;
                std::string texturePath = subMesh.defaultMaterialTexture;
                const auto materialIt = Materials.find(materialHandle);
                if (materialHandle != 0 && materialIt != Materials.end()) {
                    const MaterialResource& material = materialIt->second;
                    for (std::size_t component = 0; component < 3; ++component)
                        mixScalar(material.gpuData.color[component]);
                    const TextureHandle textureHandle = material.textures[0];
                    if (textureHandle != 0) {
                        for (const auto& [path, loadedHandle] : FileTextures) {
                            if (loadedHandle == textureHandle) {
                                texturePath = path;
                                break;
                            }
                        }
                    }
                }
                else {
                    for (std::size_t component = 0; component < 3; ++component)
                        mixScalar(subMesh.defaultMaterialColor[component]);
                }
                mixTextureFile(texturePath);
            }
        }
        const auto& lights = level.GetLevelLights();
        std::size_t staticLightCount = 0;
        std::size_t bakedLightCount = 0;
        for (const auto& ownedLight : lights) {
            if (!ownedLight) continue;
            if (ownedLight->GetType() == Level::Light::Type::Static) ++staticLightCount;
            else if (ownedLight->GetType() == Level::Light::Type::Baked) ++bakedLightCount;
        }
        const std::uint64_t hashedStaticLightCount = static_cast<std::uint64_t>((std::min)(staticLightCount, static_cast<std::size_t>(MaxBakedStaticLights)));
        const std::uint64_t hashedBakedLightCount = static_cast<std::uint64_t>(bakedLightCount);
        mixScalar(hashedStaticLightCount);
        mixScalar(hashedBakedLightCount);
        std::size_t hashedStaticLights = 0;
        for (const auto& ownedLight : lights) {
            if (!ownedLight) continue;
            const auto type = ownedLight->GetType();
            if (type == Level::Light::Type::Static) {
                if (hashedStaticLights++ >= MaxBakedStaticLights) continue;
                const std::uint32_t typeValue = static_cast<std::uint32_t>(type);
                mixScalar(typeValue);
                const Vector3 position = ownedLight->GetPosition();
                mixScalar(position.x); mixScalar(position.y); mixScalar(position.z);
            }
            else if (type == Level::Light::Type::Baked) {
                const std::uint32_t typeValue = static_cast<std::uint32_t>(type);
                mixScalar(typeValue);
                const Vector3 position = ownedLight->GetPosition();
                mixScalar(position.x); mixScalar(position.y); mixScalar(position.z);
                const bool enabled = ownedLight->IsEnabled();
                mixScalar(enabled);
                const Vector3 color = ownedLight->GetColor();
                mixScalar(color.x); mixScalar(color.y); mixScalar(color.z);
                const double intensity = static_cast<double>(ownedLight->GetIntensity());
                const double radius = static_cast<double>(ownedLight->GetEffectiveRadius());
                mixScalar(intensity);
                mixScalar(radius);
            }
        }
        const auto sunType = level.GetSunType();
        const bool hasStaticSun = sunType == Level::SunType::Static;
        const bool hasBakedSun = sunType == Level::SunType::Baked;
        mixScalar(hasStaticSun);
        mixScalar(hasBakedSun);
        if (hasStaticSun || hasBakedSun) {
            const Vector3 sunDirection = level.GetSunDirection();
            mixScalar(sunDirection.x); mixScalar(sunDirection.y); mixScalar(sunDirection.z);
            const auto* sky = level.GetCurrentSky();
            const bool hasSky = sky != nullptr;
            mixScalar(hasSky);
            if (sky) {
                const double sunSize = static_cast<double>(sky->GetSunSize());
                mixScalar(sunSize);
                if (hasBakedSun) {
                    const Vector3 sunColor = sky->GetSunColorMultiplier();
                    mixScalar(sunColor.x); mixScalar(sunColor.y); mixScalar(sunColor.z);
                }
            }
        }
        return hash == 0u ? 1u : hash;
    }

    bool Vulkan::TryLoadBakedLighting(const Level& level) {
        if (!IsInitialized() || !level.AssetsAcquired) return false;
        if (BakedLightingLevelUID == level.GetId() && BakedLightingAtlasArrayTexture.image != VK_NULL_HANDLE)
            return true;
        if (BakedLightingCacheAttemptedLevelUID == level.GetId())
            return false;
        if (vkDeviceWaitIdle(Device) != VK_SUCCESS) {
            LOG_CATEGORY_WARN("Lighting", "Could not synchronize before loading a baked-lighting cache");
            return false;
        }
        BakedLightingCacheAttemptedLevelUID = level.GetId();
        auto writeLightmapDescriptor = [&](const GpuTexture& texture) -> bool {
            if (texture.image == VK_NULL_HANDLE || LightmapDescriptorPool == VK_NULL_HANDLE ||
                LightmapArrayDescriptorSetLayout == VK_NULL_HANDLE)
                return false;
            if (LightmapArrayDescriptorSet == VK_NULL_HANDLE) {
                VkDescriptorSetAllocateInfo allocate{};
                allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocate.descriptorPool = LightmapDescriptorPool;
                allocate.descriptorSetCount = 1;
                allocate.pSetLayouts = &LightmapArrayDescriptorSetLayout;
                if (vkAllocateDescriptorSets(Device, &allocate, &LightmapArrayDescriptorSet) != VK_SUCCESS)
                    return false;
            }
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = texture.sampler;
            imageInfo.imageView = texture.view;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = LightmapArrayDescriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
            return true;
            };
        if (!writeLightmapDescriptor(LightmapFallbackArrayTexture))
            LOG_CATEGORY_WARN("Lighting", "Could not bind the neutral lightmap fallback while changing levels");
        if (BakedLightingAtlasArrayTexture.image != VK_NULL_HANDLE) DestroyTexture(BakedLightingAtlasArrayTexture);
        BakedLightingAtlasLayers = 0;
        BakedLightingAtlasPages = 0;
        BakedLightingHasStaticSun = false;
        BakedStaticLights.clear();
        BakedLightmapAssignments.clear();
        BakedLightingLevelUID = 0;
        const std::filesystem::path cachePath = std::filesystem::path("Engine/BakedLights") / (level.GetName() + ".lightmapbin");
        std::error_code existsError;
        if (!std::filesystem::exists(cachePath, existsError) || existsError)
            return false;
        std::ifstream cache(cachePath, std::ios::binary);
        LightmapCacheHeader header{};
        if (!cache.is_open() || !cache.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
            std::memcmp(header.magic, "EELMAP01", 8) != 0 || header.version != 3u) {
            LOG_CATEGORY_WARN("Lighting", "Ignoring invalid baked-lighting cache '{}'", cachePath.generic_string());
            return false;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(PhysicalDevice, &properties);
        if (header.resolution < 8u || header.resolution > properties.limits.maxImageDimension2D ||
            header.pageCount == 0u || header.pageCount > 256u ||
            header.layerCount == 0u || header.layerCount > properties.limits.maxImageArrayLayers) {
            LOG_CATEGORY_WARN("Lighting", "Baked-lighting cache '{}' does not fit this GPU or has invalid dimensions", cachePath.generic_string());
            return false;
        }
        const std::uint64_t expectedBytes = static_cast<std::uint64_t>(header.resolution) * header.resolution * header.layerCount * 4ull;
        const std::uint64_t expectedLayers = static_cast<std::uint64_t>(header.pageCount) * (1ull + static_cast<std::uint64_t>(header.staticLightCount) + (header.hasStaticSun ? 1ull : 0ull));
        if (header.hasStaticSun > 1u || header.staticLightCount > MaxBakedStaticLights || expectedLayers != header.layerCount || expectedBytes != header.pixelBytes || header.pixelBytes > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) || header.assignmentCount == 0u || header.assignmentCount > level.GetObjects().size()) {
            LOG_CATEGORY_WARN("Lighting", "Baked-lighting cache '{}' has invalid dimensions or inconsistent atlas data", cachePath.generic_string());
            return false;
        }
        const std::uint64_t currentSourceSignature = ComputeBakedLightingSourceSignature(level);
        if (currentSourceSignature == 0u || currentSourceSignature != header.sourceSignature) {
            LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because bake-relevant mesh/material/light data changed", level.GetName());
            return false;
        }
        const bool currentHasStaticSun = level.GetSunType() == Level::SunType::Static;
        const Vector3 currentSunDirection = level.GetSunDirection();
        if ((header.hasStaticSun != 0u) != currentHasStaticSun || (currentHasStaticSun && (!NearlyEqual(header.sunDirection[0], currentSunDirection.x) || !NearlyEqual(header.sunDirection[1], currentSunDirection.y) || !NearlyEqual(header.sunDirection[2], currentSunDirection.z)))) {
            LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because the Static sun changed", level.GetName());
            return false;
        }
        std::vector<LightmapCacheAssignment> records;
        std::vector<LightmapCacheLight> lightRecords;
        std::vector<unsigned char> pixels;
        try {
            records.resize(header.assignmentCount);
            lightRecords.resize(header.staticLightCount);
            pixels.resize(static_cast<std::size_t>(header.pixelBytes));
        }
        catch (const std::bad_alloc&) {
            LOG_CATEGORY_WARN("Lighting", "Not enough host memory to load baked-lighting cache '{}'", cachePath.generic_string());
            return false;
        }
        if ((!records.empty() && !cache.read(reinterpret_cast<char*>(records.data()),
            static_cast<std::streamsize>(records.size() * sizeof(LightmapCacheAssignment)))) ||
            (!lightRecords.empty() && !cache.read(reinterpret_cast<char*>(lightRecords.data()),
                static_cast<std::streamsize>(lightRecords.size() * sizeof(LightmapCacheLight)))) ||
            (!pixels.empty() && !cache.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()))) ||
            !cache) {
            LOG_CATEGORY_WARN("Lighting", "Baked-lighting cache '{}' is truncated", cachePath.generic_string());
            return false;
        }
        const auto& objects = level.GetObjects();
        std::unordered_map<const Level::LevelObject*, BakedLightmapAssignment> assignments;
        std::unordered_set<std::uint32_t> usedObjectIndices;
        assignments.reserve(records.size());
        usedObjectIndices.reserve(records.size());
        for (const LightmapCacheAssignment& record : records) {
            if (record.objectIndex >= objects.size() || record.page >= header.pageCount ||
                !usedObjectIndices.emplace(record.objectIndex).second || !objects[record.objectIndex]) {
                LOG_CATEGORY_WARN("Lighting", "Baked-lighting cache '{}' has an invalid object assignment", cachePath.generic_string());
                return false;
            }
            const auto* mesh = dynamic_cast<const Level::Mesh*>(objects[record.objectIndex].get());
            if (!mesh || static_cast<std::uint32_t>(mesh->GetMeshType()) != record.meshType ||
                static_cast<std::uint32_t>(mesh->GetMobility()) != record.mobility ||
                mesh->GetMobility() == Level::Mesh::Mobility::Movable ||
                HashAssetString(mesh->GetCustomMesh()) != record.customMeshHash) {
                LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because mesh identity changed", level.GetName());
                return false;
            }
            const Vector3 position = mesh->GetPosition();
            const Vector3 rotation = mesh->GetRotation();
            const Vector3 size = mesh->GetSize();
            if (!NearlyEqual(record.position[0], position.x) || !NearlyEqual(record.position[1], position.y) ||
                !NearlyEqual(record.position[2], position.z) || !NearlyEqual(record.rotation[0], rotation.x) ||
                !NearlyEqual(record.rotation[1], rotation.y) || !NearlyEqual(record.rotation[2], rotation.z) ||
                !NearlyEqual(record.size[0], size.x) || !NearlyEqual(record.size[1], size.y) ||
                !NearlyEqual(record.size[2], size.z)) {
                LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because a baked mesh transform changed", level.GetName());
                return false;
            }
            BakedLightmapAssignment assignment{};
            assignment.page = record.page;
            std::copy(std::begin(record.scaleOffset), std::end(record.scaleOffset), assignment.scaleOffset.begin());
            assignments.emplace(objects[record.objectIndex].get(), assignment);
        }
        std::size_t currentBakeableMeshes = 0;
        for (const auto& object : objects) {
            const auto* mesh = object ? dynamic_cast<const Level::Mesh*>(object.get()) : nullptr;
            if (!mesh || mesh->GetMobility() == Level::Mesh::Mobility::Movable) continue;
            MeshHandle meshHandle = 0;
            switch (mesh->GetMeshType()) {
            case Level::Mesh::Cube:        meshHandle = CubeMeshHandle; break;
            case Level::Mesh::Plane:       meshHandle = PlaneMeshHandle; break;
            case Level::Mesh::Sphere:      meshHandle = SphereMeshHandle; break;
            case Level::Mesh::Cylinder:    meshHandle = CylinderMeshHandle; break;
            case Level::Mesh::Wedge:       meshHandle = WedgeMeshHandle; break;
            case Level::Mesh::CornerWedge: meshHandle = CornerWedgeMeshHandle; break;
            case Level::Mesh::Cone:        meshHandle = ConeMeshHandle; break;
            case Level::Mesh::Pyramid:     meshHandle = PyramidMeshHandle; break;
            case Level::Mesh::Custom:      meshHandle = mesh->GetMeshAssetId(); break;
            default: break;
            }
            if (meshHandle != 0 && Meshes.find(meshHandle) != Meshes.end())
                ++currentBakeableMeshes;
        }
        if (currentBakeableMeshes != records.size()) {
            LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because the set of bakeable static/stationary meshes changed (cache {}, current {})", level.GetName(), records.size(), currentBakeableMeshes);
            return false;
        }
        std::vector<const Level::Light*> staticLights;
        staticLights.reserve((std::min)(level.GetLevelLights().size(), static_cast<std::size_t>(MaxBakedStaticLights)));
        for (const auto& light : level.GetLevelLights()) {
            if (light && light->GetType() == Level::Light::Type::Static && staticLights.size() < MaxBakedStaticLights)
                staticLights.push_back(light.get());
        }
        if (staticLights.size() != lightRecords.size()) {
            LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because the Static-light set changed", level.GetName());
            return false;
        }
        for (std::size_t i = 0; i < staticLights.size(); ++i) {
            const Vector3 position = staticLights[i]->GetPosition();
            if (!NearlyEqual(lightRecords[i].position[0], position.x) ||
                !NearlyEqual(lightRecords[i].position[1], position.y) ||
                !NearlyEqual(lightRecords[i].position[2], position.z)) {
                LOG_CATEGORY_INFO("Lighting", "Baked-lighting cache for '{}' is stale because a Static light moved", level.GetName());
                return false;
            }
        }
        const std::size_t pageBytes = static_cast<std::size_t>(header.resolution) * header.resolution * 4u;
        GpuTexture loadedAtlas{};
        auto copyLayer = [&pixels, pageBytes](std::uint32_t layer, unsigned char* destination) { std::memcpy(destination, pixels.data() + static_cast<std::size_t>(layer) * pageBytes, pageBytes); };
        if (!CreateTextureArrayFromGeneratedPixels(header.resolution, header.resolution,
            header.layerCount, header.pixelBytes, copyLayer, loadedAtlas)) {
            LOG_CATEGORY_WARN("Lighting", "Could not upload baked-lighting cache '{}'", cachePath.generic_string());
            return false;
        }
        if (!writeLightmapDescriptor(loadedAtlas)) {
            DestroyTexture(loadedAtlas);
            (void)writeLightmapDescriptor(LightmapFallbackArrayTexture);
            LOG_CATEGORY_WARN("Lighting", "Could not bind the restored baked-lighting atlas");
            return false;
        }
        BakedLightingAtlasArrayTexture = loadedAtlas;
        BakedLightingAtlasLayers = header.layerCount;
        BakedLightingAtlasPages = header.pageCount;
        BakedLightingHasStaticSun = header.hasStaticSun != 0u;
        BakedLightmapAssignments = std::move(assignments);
        BakedStaticLights.reserve(staticLights.size());
        for (std::size_t i = 0; i < staticLights.size(); ++i) {
            BakedStaticLightInfo info{};
            info.light = staticLights[i];
            std::copy(std::begin(lightRecords[i].position), std::end(lightRecords[i].position), info.bakedPosition.begin());
            BakedStaticLights.push_back(info);
        }
        BakedLightingLevelUID = level.GetId();
        LOG_CATEGORY_INFO("Lighting", "Restored baked lighting from '{}' ({}x{}, {} page(s), {} layer(s))",
            cachePath.generic_string(), header.resolution, header.resolution, header.pageCount, header.layerCount);
        return true;
    }

    bool Vulkan::DrawFrame(Level& level, double deltaTime, const Camera& camera, const std::function<void(VkCommandBuffer)>& guiRecorder, bool falseOnEveryFail) {
        if (!IsInitialized()) {
            LOG_CATEGORY_ERROR("Vulkan", "Vulkan is NOT initialized");
            return !falseOnEveryFail;
        }
        if (camera.GetDepthMode() != DepthMode::ReverseZeroToOne) {
            LOG_CATEGORY_ERROR("Vulkan", "The current Vulkan 3D pipeline expects Camera::ReverseZeroToOne depth mode");
            return !falseOnEveryFail;
        }
        if (GameWindow->FramebufferWidth() == 0 || GameWindow->FramebufferHeight() == 0)
            return true;
        FrameSync& frame = m_frames[CurrentFrame];
        if (vkWaitForFences(Device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not Wait for Synchronization");
            return false;
        }
        if (!level.AssetsAcquired) {
            LOG_CATEGORY_ERROR("Assets", "DrawFrame received level '{}' before AcquireLevelAssets completed", level.GetName());
            return !falseOnEveryFail;
        }
        Level* PrimaryLevel = &level;
        if (BakedLightingLevelUID != PrimaryLevel->GetId() && BakedLightingCacheAttemptedLevelUID != PrimaryLevel->GetId())
            (void)TryLoadBakedLighting(*PrimaryLevel);
        Level::Sky* activeSky = PrimaryLevel->GetCurrentSky();
        const Level::Sky::Mode skyMode = activeSky ? activeSky->GetMode() : Level::Sky::SkyNone;
        const bool drawSky = activeSky != nullptr && skyMode != Level::Sky::SkyNone;
        const bool skyNeedsInstance = drawSky && (skyMode == Level::Sky::SkyBox || skyMode == Level::Sky::SkyDome);
        if (drawSky && activeSky->GetMaterialId() == 0 && !activeSky->GetMaterialAsset().empty())
            LOG_CATEGORY_WARN("Assets", "Sky material '{}' was not acquired before DrawFrame", activeSky->GetMaterialAsset());

        // LOAD CONVARS
        bool showBakedLighting = false;
        if (Console().GetConVar("rnd_visualizebakedlighting"))
            showBakedLighting = Console().GetConVar("rnd_visualizebakedlighting")->GetBool();
        const bool hasActiveLightmap = PrimaryLevel && BakedLightingLevelUID == PrimaryLevel->GetId() && BakedLightingAtlasArrayTexture.image != VK_NULL_HANDLE;
        RealtimeLightingGpuData realtimeLightingData{};
        std::uint32_t realtimeLightCount = 0;
        for (const auto& ownedLight : PrimaryLevel->GetLevelLights()) {
            if (!ownedLight || ownedLight->GetType() != Level::Light::Type::RealTime || !ownedLight->IsEnabled()) continue;
            const float intensity = (std::max)(ownedLight->GetIntensity(), 0.0f);
            const float radius = (std::max)(ownedLight->GetEffectiveRadius(), 0.0f);
            if (intensity <= 0.0f || radius <= 0.0f || realtimeLightCount >= MaxRealtimeLights) continue;
            const Vector3 position = ownedLight->GetPosition();
            const Vector3 color = ownedLight->GetColor();
            realtimeLightingData.positionRadius[realtimeLightCount] = { static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z), radius };
            realtimeLightingData.colorIntensity[realtimeLightCount] = { static_cast<float>((std::max)(color.x, 0.0)), static_cast<float>((std::max)(color.y, 0.0)), static_cast<float>((std::max)(color.z, 0.0)), intensity };
            ++realtimeLightCount;
        }
        const bool hasRealtimeSun = PrimaryLevel->GetSunType() == Level::SunType::RealTime;
        if (hasRealtimeSun) {
            Vector3 sunDirection = PrimaryLevel->GetSunDirection().Unit();
            if (sunDirection == Vector3::Zero()) sunDirection = Vector3::ZAxis();
            const Vector3 sunColor = PrimaryLevel->GetCurrentSky() ? PrimaryLevel->GetCurrentSky()->GetSunColorMultiplier() : Vector3::One();
            realtimeLightingData.sunDirectionIntensity = { static_cast<float>(sunDirection.x), static_cast<float>(sunDirection.y), static_cast<float>(sunDirection.z), 1.0f };
            realtimeLightingData.sunColor = { static_cast<float>((std::max)(sunColor.x, 0.0)), static_cast<float>((std::max)(sunColor.y, 0.0)), static_cast<float>((std::max)(sunColor.z, 0.0)), 0.0f };
        }
        realtimeLightingData.metadata = { static_cast<float>(realtimeLightCount), hasRealtimeSun ? 1.0f : 0.0f, 0.0f, 0.0f };
        if (frame.mappedRealtimeLighting)
            std::memcpy(frame.mappedRealtimeLighting, &realtimeLightingData, sizeof(realtimeLightingData));
        const bool hasRealtimeLighting = realtimeLightCount > 0u || hasRealtimeSun;

        // GLOBAL SHADER PARAMETERS
        const Vector3 cameraPosition = camera.GetViewPoint();
        const Matrix4 cameraMatrix = camera.GetViewProjMatrix();
        const float elapsed = std::chrono::duration<float>(std::chrono::system_clock::now() - engineStart).count();
        const float sinElapsed = std::sin(elapsed);
        const float sinDelta = static_cast<float>(std::sin(deltaTime));
        const float width = static_cast<float>(GameWindow->FramebufferWidth());
        const float height = static_cast<float>(GameWindow->FramebufferHeight());
        for (auto& [materialId, material] : Materials) {
            (void)materialId;
            auto& materialFrame = material.frames[CurrentFrame];
            if (materialFrame.mapped)
                std::memcpy(materialFrame.mapped, &material.gpuData, sizeof(MaterialGpuData));
        }
        if (frame.mappedLightmapParams) {
            MaterialGpuData runtimeLightmapData{};
            runtimeLightmapData.matUV = { static_cast<float>(cameraPosition.x), static_cast<float>(cameraPosition.y), static_cast<float>(cameraPosition.z), 1.0f };
            for (std::uint32_t i = 0; i < MaxBakedStaticLights; ++i)
                runtimeLightmapData.params[i * 2u] = { 0.0f, 0.0f, 0.0f, 0.001f };
            const std::size_t staticCount = hasActiveLightmap ? (std::min)(BakedStaticLights.size(), static_cast<std::size_t>(MaxBakedStaticLights)) : 0u;
            for (std::size_t i = 0; i < staticCount; ++i) {
                const BakedStaticLightInfo& baked = BakedStaticLights[i];
                const Level::Light* liveLight = nullptr;
                for (const auto& ownedLight : PrimaryLevel->GetLevelLights()) {
                    if (ownedLight && ownedLight.get() == baked.light) {
                        liveLight = ownedLight.get();
                        break;
                    }
                }
                const Vector3 color = liveLight ? liveLight->GetColor() : Vector3::Zero();
                const float intensity = liveLight && liveLight->GetType() == Level::Light::Type::Static && liveLight->IsEnabled() ? (std::max)(liveLight->GetIntensity(), 0.0f) : 0.0f;
                const float radius = liveLight ? (std::max)(liveLight->GetEffectiveRadius(), 0.001f) : 0.001f;
                runtimeLightmapData.params[i * 2u] = { baked.bakedPosition[0], baked.bakedPosition[1], baked.bakedPosition[2], radius };
                runtimeLightmapData.params[i * 2u + 1u] = { static_cast<float>(color.x), static_cast<float>(color.y), static_cast<float>(color.z), intensity };
            }
            const bool hasStaticSun = hasActiveLightmap && BakedLightingHasStaticSun;
            const Vector3 sunColor = hasStaticSun && PrimaryLevel->GetCurrentSky() ? PrimaryLevel->GetCurrentSky()->GetSunColorMultiplier() : Vector3::One();
            runtimeLightmapData.params[30] = { static_cast<float>(sunColor.x), static_cast<float>(sunColor.y), static_cast<float>(sunColor.z), 0.0f };
            runtimeLightmapData.params[31] = { hasActiveLightmap ? static_cast<float>(BakedLightingAtlasPages) : 0.0f, static_cast<float>(staticCount), hasStaticSun ? 1.0f : 0.0f, showBakedLighting ? 1.0f : 0.0f };
            std::memcpy(frame.mappedLightmapParams, &runtimeLightmapData, sizeof(runtimeLightmapData));
        }
        std::uint32_t imageIndex = 0;
        const VkResult acquireResult = vkAcquireNextImageKHR(Device, Swapchain, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
            return RecreateSwapchain().IsSuccess;
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not Acquire Next Image: {}", VkResultToString(acquireResult));
            return acquireResult == VK_TIMEOUT || acquireResult == VK_NOT_READY ? !falseOnEveryFail : false;
        }
        VkCommandBuffer commandBuffer = CommandBuffers[imageIndex];
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not Begin Command Buffer");
            return !falseOnEveryFail;
        }
        thread_local std::vector<PendingInstance> pendingInstances;
        thread_local std::vector<Batch> batches;
        thread_local std::vector<InstanceData> flattenedInstances;
        pendingInstances.clear();
        batches.clear();
        flattenedInstances.clear();
        if (PrimaryLevel) {
            const auto& levelObjects = PrimaryLevel->GetObjects();
            constexpr std::size_t ObjectsPerTask = 64u;
            const std::size_t chunkCount = (levelObjects.size() + ObjectsPerTask - 1u) / ObjectsPerTask;
            std::vector<std::vector<PendingInstance>> pendingChunks(chunkCount);
            ParallelForRanges(levelObjects.size(), ObjectsPerTask, [&](std::size_t begin, std::size_t end) {
                std::vector<PendingInstance> local;
                for (std::size_t objectIndex = begin; objectIndex < end; ++objectIndex) {
                    const auto& object = levelObjects[objectIndex];
                    if (!object) continue;
                    const auto* mesh = dynamic_cast<const Level::Mesh*>(object.get());
                    if (!mesh) continue;
                    std::uint32_t meshHandle = 0;
                    switch (mesh->GetMeshType()) {
                    case Level::Mesh::Cube:        meshHandle = CubeMeshHandle; break;
                    case Level::Mesh::Plane:       meshHandle = PlaneMeshHandle; break;
                    case Level::Mesh::Sphere:      meshHandle = SphereMeshHandle; break;
                    case Level::Mesh::Cylinder:    meshHandle = CylinderMeshHandle; break;
                    case Level::Mesh::Wedge:       meshHandle = WedgeMeshHandle; break;
                    case Level::Mesh::CornerWedge: meshHandle = CornerWedgeMeshHandle; break;
                    case Level::Mesh::Cone:        meshHandle = ConeMeshHandle; break;
                    case Level::Mesh::Pyramid:     meshHandle = PyramidMeshHandle; break;
                    case Level::Mesh::Custom:      meshHandle = mesh->GetMeshAssetId(); break;
                    default:                       meshHandle = 0; break;
                    }
                    auto gpuMeshIt = Meshes.find(meshHandle);
                    if (meshHandle == 0 || gpuMeshIt == Meshes.end()) {
                        meshHandle = 0;
                        gpuMeshIt = Meshes.find(meshHandle);
                    }
                    if (gpuMeshIt == Meshes.end()) continue;
                    const GpuMesh& gpuMesh = gpuMeshIt->second;
                    const Vector3& position = mesh->GetPosition();
                    const Vector3& size = mesh->GetSize();
                    const Vector3& rotation = mesh->GetRotation();
                    const BakedLightmapAssignment* lightmapAssignment = nullptr;
                    if (BakedLightingLevelUID == PrimaryLevel->GetId()) {
                        const auto assignmentIt = BakedLightmapAssignments.find(object.get());
                        if (assignmentIt != BakedLightmapAssignments.end())
                            lightmapAssignment = &assignmentIt->second;
                    }
                    for (std::size_t subIdx = 0; subIdx < gpuMesh.subMeshes.size(); ++subIdx) {
                        PendingInstance pending{};
                        pending.mesh = meshHandle;
                        pending.subMeshIndex = static_cast<std::uint32_t>(subIdx);
                        const MaterialHandle assignedMaterial = mesh->GetMaterialId(static_cast<std::uint32_t>(subIdx));
                        pending.material = meshHandle == 0 ? ErrorMaterialHandle :
                            (assignedMaterial != 0 ? assignedMaterial : gpuMesh.subMeshes[subIdx].defaultMaterial);
                        const auto materialResourceIt = Materials.find(pending.material);
                        const bool materialReceivesLighting = materialResourceIt == Materials.end() || materialResourceIt->second.lit;
                        pending.instance.position[0] = static_cast<float>(position.x);
                        pending.instance.position[1] = static_cast<float>(position.y);
                        pending.instance.position[2] = static_cast<float>(position.z);
                        pending.instance.size[0] = static_cast<float>(size.x);
                        pending.instance.size[1] = static_cast<float>(size.y);
                        pending.instance.size[2] = static_cast<float>(size.z);
                        pending.instance.rotation[0] = static_cast<float>(rotation.x);
                        pending.instance.rotation[1] = static_cast<float>(rotation.y);
                        pending.instance.rotation[2] = static_cast<float>(rotation.z);
                        if (lightmapAssignment && materialReceivesLighting) {
                            std::copy(lightmapAssignment->scaleOffset.begin(), lightmapAssignment->scaleOffset.end(), pending.instance.lightmapScaleOffset);
                            pending.instance.lightmapPage = static_cast<float>(lightmapAssignment->page);
                        }
                        else {
                            pending.instance.lightmapPage = materialReceivesLighting ? -2.0f : -1.0f;
                            pending.instance.lightmapScaleOffset[0] = 1.0f;
                            pending.instance.lightmapScaleOffset[1] = 1.0f;
                            pending.instance.lightmapScaleOffset[2] = 0.0f;
                            pending.instance.lightmapScaleOffset[3] = 0.0f;
                        }
                        local.push_back(pending);
                    }
                }
                pendingChunks[begin / ObjectsPerTask] = std::move(local);
                }, 512u);
            std::size_t totalPending = 0;
            for (const auto& chunk : pendingChunks) totalPending += chunk.size();
            pendingInstances.reserve(totalPending);
            for (auto& chunk : pendingChunks)
                pendingInstances.insert(pendingInstances.end(), std::make_move_iterator(chunk.begin()), std::make_move_iterator(chunk.end()));
        }
        std::sort(pendingInstances.begin(), pendingInstances.end(), [](const PendingInstance& a, const PendingInstance& b) {if (a.mesh != b.mesh)return a.mesh < b.mesh; if (a.subMeshIndex != b.subMeshIndex) return a.subMeshIndex < b.subMeshIndex; return a.material < b.material; });
        const std::size_t requiredInstanceCount = pendingInstances.size() + (skyNeedsInstance ? 1u : 0u);
        if (requiredInstanceCount > 0) {
            bool sufficient = true;
            for (const FrameSync& frame : m_frames)
                if (requiredInstanceCount > frame.instanceCapacity) {
                    sufficient = false;
                    break;
                }
            if (!sufficient) {
                if (vkDeviceWaitIdle(Device) != VK_SUCCESS) {
                    vkEndCommandBuffer(commandBuffer);
                    LOG_CATEGORY_ERROR("Vulkan", "Could not grow instance buffer");
                    return false;
                }
                std::size_t newCapacity = InitialInstanceCapacity;
                for (const FrameSync& frame : m_frames)
                    newCapacity = (std::max)(newCapacity, frame.instanceCapacity);
                while (newCapacity < requiredInstanceCount)
                    newCapacity *= 2;
                if (Device == VK_NULL_HANDLE) {
                    for (auto& frame : m_frames)
                        frame.instanceBuffer = {};
                }
                else {
                    for (FrameSync& frame : m_frames) {
                        if (frame.mappedInstanceData && frame.instanceBuffer.memory != VK_NULL_HANDLE)
                            vkUnmapMemory(Device, frame.instanceBuffer.memory);
                        frame.mappedInstanceData = nullptr;
                        frame.instanceCapacity = 0;
                        DestroyBuffer(frame.instanceBuffer);
                    }
                    for (FrameSync& frame : m_frames) {
                        const VkDeviceSize bytes = static_cast<VkDeviceSize>(newCapacity * sizeof(InstanceData));
                        if (!CreateBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frame.instanceBuffer)) {
                            vkEndCommandBuffer(commandBuffer);
                            LOG_CATEGORY_ERROR("Vulkan", "Could not grow instance buffer");
                            return false;
                        }
                        void* mapped = nullptr;
                        if (vkMapMemory(Device, frame.instanceBuffer.memory, 0, bytes, 0, &mapped) != VK_SUCCESS) {
                            vkEndCommandBuffer(commandBuffer);
                            LOG_CATEGORY_ERROR("Vulkan", "Could not grow instance buffer");
                            return false;
                        }
                        frame.mappedInstanceData = mapped;
                        frame.instanceCapacity = newCapacity;
                    }
                }
            }
            flattenedInstances.reserve(requiredInstanceCount);
            if (skyNeedsInstance) {
                const Vector3& rotation = activeSky->GetRotation();
                InstanceData skyInstance{};
                skyInstance.position[0] = static_cast<float>(cameraPosition.x);
                skyInstance.position[1] = static_cast<float>(cameraPosition.y);
                skyInstance.position[2] = static_cast<float>(cameraPosition.z);
                skyInstance.size[0] = 200.0f;
                skyInstance.size[1] = 200.0f;
                skyInstance.size[2] = 200.0f;
                skyInstance.rotation[0] = static_cast<float>(rotation.x - (skyMode == Level::Sky::SkyDome ? 90.0 : 0.0));
                skyInstance.rotation[1] = static_cast<float>(rotation.y);
                skyInstance.rotation[2] = static_cast<float>(rotation.z);
                flattenedInstances.push_back(skyInstance);
            }
            for (const PendingInstance& pending : pendingInstances) {
                if (batches.empty() || batches.back().mesh != pending.mesh || batches.back().subMeshIndex != pending.subMeshIndex || batches.back().material != pending.material) {
                    Batch batch{};
                    batch.mesh = pending.mesh;
                    batch.subMeshIndex = pending.subMeshIndex;
                    batch.material = pending.material;
                    batch.firstInstance = static_cast<std::uint32_t>(flattenedInstances.size());
                    batch.instanceCount = 0;
                    batches.push_back(batch);
                }
                flattenedInstances.push_back(pending.instance);
                batches.back().instanceCount++;
            }
            if (!flattenedInstances.empty() && frame.mappedInstanceData == nullptr) {
                vkEndCommandBuffer(commandBuffer);
                LOG_CATEGORY_ERROR("Vulkan", "Could not update instance buffer");
                return !falseOnEveryFail;
            }
            if (!flattenedInstances.empty()) {
                const std::size_t bytes = flattenedInstances.size() * sizeof(InstanceData);
                std::memcpy(frame.mappedInstanceData, flattenedInstances.data(), bytes);
            }
        }
        VkClearValue clearValues[4]{};
        clearValues[0].color = { 0.3f, 0.3f, 0.5f, 1.0f };
        clearValues[1].color = { 0.0f, 0.0f, 0.0f, 0.0f }; // ALPHA 0 == NO GEOMETRY
        clearValues[2].color = clearValues[0].color; // FALLBACK WHEN SKY IS NOT ACTIVE
        clearValues[3].depthStencil = { 0.0f, 0 };
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = RenderPass;
        renderPassInfo.framebuffer = SceneTargets[imageIndex].framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = SwapchainExtent;
        renderPassInfo.clearValueCount = 4;
        renderPassInfo.pClearValues = clearValues;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        RenderPushConstants renderConstants{};
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                renderConstants.viewProjection[column * 4 + row] = static_cast<float>(cameraMatrix[row][column]);
        renderConstants.cameraPosition[0] = static_cast<float>(cameraPosition.x);
        renderConstants.cameraPosition[1] = static_cast<float>(cameraPosition.y);
        renderConstants.cameraPosition[2] = static_cast<float>(cameraPosition.z);
        renderConstants.cameraPosition[3] = 0.0f;
        renderConstants.timeData[0] = elapsed;
        renderConstants.timeData[1] = static_cast<float>(deltaTime);
        renderConstants.timeData[2] = sinElapsed;
        renderConstants.timeData[3] = sinDelta;
        renderConstants.screenData[0] = width;
        renderConstants.screenData[1] = height;
        renderConstants.screenData[2] = static_cast<float>(SwapchainExtent.width) / static_cast<float>((std::max)(SwapchainExtent.height, 1u));
        renderConstants.frameData[0] = static_cast<float>(FrameCounter);
        if (drawSky) {
            Vector3 sunDirection = PrimaryLevel->GetSunDirection();
            sunDirection = sunDirection.Unit();
            if (sunDirection == Vector3::Zero()) sunDirection = Vector3::ZAxis();
            RenderPushConstants skyConstants = renderConstants;
            skyConstants.cameraPosition[3] = static_cast<float>(skyMode);
            skyConstants.directionAndSize[0] = static_cast<float>(sunDirection.x);
            skyConstants.directionAndSize[1] = static_cast<float>(sunDirection.y);
            skyConstants.directionAndSize[2] = static_cast<float>(sunDirection.z);
            skyConstants.directionAndSize[3] = activeSky->GetSunSize();
            vkCmdPushConstants(commandBuffer, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RenderPushConstants), &skyConstants);
            MaterialHandle skyMaterialHandle = activeSky->GetMaterialId();
            auto skyMaterialIt = Materials.find(skyMaterialHandle);
            if (skyMaterialIt == Materials.end()) { skyMaterialHandle = 0; skyMaterialIt = Materials.find(skyMaterialHandle); }
            if (skyMaterialIt != Materials.end()) {
                MaterialResource* skyMaterial = &skyMaterialIt->second;
                MaterialPipelineConfig skyPipelineConfig = skyMaterial->pipelineConfig;
                skyPipelineConfig.isSky = true;
                skyPipelineConfig.flatSky = skyMode == Level::Sky::SkyFlat;
                skyPipelineConfig.cullMode = VK_CULL_MODE_NONE;
                VkPipeline skyPipeline = CreateMaterialPipeline(FileParsers::SkyFragmentShaderPath, skyPipelineConfig);
                if (skyPipeline == VK_NULL_HANDLE && skyMaterialHandle != 0) {
                    skyMaterialHandle = 0;
                    skyMaterialIt = Materials.find(0);
                    if (skyMaterialIt != Materials.end()) {
                        skyMaterial = &skyMaterialIt->second;
                        MaterialPipelineConfig fallbackSkyConfig = skyMaterial->pipelineConfig;
                        fallbackSkyConfig.isSky = true;
                        fallbackSkyConfig.flatSky = skyMode == Level::Sky::SkyFlat;
                        fallbackSkyConfig.cullMode = VK_CULL_MODE_NONE;
                        skyPipeline = CreateMaterialPipeline(FileParsers::SkyFragmentShaderPath, fallbackSkyConfig);
                    }
                }
                if (skyPipeline != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
                    const VkDescriptorSet skySet = skyMaterial->frames[CurrentFrame].descriptorSet;
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &skySet, 0, nullptr);
                    if (skyMode == Level::Sky::SkyFlat)
                        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
                    else {
                        const std::uint32_t skyMeshHandle = skyMode == Level::Sky::SkyBox ? CubeMeshHandle : SphereMeshHandle;
                        const auto skyMeshIt = Meshes.find(skyMeshHandle);
                        if (skyMeshIt != Meshes.end() && frame.instanceBuffer.buffer != VK_NULL_HANDLE) {
                            const GpuMesh& skyMesh = skyMeshIt->second;
                            VkBuffer vertexBuffers[] = { skyMesh.vertexBuffer.buffer, frame.instanceBuffer.buffer };
                            VkDeviceSize offsets[] = { 0, 0 };
                            vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
                            vkCmdBindIndexBuffer(commandBuffer, skyMesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                            for (const GpuSubMesh& subMesh : skyMesh.subMeshes)
                                vkCmdDrawIndexed(commandBuffer, subMesh.indexCount, 1, subMesh.indexOffset, 0, 0);
                        }
                    }
                }
                else
                    LOG_CATEGORY_ERROR("Vulkan", "Sky material '{}' has no valid sky graphics pipeline", skyMaterial->name);
            }
        }

        // RESTORE SHADER STATE BEFORE DRAWING PEASANT WORLD MESHES
        renderConstants.cameraPosition[3] = 0.0f;
        vkCmdPushConstants(commandBuffer, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RenderPushConstants), &renderConstants);
        VkPipeline lastBoundPipeline = VK_NULL_HANDLE;
        MaterialHandle lastBoundMaterial = UINT32_MAX;
        std::uint32_t lastBoundMesh = (std::numeric_limits<std::uint32_t>::max)();
        for (const Batch& batch : batches) {
            const auto meshIt = Meshes.find(batch.mesh);
            if (meshIt == Meshes.end())continue;
            const GpuMesh& gpuMesh = meshIt->second;
            if (batch.subMeshIndex >= gpuMesh.subMeshes.size())continue;
            const GpuSubMesh& subMesh = gpuMesh.subMeshes[batch.subMeshIndex];
            MaterialHandle materialHandle = batch.material;
            auto materialIt = Materials.find(materialHandle);
            if (materialIt == Materials.end()) { materialHandle = 0; materialIt = Materials.find(materialHandle); }
            if (materialIt == Materials.end())continue;
            MaterialResource& material = materialIt->second;
            auto pipelineIt = ShaderPipelines.find(material.fragmentShaderSpv);
            VkPipeline pipeline = pipelineIt == ShaderPipelines.end() ? CreateMaterialPipeline(material.fragmentShaderSpv, material.pipelineConfig) : pipelineIt->second;
            if (pipeline == VK_NULL_HANDLE) {
                LOG_CATEGORY_ERROR("Vulkan", "Material '{}' has no valid graphics pipeline; using missing material", material.name);
                auto fallbackIt = Materials.find(0);
                if (fallbackIt == Materials.end())continue;
                materialHandle = 0;
                materialIt = fallbackIt;
                pipelineIt = ShaderPipelines.find(materialIt->second.fragmentShaderSpv);
                if (pipelineIt == ShaderPipelines.end())continue;
                pipeline = pipelineIt->second;
            }
            if (pipeline != lastBoundPipeline) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                lastBoundPipeline = pipeline;
            }
            if (materialHandle != lastBoundMaterial) {
                const VkDescriptorSet set = materialIt->second.frames[CurrentFrame].descriptorSet;
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &set, 0, nullptr);
                lastBoundMaterial = materialHandle;
            }
            if (batch.mesh != lastBoundMesh) {
                VkBuffer vertexBuffers[] = { gpuMesh.vertexBuffer.buffer, frame.instanceBuffer.buffer };
                VkDeviceSize offsets[] = { 0, 0 };
                vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, gpuMesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                lastBoundMesh = batch.mesh;
            }
            vkCmdDrawIndexed(commandBuffer, subMesh.indexCount, batch.instanceCount, subMesh.indexOffset, 0, batch.firstInstance);
        }
        vkCmdEndRenderPass(commandBuffer);

        // LIGHTMAP
        const bool compositeLightmapThisFrame = hasActiveLightmap || showBakedLighting || hasRealtimeLighting;
        if (hasActiveLightmap || hasRealtimeLighting) {
            VkClearValue lightmapInfoClear{};
            lightmapInfoClear.color = { 0.0f, 0.0f, 0.0f, 0.0f };
            VkRenderPassBeginInfo lightmapInfoPassInfo{};
            lightmapInfoPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            lightmapInfoPassInfo.renderPass = LightmapInfoRenderPass;
            lightmapInfoPassInfo.framebuffer = SceneTargets[imageIndex].lightmapInfoFramebuffer;
            lightmapInfoPassInfo.renderArea.offset = { 0, 0 };
            lightmapInfoPassInfo.renderArea.extent = SwapchainExtent;
            lightmapInfoPassInfo.clearValueCount = 1;
            lightmapInfoPassInfo.pClearValues = &lightmapInfoClear;
            vkCmdBeginRenderPass(commandBuffer, &lightmapInfoPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdPushConstants(commandBuffer, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RenderPushConstants), &renderConstants);
            VkPipeline infoLastBoundPipeline = VK_NULL_HANDLE;
            std::uint32_t infoBoundMesh = (std::numeric_limits<std::uint32_t>::max)();
            for (const Batch& batch : batches) {
                const auto meshIt = Meshes.find(batch.mesh);
                if (meshIt == Meshes.end()) continue;
                const GpuMesh& gpuMesh = meshIt->second;
                if (batch.subMeshIndex >= gpuMesh.subMeshes.size()) continue;
                const GpuSubMesh& subMesh = gpuMesh.subMeshes[batch.subMeshIndex];
                auto materialIt = Materials.find(batch.material);
                if (materialIt == Materials.end()) materialIt = Materials.find(0);
                const VkCullModeFlags materialCullMode = materialIt != Materials.end() ? materialIt->second.pipelineConfig.cullMode : VK_CULL_MODE_BACK_BIT;
                const std::size_t cullIndex = static_cast<std::size_t>(materialCullMode & VK_CULL_MODE_FRONT_AND_BACK);
                if (cullIndex >= LightmapInfoPipelines.size() || LightmapInfoPipelines[cullIndex] == VK_NULL_HANDLE) continue;
                const VkPipeline infoPipeline = LightmapInfoPipelines[cullIndex];
                if (infoPipeline != infoLastBoundPipeline) {
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, infoPipeline);
                    infoLastBoundPipeline = infoPipeline;
                }
                if (batch.mesh != infoBoundMesh) {
                    VkBuffer vertexBuffers[] = { gpuMesh.vertexBuffer.buffer, frame.instanceBuffer.buffer };
                    VkDeviceSize offsets[] = { 0, 0 };
                    vkCmdBindVertexBuffers(commandBuffer, 0, 2, vertexBuffers, offsets);
                    vkCmdBindIndexBuffer(commandBuffer, gpuMesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                    infoBoundMesh = batch.mesh;
                }
                vkCmdDrawIndexed(commandBuffer, subMesh.indexCount, batch.instanceCount, subMesh.indexOffset, 0, batch.firstInstance);
            }
            vkCmdEndRenderPass(commandBuffer);
        }
        if (compositeLightmapThisFrame) {
            VkRenderPassBeginInfo compositePassInfo{};
            compositePassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            compositePassInfo.renderPass = LightmapCompositeRenderPass;
            compositePassInfo.framebuffer = SceneTargets[imageIndex].lightmapCompositeFramebuffer;
            compositePassInfo.renderArea.offset = { 0, 0 };
            compositePassInfo.renderArea.extent = SwapchainExtent;
            compositePassInfo.clearValueCount = 0;
            compositePassInfo.pClearValues = nullptr;
            vkCmdBeginRenderPass(commandBuffer, &compositePassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, LightmapCompositePipeline);
            const VkDescriptorSet compositeSets[] = {
                SceneTargets[imageIndex].sceneInputDescriptorSet,
                LightmapArrayDescriptorSet,
                frame.lightmapRuntimeDescriptorSet,
                frame.realtimeLightingDescriptorSet
            };
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PostProcessPipelineLayout, 0, 4, compositeSets, 0, nullptr);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
        }

        // POST PROCESSING
        VkClearValue presentClear{};
        presentClear.color = { 0.3f, 0.3f, 0.5f, 1.0f };
        VkRenderPassBeginInfo presentPassInfo{};
        presentPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        presentPassInfo.renderPass = PresentRenderPass;
        presentPassInfo.framebuffer = SwapchainFramebuffers[imageIndex];
        presentPassInfo.renderArea.offset = { 0, 0 };
        presentPassInfo.renderArea.extent = SwapchainExtent;
        presentPassInfo.clearValueCount = 1;
        presentPassInfo.pClearValues = &presentClear;
        vkCmdBeginRenderPass(commandBuffer, &presentPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PostProcessPipeline);
        const VkDescriptorSet finalSceneDescriptorSet = compositeLightmapThisFrame ? SceneTargets[imageIndex].postDescriptorSet : SceneTargets[imageIndex].sceneInputDescriptorSet;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PostProcessPipelineLayout, 0, 1, &finalSceneDescriptorSet, 0, nullptr);
        PostProcessPushConstants postConstants{};
        postConstants.cameraPosition[0] = renderConstants.cameraPosition[0];
        postConstants.cameraPosition[1] = renderConstants.cameraPosition[1];
        postConstants.cameraPosition[2] = renderConstants.cameraPosition[2];
        postConstants.fogParams[0] = PrimaryLevel->fogStart;
        postConstants.fogParams[1] = PrimaryLevel->fogEnd;
        postConstants.fogColor[0] = static_cast<float>(PrimaryLevel->fogColor.x);
        postConstants.fogColor[1] = static_cast<float>(PrimaryLevel->fogColor.y);
        postConstants.fogColor[2] = static_cast<float>(PrimaryLevel->fogColor.z);
        postConstants.fogColor[3] = PrimaryLevel->fogColorOverride;
        postConstants.outputParams[0] = (SwapchainFormat == VK_FORMAT_B8G8R8A8_SRGB || SwapchainFormat == VK_FORMAT_R8G8B8A8_SRGB) ? 1.0f : 0.0f;
        vkCmdPushConstants(commandBuffer, PostProcessPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessPushConstants), &postConstants);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        if (guiRecorder)
            guiRecorder(commandBuffer);
        vkCmdEndRenderPass(commandBuffer);
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not End Command Buffer");
            return false;
        }
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (vkResetFences(Device, 1, &frame.inFlight) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not Reset Frame Fence");
            return false;
        }
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &frame.imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &frame.renderFinished;
        if (vkQueueSubmit(GraphicsQ, 1, &submitInfo, frame.inFlight) != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not Submit Queue");
            return !falseOnEveryFail;
        }
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &frame.renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &Swapchain;
        presentInfo.pImageIndices = &imageIndex;
        const VkResult presentResult = vkQueuePresentKHR(PresentQ, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
            if (!RecreateSwapchain().IsSuccess)
                return false;
        }
        else if (presentResult != VK_SUCCESS) {
            LOG_CATEGORY_ERROR("Vulkan", "Could not queue an image on a display surface: {}", VkResultToString(presentResult));
            return false;
        }
        CurrentFrame = (CurrentFrame + 1) % MaxFramesInFlight;
        return true;
    }

    bool Vulkan::DrawFrame(double deltaTime, const Camera& camera, const std::function<void(VkCommandBuffer)>& guiRecorder, bool falseOnEveryFail) {
        const auto level = PrimaryLevel.lock();
        if (!level) {
            LOG_CATEGORY_ERROR("Vulkan", "DrawFrame requires an explicit Level; no compatibility level is assigned");
            return !falseOnEveryFail;
        }
        return DrawFrame(*level, deltaTime, camera, guiRecorder, falseOnEveryFail);
    }
}