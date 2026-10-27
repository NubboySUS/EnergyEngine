//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Save and load engine file formats   //
//                                              //
//==============================================//


// FILE FORMATS:
//  .eemf -> Energy Engine Mesh File            | obj, fbx, glb e.t.c.
//  .eetf -> Energy Engine Texture File         | png, jpg, bmp e.t.c.
//  .eevf -> Energy Engine Vector File          | svg
//  .eesf -> Energy Engine Shader File          | spv
//  .eelf -> Energy Engine Level File           | game levels
//  .eeaf -> Energy Engine Animation File       | any keyframed animation, not only for bones
//  .eerf -> Energy Engine Render material File | materials
//  .eeif -> Energy Engine user Interface File  | graphical user interfaces
//  .eeof -> Energy Engine AUdio File           | at least i pronounce that as "ow-dee-ow"
//  .eexf -> Energy Engine particle efX File    | effects


#pragma once

#include "../VulkanContext.h"
#include "../Logger.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "FileParsers/tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "FileParsers/stb_image.h"
#include "Utils.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../EngineResources.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Engine::FileParsers {
    inline constexpr std::string_view LightBakeVersion = "0.1.0";
    inline constexpr std::string_view CookedShaderHeader = "ENERGY ENGINE SHADER FILE v1.0.0\r\n";
    inline constexpr std::string_view CookedShaderHeaderText = "ENERGY ENGINE SHADER FILE v1.0.0";
    inline constexpr std::string_view CookedMeshHeader = "ENERGY ENGINE MESH FILE v0.2.1\n";

    inline constexpr const char* BaseVertexShaderPath = "Engine/Shaders/default.vert.spv";
    inline constexpr const char* BaseFragmentShaderPath = "Engine/Shaders/default.frag.spv";
    inline constexpr const char* SkyVertexShaderPath = "Engine/Shaders/sky.vert.spv";
    inline constexpr const char* SkyFragmentShaderPath = "Engine/Shaders/sky.frag.spv";
    inline constexpr const char* ErrorModelFragmentShaderPath = "Engine/Shaders/error.frag.spv";
    inline constexpr const char* MissingFragmentShaderPath = "Engine/Shaders/missing.frag.spv";
    inline constexpr const char* PostVertexShaderPath = "Engine/Shaders/post.vert.spv";
    inline constexpr const char* PostFragmentShaderPath = "Engine/Shaders/post.frag.spv";
    inline constexpr const char* LightBakeComputeShaderPath = "Engine/Shaders/light_bake.comp.spv";

    inline constexpr std::uint32_t MaxCookedSubMeshes = 1000000u;
    inline constexpr std::uint32_t MaxCookedVerticesOrIndices = 100000000u;
    inline constexpr std::uint32_t MaxCookedStringBytes = 1048576u;
    static constexpr unsigned char SpirvMagic[] = { 0x03, 0x02, 0x23, 0x07 };
    inline std::string GetFileExtension(std::string_view value);
    enum class EnergyEngineFileFormat {
        Mesh,           // .eemf
        Texture,        // .eetf
        Vector,         // .eevf
        Shader,         // .eesf
        Level,          // .eelf
        Animation,      // .eeaf
        RenderMaterial, // .eerf
        UserInterface,  // .eeif
        Audio,          // .eeof
        Particle,       // .eexf
        Unknown
    };

    inline EnergyEngineFileFormat DetectFileFormat(std::string_view path) {
        const std::string extension = GetFileExtension(path);
        if (extension == ".eemf" || extension == ".obj" || extension == ".fbx" || extension == ".glb" || extension == ".gltf") return EnergyEngineFileFormat::Mesh;
        if (extension == ".eetf" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".hdr" || extension == ".psd" || extension == ".tga" || extension == ".gif" || extension == ".pic" || extension == ".pgm" || extension == ".ppm") return EnergyEngineFileFormat::Texture;
        if (extension == ".eevf" || extension == ".svg") return EnergyEngineFileFormat::Vector;
        if (extension == ".eesf" || extension == ".spv") return EnergyEngineFileFormat::Shader;
        if (extension == ".eelf") return EnergyEngineFileFormat::Level;
        if (extension == ".eeaf") return EnergyEngineFileFormat::Animation;
        if (extension == ".eerf") return EnergyEngineFileFormat::RenderMaterial;
        if (extension == ".eeif") return EnergyEngineFileFormat::UserInterface;
        if (extension == ".eeof") return EnergyEngineFileFormat::Audio;
        if (extension == ".eexf") return EnergyEngineFileFormat::Particle;
        return EnergyEngineFileFormat::Unknown;
    }

    inline std::string NormalizeAssetKey(std::string_view value) {
        if (value.empty()) return {};
        return std::filesystem::path(std::string(value)).lexically_normal().generic_string();
    }

    inline std::string GetFileExtension(std::string_view value) {
        const std::string key(value);
        const std::size_t dot = key.find_last_of('.');
        const std::size_t slash = key.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
        std::string extension = key.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    inline std::string ResolveAssetReferencePath(std::string_view reference, const std::filesystem::path& ownerAssetPath) {
        if (reference.empty()) return {};
        std::string normalizedSeparators(reference);
        std::replace(normalizedSeparators.begin(), normalizedSeparators.end(), '\\', '/');
        std::filesystem::path referencedPath(normalizedSeparators);
        if (referencedPath.is_absolute() || referencedPath.has_root_name())
            return NormalizeAssetKey(referencedPath.generic_string());
        const std::string normalizedReference = NormalizeAssetKey(referencedPath.generic_string());
        const std::filesystem::path normalizedPath(normalizedReference);
        if (normalizedPath.begin() != normalizedPath.end()) {
            std::string firstComponent = normalizedPath.begin()->string();
            std::transform(firstComponent.begin(), firstComponent.end(), firstComponent.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (firstComponent == "engine") return normalizedReference;
        }
        const std::filesystem::path ownerDirectory = ownerAssetPath.has_parent_path() ? ownerAssetPath.parent_path() : std::filesystem::path(".");
        const std::filesystem::path ownerRelative = (ownerDirectory / referencedPath).lexically_normal();
        std::error_code existsError;
        if (std::filesystem::exists(ownerRelative, existsError) && !existsError)
            return NormalizeAssetKey(ownerRelative.generic_string());
        existsError.clear();
        if (std::filesystem::exists(referencedPath, existsError) && !existsError)
            return normalizedReference;
        return NormalizeAssetKey(ownerRelative.generic_string());
    }

    inline std::string CookedMeshPathFor(std::string_view sourcePath) {
        std::filesystem::path path{ std::string(sourcePath) };
        if (GetFileExtension(sourcePath) != ".eemf") path.replace_extension(".eemf");
        return path.generic_string();
    }

    inline std::string BuiltInCookedMeshPath(std::string_view cacheName) {
        return (std::filesystem::path("Engine/GeneratedMeshes") / (std::string(cacheName) + ".eemf")).generic_string();
    }

    inline bool IsCookedMeshCacheCurrent(std::string_view sourcePath, std::string_view cookedPath) {
        std::error_code timeError;
        const bool cookedExists = std::filesystem::exists(std::filesystem::path(std::string(cookedPath)), timeError) && !timeError;
        if (!cookedExists) return false;
        std::error_code sourceTimeError, cookedTimeError;
        const auto sourceTime = std::filesystem::last_write_time(std::filesystem::path(std::string(sourcePath)), sourceTimeError);
        const auto cookedTime = std::filesystem::last_write_time(std::filesystem::path(std::string(cookedPath)), cookedTimeError);
        return sourceTimeError || cookedTimeError || cookedTime >= sourceTime;
    }

    inline bool SaveCookedMeshFile(const std::filesystem::path& path, const Vulkan::MeshData& data) {
        if (!data.IsValid() || data.subMeshes.size() > MaxCookedSubMeshes) return false;
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(CookedMeshHeader.data(), static_cast<std::streamsize>(CookedMeshHeader.size()));
            auto WriteBinary = [&file](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
                file.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
                return file.good();
                };
            auto WriteCookedString = [&file](const std::string& value) {
                if (value.size() > MaxCookedStringBytes) return false;
                const auto size = static_cast<std::uint32_t>(value.size());
                file.write(reinterpret_cast<const char*>(&size), sizeof(size));
                if (size > 0) file.write(value.data(), static_cast<std::streamsize>(size));
                return file.good();
                };
            if (!file.good()) return false;
            if (!WriteBinary(static_cast<std::uint32_t>(data.subMeshes.size()))) return false;
            for (const Vulkan::SubMeshData& sub : data.subMeshes) {
                if (sub.vertices.size() > MaxCookedVerticesOrIndices || sub.indices.size() > MaxCookedVerticesOrIndices) return false;
                if (!WriteBinary(static_cast<std::uint32_t>(sub.vertices.size())) || !WriteBinary(static_cast<std::uint32_t>(sub.indices.size())) || !WriteCookedString(sub.materialName) || !WriteBinary(sub.defaultMaterialColor[0]) || !WriteBinary(sub.defaultMaterialColor[1]) || !WriteBinary(sub.defaultMaterialColor[2]) || !WriteBinary(sub.defaultMaterialColor[3]) || !WriteCookedString(sub.defaultMaterialTexture)) return false;
                for (const Vulkan::MeshVertex& vertex : sub.vertices) {
                    for (float value : vertex.position) if (!WriteBinary(value)) return false;
                    for (float value : vertex.uv) if (!WriteBinary(value)) return false;
                    for (float value : vertex.lightmapUV) if (!WriteBinary(value)) return false;
                }
                for (std::uint32_t index : sub.indices) if (!WriteBinary(index)) return false;
            }
            file.flush();
            if (!file.good()) return false;
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec) { std::error_code removeEc; std::filesystem::remove(path, removeEc); ec.clear(); std::filesystem::rename(temporary, path, ec); }
        if (ec) { std::error_code ignored; std::filesystem::remove(temporary, ignored); return false; }
        return true;
    }

    inline bool LoadCookedMeshFile(const std::filesystem::path& path, Vulkan::MeshData& data) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        auto ReadBinary = [&file](auto& value) {
            using T = std::decay_t<decltype(value)>;
            static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
            file.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
            return file.good();
            };
        auto ReadCookedString = [&file](std::string& value) {
            std::uint32_t size = 0;
            file.read(reinterpret_cast<char*>(&size), sizeof(size));
            if (!file.good() || size > MaxCookedStringBytes) return false;
            value.resize(size);
            if (size != 0) file.read(value.data(), static_cast<std::streamsize>(size));
            return file.good();
            };
        std::string header(CookedMeshHeader.size(), '\0');
        file.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (!file.good()) {
            LOG_CATEGORY_ERROR("Assets", "Failed to read mesh file header '{}'", path.generic_string());
            return false;
        }
        if (std::string_view(header) != CookedMeshHeader) {
            LOG_CATEGORY_ERROR("Assets", "Mesh format version mismatch for '{}': expected '{}', got '{}'", path.generic_string(), CookedMeshHeader, header);
            return false;
        }
        std::uint32_t subMeshCount = 0;
        if (!ReadBinary(subMeshCount) || subMeshCount == 0 || subMeshCount > MaxCookedSubMeshes) {
            LOG_CATEGORY_ERROR("Assets", "Invalid mesh header or submesh count in '{}'", path.generic_string());
            return false;
        }
        Vulkan::MeshData loaded{};
        loaded.hasLightmapUVs = true;
        loaded.subMeshes.reserve(subMeshCount);
        std::uint64_t totalVertices = 0, totalIndices = 0;
        for (std::uint32_t sm = 0; sm < subMeshCount; ++sm) {
            std::uint32_t vertexCount = 0, indexCount = 0;
            Vulkan::SubMeshData sub{};
            if (!ReadBinary(vertexCount) || !ReadBinary(indexCount) || vertexCount == 0 || indexCount == 0 || indexCount % 3u != 0u || vertexCount > MaxCookedVerticesOrIndices || indexCount > MaxCookedVerticesOrIndices || !ReadCookedString(sub.materialName) || !ReadBinary(sub.defaultMaterialColor[0]) || !ReadBinary(sub.defaultMaterialColor[1]) || !ReadBinary(sub.defaultMaterialColor[2]) || !ReadBinary(sub.defaultMaterialColor[3]) || !ReadCookedString(sub.defaultMaterialTexture)) return false;
            totalVertices += vertexCount;
            totalIndices += indexCount;
            if (totalVertices > MaxCookedVerticesOrIndices || totalIndices > MaxCookedVerticesOrIndices) return false;
            sub.vertices.resize(vertexCount);
            sub.indices.resize(indexCount);
            for (Vulkan::MeshVertex& vertex : sub.vertices) {
                for (float& value : vertex.position) if (!ReadBinary(value)) return false;
                for (float& value : vertex.uv) if (!ReadBinary(value)) return false;
                for (float& value : vertex.lightmapUV) if (!ReadBinary(value)) return false;
            }
            for (std::uint32_t& index : sub.indices) if (!ReadBinary(index)) return false;
            if (!sub.IsValid()) return false;
            loaded.subMeshes.push_back(std::move(sub));
        }
        char trailing = 0;
        if (file.read(&trailing, 1) || !file.eof()) return false;
        data = std::move(loaded);
        return data.IsValid();
    }

    inline bool LoadOBJMesh(std::string_view filename, Vulkan::MeshData& data) {
        const std::string key = NormalizeAssetKey(filename);
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> objMaterials;
        std::string err;
        const std::filesystem::path objPath(key);
        std::string materialDirectory = objPath.has_parent_path() ? objPath.parent_path().string() : std::string(".");
        if (!materialDirectory.empty() && materialDirectory.back() != '/' && materialDirectory.back() != '\\')
            materialDirectory.push_back(std::filesystem::path::preferred_separator);
        const bool loaded = tinyobj::LoadObj(&attrib, &shapes, &objMaterials, &err, key.c_str(), materialDirectory.c_str(), true);
        if (!err.empty()) {
            if (loaded) LOG_CATEGORY_WARN("Vulkan", "OBJ warning for '{}': {}", key, err);
            else {
                LOG_CATEGORY_ERROR("Vulkan", "OBJ error loading '{}': {}", key, err);
                return false;
            }
        }
        if (attrib.vertices.empty()) {
            LOG_CATEGORY_ERROR("Vulkan", "OBJ '{}' contains no vertices", key);
            return false;
        }
        std::unordered_map<int, std::size_t> subMeshForMaterial;
        std::vector<std::unordered_map<std::uint64_t, std::uint32_t>> vertexLookupBySubMesh;
        auto getSubMeshIndex = [&](int materialId) -> std::size_t {
            const auto found = subMeshForMaterial.find(materialId);
            if (found != subMeshForMaterial.end()) return found->second;
            Vulkan::SubMeshData subMesh{};
            if (materialId >= 0 && static_cast<std::size_t>(materialId) < objMaterials.size()) {
                const tinyobj::material_t& sourceMaterial = objMaterials[static_cast<std::size_t>(materialId)];
                subMesh.materialName = sourceMaterial.name;
                subMesh.defaultMaterialColor = { sourceMaterial.diffuse[0], sourceMaterial.diffuse[1], sourceMaterial.diffuse[2], sourceMaterial.dissolve };
                if (!sourceMaterial.diffuse_texname.empty()) subMesh.defaultMaterialTexture = ResolveAssetReferencePath(sourceMaterial.diffuse_texname, std::filesystem::path(key));
            }
            const std::size_t subMeshIndex = data.subMeshes.size();
            data.subMeshes.push_back(std::move(subMesh));
            vertexLookupBySubMesh.emplace_back();
            subMeshForMaterial.emplace(materialId, subMeshIndex);
            return subMeshIndex;
            };
        struct ImportedCorner {
            Vulkan::MeshVertex vertex{};
            int positionIndex = -1;
            int texcoordIndex = -1;
        };
        for (const tinyobj::shape_t& shape : shapes) {
            std::size_t indexOffset = 0;
            for (std::size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
                const std::size_t faceVertexCount = shape.mesh.num_face_vertices[face];
                if (indexOffset + faceVertexCount > shape.mesh.indices.size()) {
                    LOG_CATEGORY_ERROR("Vulkan", "OBJ '{}' has inconsistent face/index data", key);
                    return false;
                }
                int materialId = -1;
                if (face < shape.mesh.material_ids.size()) materialId = shape.mesh.material_ids[face];
                std::vector<ImportedCorner> faceVertices;
                faceVertices.reserve(faceVertexCount);
                for (std::size_t corner = 0; corner < faceVertexCount; ++corner) {
                    const tinyobj::index_t& index = shape.mesh.indices[indexOffset + corner];
                    if (index.vertex_index < 0 || static_cast<std::size_t>(index.vertex_index) * 3 + 2 >= attrib.vertices.size()) {
                        LOG_CATEGORY_ERROR("Vulkan", "OBJ '{}' contains an invalid vertex index", key);
                        return false;
                    }
                    ImportedCorner imported{};
                    imported.positionIndex = index.vertex_index;
                    imported.texcoordIndex = index.texcoord_index;
                    imported.vertex.position[0] = attrib.vertices[3 * index.vertex_index + 0];
                    imported.vertex.position[1] = attrib.vertices[3 * index.vertex_index + 1];
                    imported.vertex.position[2] = attrib.vertices[3 * index.vertex_index + 2];
                    if (index.texcoord_index >= 0 && static_cast<std::size_t>(index.texcoord_index) * 2 + 1 < attrib.texcoords.size()) {
                        imported.vertex.uv[0] = attrib.texcoords[2 * index.texcoord_index + 0];
                        imported.vertex.uv[1] = attrib.texcoords[2 * index.texcoord_index + 1];
                    }
                    faceVertices.push_back(imported);
                }
                if (faceVertexCount >= 3) {
                    const std::size_t subMeshIndex = getSubMeshIndex(materialId);
                    Vulkan::SubMeshData& subMesh = data.subMeshes[subMeshIndex];
                    auto& vertexLookup = vertexLookupBySubMesh[subMeshIndex];
                    for (std::size_t tri = 1; tri + 1 < faceVertexCount; ++tri) {
                        const ImportedCorner triangle[] = { faceVertices[0], faceVertices[tri], faceVertices[tri + 1] };
                        for (const ImportedCorner& corner : triangle) {
                            const std::uint64_t vertexKey = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(corner.positionIndex)) << 32) | static_cast<std::uint32_t>(corner.texcoordIndex);
                            auto found = vertexLookup.find(vertexKey);
                            if (found == vertexLookup.end()) {
                                if (subMesh.vertices.size() >= (std::numeric_limits<std::uint32_t>::max)()) {
                                    LOG_CATEGORY_ERROR("Vulkan", "OBJ '{}' has too many vertices in one material group", key);
                                    return false;
                                }
                                const auto localIndex = static_cast<std::uint32_t>(subMesh.vertices.size());
                                subMesh.vertices.push_back(corner.vertex);
                                found = vertexLookup.emplace(vertexKey, localIndex).first;
                            }
                            subMesh.indices.push_back(found->second);
                        }
                    }
                }
                indexOffset += faceVertexCount;
            }
            if (indexOffset != shape.mesh.indices.size()) {
                LOG_CATEGORY_ERROR("Vulkan", "OBJ '{}' has trailing/inconsistent face index data", key);
                return false;
            }
        }
        data.subMeshes.erase(std::remove_if(data.subMeshes.begin(), data.subMeshes.end(), [](const Vulkan::SubMeshData& subMesh) { return !subMesh.IsValid(); }), data.subMeshes.end());
        if (!data.IsValid()) {
            LOG_CATEGORY_ERROR("Vulkan", "OBJ '{}' produced invalid mesh data", key);
            return false;
        }
        return true;
    }

    enum class MeshFileLoadStatus {
        Success,
        InvalidCookedFile,
        ReleaseRequiresCookedMesh,
        UnsupportedFormat,
        SourceParseFailed
    };

    struct MeshFileLoadResult {
        MeshFileLoadStatus status = MeshFileLoadStatus::Success;
        std::string extension;
        std::string cookedPath;
        bool loadedCooked = false;
        bool Succeeded() const noexcept { return status == MeshFileLoadStatus::Success; }
    };

    inline MeshFileLoadResult LoadMeshAssetFile(std::string_view filename, Vulkan::MeshData& data, bool requireCookedMeshForObj) {
        MeshFileLoadResult result{};
        const std::string key = NormalizeAssetKey(filename);
        result.extension = GetFileExtension(key);
        result.cookedPath = CookedMeshPathFor(key);
        const bool requestedCooked = result.extension == ".eemf";
        if (requestedCooked) {
            result.loadedCooked = LoadCookedMeshFile(key, data);
            if (!result.loadedCooked)
                result.status = MeshFileLoadStatus::InvalidCookedFile;
            return result;
        }
        if (IsCookedMeshCacheCurrent(key, result.cookedPath))
            result.loadedCooked = LoadCookedMeshFile(result.cookedPath, data);
        if (requireCookedMeshForObj && result.extension == ".obj" && !result.loadedCooked) {
            result.status = MeshFileLoadStatus::ReleaseRequiresCookedMesh;
            return result;
        }
        if (result.loadedCooked) return result;
        if (result.extension == ".obj") {
            LOG_CATEGORY_INFO("Lighting", "Cooking mesh '{}' with xatlas because no valid cooked cache was found", key);
            if (!LoadOBJMesh(key, data)) result.status = MeshFileLoadStatus::SourceParseFailed;
            return result;
        }
        result.status = MeshFileLoadStatus::UnsupportedFormat;
        return result;
    }

    struct DecodedTexture {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<unsigned char> rgba;
    };

    inline bool LoadTextureImage(std::string_view filename, DecodedTexture& output, std::string& error) {
        const std::string key = NormalizeAssetKey(filename);
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* pixels = stbi_load(key.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels || width <= 0 || height <= 0) {
            error = stbi_failure_reason() ? stbi_failure_reason() : "unknown image error";
            if (pixels) stbi_image_free(pixels);
            return false;
        }
        const std::uint64_t byteCount64 = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u;
        if (byteCount64 > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
            stbi_image_free(pixels);
            error = "decoded image is too large";
            return false;
        }
        const std::size_t byteCount = static_cast<std::size_t>(byteCount64);
        output.width = static_cast<std::uint32_t>(width);
        output.height = static_cast<std::uint32_t>(height);
        output.rgba.assign(pixels, pixels + byteCount);
        stbi_image_free(pixels);
        return true;
    }

    inline bool SaveShaderEESFFile(const std::filesystem::path& path, const std::vector<char>& bytecode, std::string& error) {
        if (!(bytecode.size() >= sizeof(SpirvMagic) && bytecode.size() % sizeof(std::uint32_t) == 0 && std::memcmp(bytecode.data(), SpirvMagic, sizeof(SpirvMagic)) == 0)) {
            error = "Refusing to write invalid SPIR-V bytecode";
            return false;
        }
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "Could not create shader directory: " + ec.message();
            return false;
        }
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                error = "Could not open temporary shader file for writing";
                return false;
            }
            file.write(CookedShaderHeader.data(), static_cast<std::streamsize>(CookedShaderHeader.size()));
            file.write(bytecode.data(), static_cast<std::streamsize>(bytecode.size()));
            file.flush();
            if (!file.good()) {
                error = "Could not finish writing shader file";
                file.close();
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec) { std::error_code removeError; std::filesystem::remove(path, removeError); ec.clear(); std::filesystem::rename(temporary, path, ec); }
        if (ec) { error = "Could not replace shader file: " + ec.message(); std::error_code ignored; std::filesystem::remove(temporary, ignored); return false; }
        error.clear();
        return true;
    }

    inline bool LoadShaderFile(std::string_view filename, std::vector<char>& bytecode, std::string& error) {
        const std::string key = NormalizeAssetKey(filename);
        const std::string extension = GetFileExtension(key);
        if (extension != ".spv" && extension != ".eesf") {
            error = "Unsupported shader file extension '" + extension + "'";
            return false;
        }
        std::ifstream file(key, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            error = "Could not open shader file '" + key + "'";
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
            error = "Shader file is empty or too large: '" + key + "'";
            return false;
        }
        std::vector<char> fileBytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(fileBytes.data(), size)) {
            error = "Could not fully read shader file '" + key + "'";
            return false;
        }
        std::size_t payloadOffset = 0;
        bool hasHeader = true;
        payloadOffset = 0;
        if (fileBytes.size() < CookedShaderHeaderText.size() + 1 || std::memcmp(fileBytes.data(), CookedShaderHeaderText.data(), CookedShaderHeaderText.size()) != 0)
            hasHeader = false;
        else {
            payloadOffset = CookedShaderHeaderText.size();
            if (fileBytes[payloadOffset] == '\r') ++payloadOffset;
            if (payloadOffset >= fileBytes.size() || fileBytes[payloadOffset] != '\n') {
                payloadOffset = 0;
                hasHeader = false;
            }
            else
                ++payloadOffset;
        }
        if (extension == ".eesf" && !hasHeader) {
            error = "Shader file has an invalid or unsupported EESF header: '" + key + "'";
            return false;
        }
        if (extension == ".spv" && !hasHeader) payloadOffset = 0;
        if (payloadOffset >= fileBytes.size()) {
            error = "Shader file contains no SPIR-V payload: '" + key + "'";
            return false;
        }
        std::vector<char> loaded(fileBytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset), fileBytes.end());
        if (!(loaded.size() >= sizeof(SpirvMagic) && loaded.size() % sizeof(std::uint32_t) == 0 && std::memcmp(loaded.data(), SpirvMagic, sizeof(SpirvMagic)) == 0)) {
            error = "Shader payload has an invalid SPIR-V magic value or byte length: '" + key + "'";
            return false;
        }
        bytecode = std::move(loaded);
        error.clear();
        return true;
    }

    inline std::vector<char> ReadShaderFile(const char* filename) {
        if (!filename || !*filename) {
            LOG_CATEGORY_ERROR("Vulkan", "Cannot load an empty shader filename");
            return {};
        }
        std::string normalizedLookupPath = filename;
        const auto marker = normalizedLookupPath.rfind("Engine/Shaders/");
        if (marker != std::string::npos) normalizedLookupPath = normalizedLookupPath.substr(marker);
        std::replace(normalizedLookupPath.begin(), normalizedLookupPath.end(), '\\', '/');
        std::filesystem::path embeddedSPVPath(normalizedLookupPath);
        if (GetFileExtension(normalizedLookupPath) == ".eesf") embeddedSPVPath.replace_extension(".spv");
        const std::string embeddedSPVKey = embeddedSPVPath.generic_string();
        const std::string requestedExtension = GetFileExtension(filename);
        std::vector<std::filesystem::path> candidates;
        // LOAD ANY SHADER FILE FORMAT
        if (requestedExtension != ".spv" && requestedExtension != ".eesf") {
            std::filesystem::path eesfPath(filename);
            eesfPath += ".eesf";
            candidates.push_back(std::move(eesfPath));
            std::filesystem::path spvPath(filename);
            spvPath += ".spv";
            candidates.push_back(std::move(spvPath));
        }
        else {
            // TRY TO LOAD SPECIFIED SHADER FORMAT AND THEN OTHER SUPPORTED FORMAT
            candidates.emplace_back(filename);
            if (requestedExtension == ".spv" || requestedExtension == ".eesf") {
                std::filesystem::path alternate(filename);
                alternate.replace_extension(requestedExtension == ".spv" ? ".eesf" : ".spv");
                if (alternate != candidates.front()) candidates.push_back(std::move(alternate));
            }
        }
        std::string lastError;
        for (const auto& candidate : candidates) {
            std::vector<char> loaded;
            std::string error;
            if (LoadShaderFile(candidate.generic_string(), loaded, error)) return loaded;
            lastError = std::move(error);
        }
        std::vector<char> embedded;
#if defined(_WIN32)
        int resourceId = 0;
        if (embeddedSPVKey == BaseFragmentShaderPath) resourceId = IDR_DEFAULT_FRAG;
        else if (embeddedSPVKey == BaseVertexShaderPath) resourceId = IDR_DEFAULT_VERT;
        else if (embeddedSPVKey == ErrorModelFragmentShaderPath) resourceId = IDR_ERROR_FRAG;
        else if (embeddedSPVKey == MissingFragmentShaderPath) resourceId = IDR_MISSING_FRAG;
        else if (embeddedSPVKey == PostFragmentShaderPath) resourceId = IDR_POST_FRAG;
        else if (embeddedSPVKey == PostVertexShaderPath) resourceId = IDR_POST_VERT;
        else if (embeddedSPVKey == SkyFragmentShaderPath) resourceId = IDR_SKY_FRAG;
        else if (embeddedSPVKey == SkyVertexShaderPath) resourceId = IDR_SKY_VERT;
        if (resourceId != 0) {
            const HMODULE module = GetModuleHandleW(nullptr);
            const HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
            if (resource) {
                const DWORD size = SizeofResource(module, resource);
                const HGLOBAL loadedResource = LoadResource(module, resource);
                const void* data = loadedResource ? LockResource(loadedResource) : nullptr;
                if (size > 0 && data) {
                    embedded.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
                    if (embedded.size() >= sizeof(SpirvMagic) && embedded.size() % sizeof(std::uint32_t) == 0 && std::memcmp(embedded.data(), SpirvMagic, sizeof(SpirvMagic)) == 0) return embedded;
                    LOG_CATEGORY_WARN("Vulkan", "Embedded shader '{}' has invalid SPIR-V bytecode; trying disk", filename);
                    embedded.clear();
                }
            }
        }
#elif defined(ENERGYENGINE_HAS_GENERATED_EMBEDDED_SHADERS)
        const std::string embeddedRequestedKey = normalizedLookupPath;
        const std::string* embeddedKeys[] = { &embeddedSPVKey, &embeddedRequestedKey };
        for (const std::string* embeddedKey : embeddedKeys) {
            if (*embeddedKey == embeddedSPVKey && embeddedKey != embeddedKeys[0]) continue;
            if (const auto* blob = EnergyEngineEmbeddedShaders::Find(*embeddedKey)) {
                if (blob->data && blob->size > 0) {
                    embedded.assign(reinterpret_cast<const char*>(blob->data), reinterpret_cast<const char*>(blob->data) + blob->size);
                    if (embedded.size() >= sizeof(SpirvMagic) && embedded.size() % sizeof(std::uint32_t) == 0 && std::memcmp(embedded.data(), SpirvMagic, sizeof(SpirvMagic)) == 0) return embedded;
                    LOG_CATEGORY_WARN("Vulkan", "Embedded shader '{}' has invalid SPIR-V bytecode; trying disk", filename);
                    embedded.clear();
                }
            }
        }
#else
        (void)embeddedSPVKey;
#endif
        LOG_CATEGORY_ERROR("Vulkan", "Could not load shader '{}' as .spv or .eesf: {}", filename, lastError);
        return {};
    }

    inline bool LoadMaterialAssetFile(std::string_view filename, Vulkan::MaterialDesc& desc, std::string& error) {
        const std::string key = NormalizeAssetKey(filename);
        std::ifstream file(key);
        if (!file.is_open()) {
            error = "Could not open material asset '" + key + "'";
            return false;
        }
        desc = Vulkan::MaterialDesc{};
        desc.name = key;
        std::string line;
        while (std::getline(file, line)) {
            line = Trim(std::move(line));
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const std::string field = ToLower(Trim(line.substr(0, equals)));
            const std::string value = Trim(line.substr(equals + 1));
            if (field == "name" || field == "materialname") desc.name = value;
            else if (field == "shader" || field == "shaderpath") desc.fragmentShaderSpv = value;
            else if (field == "twosided" || field == "doublesided") {
                std::string lowerVal = ToLower(value);
                desc.twoSided = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
                desc.cullMode = desc.twoSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            }
            else if (field == "cullmode" || field == "cull") {
                std::string lowerVal = ToLower(value);
                if (lowerVal == "none") desc.cullMode = VK_CULL_MODE_NONE;
                else if (lowerVal == "front") desc.cullMode = VK_CULL_MODE_FRONT_BIT;
                else if (lowerVal == "back") desc.cullMode = VK_CULL_MODE_BACK_BIT;
                else if (lowerVal == "frontandback") desc.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
            }
            else if (field == "polygonmode" || field == "fillmode" || field == "drawmode") {
                std::string lowerVal = ToLower(value);
                if (lowerVal == "line" || lowerVal == "wireframe") desc.polygonMode = VK_POLYGON_MODE_LINE;
                else if (lowerVal == "point") desc.polygonMode = VK_POLYGON_MODE_POINT;
                else desc.polygonMode = VK_POLYGON_MODE_FILL;
            }
            else if (field == "depthbias") {
                std::string lowerVal = ToLower(value);
                desc.enableDepthBias = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes");
            }
            else if (field == "linewidth") {
                try { desc.lineWidth = std::stof(value); }
                catch (...) {}
            }
            else if (field == "uv" || field == "matuv" || field == "materialuv") {
                const auto n = ParseNumbers(value);
                if (n.size() >= 2) desc.matUV = { n[0], n[1], n.size() > 2 ? n[2] : 0.0f, n.size() > 3 ? n[3] : 0.0f };
            }
            else if (field == "lit" || field == "receiveslighting" || field == "receives_light") {
                const std::string lowerVal = ToLower(value);
                desc.lit = (lowerVal == "true" || lowerVal == "1" || lowerVal == "yes" || lowerVal == "on");
            }
            else if (field == "colormap") desc.colorMap = value;
            else if (field == "color") {
                const auto n = ParseNumbers(value);
                if (n.size() >= 3) desc.color = { n[0], n[1], n[2], n.size() > 3 ? n[3] : 1.0f };
            }
            else if (field == "coluv" || field == "coloruv" || field == "colmapuv" || field == "colormapuv") {
                const auto n = ParseNumbers(value);
                if (n.size() >= 2) desc.colorMapUV = { n[0], n[1], n.size() > 2 ? n[2] : 0.0f, n.size() > 3 ? n[3] : 0.0f };
            }
            else if (field.rfind("float.", 0) == 0) {
                const auto n = ParseNumbers(value);
                const std::string name = field.substr(6);
                if (n.size() >= 2) { desc.parameterSlots[name] = static_cast<std::uint32_t>((std::max)(0.0f, n[0])); desc.floatParameters[name] = n[1]; }
            }
            else if (field.rfind("vec4.", 0) == 0) {
                const auto n = ParseNumbers(value);
                const std::string name = field.substr(5);
                if (n.size() >= 5) { desc.parameterSlots[name] = static_cast<std::uint32_t>((std::max)(0.0f, n[0])); desc.vec4Parameters[name] = { n[1], n[2], n[3], n[4] }; }
            }
            else if (field.rfind("texture.", 0) == 0) {
                const auto comma = value.find(',');
                if (comma != std::string::npos) {
                    const std::string name = field.substr(8);
                    try { desc.textureSlots[name] = static_cast<std::uint32_t>(std::stoul(Trim(value.substr(0, comma)))); }
                    catch (...) { desc.textureSlots[name] = 0; }
                    desc.textures[name] = Trim(value.substr(comma + 1));
                }
            }
        }
        const std::filesystem::path ownerPath(key);
        auto resolveTexturePath = [&](std::string& texturePath) { texturePath = ResolveAssetReferencePath(texturePath, ownerPath); };
        resolveTexturePath(desc.colorMap);
        for (auto& [textureName, texturePath] : desc.textures) { (void)textureName; resolveTexturePath(texturePath); }
        return true;
    }

    enum class MetadataWriteStatus { Success, DirectoryCreationFailed, OpenFailed, WriteFailed };
    struct MetadataWriteOutcome {
        MetadataWriteStatus status = MetadataWriteStatus::Success;
        std::string path;
        std::string error;
    };

    inline MetadataWriteOutcome SaveLightmapUVTestMetadata(std::string_view levelName, std::string_view version, std::uint32_t resolution, std::uint32_t pages, std::size_t packedInstances) {
        MetadataWriteOutcome result{};
        const std::filesystem::path filePath = std::filesystem::path("Engine/BakedLights") / (std::string(levelName) + ".uvtest.lightmap");
        result.path = filePath.generic_string();
        std::error_code filesystemError;
        std::filesystem::create_directories(filePath.parent_path(), filesystemError);
        if (filesystemError) {
            result.status = MetadataWriteStatus::DirectoryCreationFailed;
            result.error = filesystemError.message();
            return result;
        }
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            result.status = MetadataWriteStatus::OpenFailed;
            return result;
        }
        file << "# ENERGY ENGINE LIGHTMAP ATLAS UV TEST (NOT BAKED LIGHTING)\n";
        file << "version = " << version << "\n";
        file << "level = " << levelName << "\n";
        file << "kind = atlas_uv_test\n";
        file << "format = rgba8\n";
        file << "atlas_resolution = " << resolution << "\n";
        file << "atlas_pages = " << pages << "\n";
        file << "packed_instances = " << packedInstances << "\n";
        file.close();
        if (!file) result.status = MetadataWriteStatus::WriteFailed;
        return result;
    }
}