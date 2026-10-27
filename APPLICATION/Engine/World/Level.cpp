//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Load levels and maintain            //
//           declared asset dependencies.       //
//                                              //
//==============================================//

#include "Level.h"
#include "Camera.h"
#include "../Main/VulkanContext.h"
#include "../Main/Logger.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace Engine {
    Level::Level(std::string name, unsigned int uid) : LevelName(std::move(name)), LevelUID(uid) { CurrentSky = std::make_unique<Sky>(); CurrentSky->ParentLevel = this; }

    void Level::AddObject(std::unique_ptr<LevelObject> object) {
        if (!object) return;
        object->ParentLevel = this;
        const std::string className = object->ClassName();
        LevelObject* inserted = nullptr;
        const bool previousDeferral = AssetMutationDeferred;
        AssetMutationDeferred = true;
        if (className == "Sky") {
            SetCurrentSky(std::unique_ptr<Sky>(static_cast<Sky*>(object.release())));
            inserted = CurrentSky.get();
        }
        else if (className == "Light") {
            Lights.emplace_back(static_cast<Light*>(object.release()));
            inserted = Lights.back().get();
        }
        else {
            Objects.push_back(std::move(object));
            inserted = Objects.back().get();
            if (className == "Mesh") {
                auto* meshPtr = static_cast<Mesh*>(inserted);
                if (meshPtr->GetMeshAssetId() != 0) EverUsedMeshAssetIds.emplace(meshPtr->GetMeshAssetId());
                for (const std::uint32_t id : meshPtr->GetMaterialIds())
                    if (id != 0) EverUsedMaterialIds.emplace(id);
            }
        }
        if (inserted) inserted->OnSpawned();
        AssetMutationDeferred = previousDeferral;
        if (AssetsAcquired && !AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*this);
    }

    void Level::SetCurrentSky(std::unique_ptr<Sky> sky) {
        if (CurrentSky && CurrentSky.get() != sky.get() && CurrentSky->RuntimeMaterialId != 0 && Vulkan::Get().IsInitialized())
            Vulkan::Get().ReleaseSkyMaterialInstance(*CurrentSky);
        CurrentSky = std::move(sky);
        if (!CurrentSky) return;
        CurrentSky->ParentLevel = this;
        if (CurrentSky->MaterialId != 0) EverUsedMaterialIds.emplace(CurrentSky->MaterialId);
        if (AssetsAcquired && !AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*this);
    }

    void Level::SetCurrentPlayer(std::unique_ptr<Player> player, bool respawn) {
        const bool previousDeferral = AssetMutationDeferred;
        AssetMutationDeferred = true;
        CurrentPlayer = std::move(player);
        if (CurrentPlayer) {
            CurrentPlayer->ParentLevel = this;
            CurrentPlayer->OnSpawned();
            if (respawn) CurrentPlayer->Respawn();
        }
        AssetMutationDeferred = previousDeferral;
        if (AssetsAcquired && !AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*this);
    }

    void Level::SetSunAngles(float azimuthDegrees, float elevationDegrees) noexcept {
        const float azimuth = DegreesToRadians(azimuthDegrees);
        const float elevation = DegreesToRadians(elevationDegrees);
        const float horizontal = std::cos(elevation);
        SunDirection = Vector3(static_cast<double>(horizontal * std::cos(azimuth)), static_cast<double>(horizontal * std::sin(azimuth)), static_cast<double>(std::sin(elevation)));
    }

    void Level::DeclareMeshDependency(std::string path) {
        if (!path.empty()) DeclaredMeshAssetPaths.emplace(std::move(path));
        if (AssetsAcquired && !AssetMutationDeferred && Vulkan::Get().IsInitialized()) (void)Vulkan::Get().AcquireLevelAssets(*this);
    }
    void Level::DeclareMaterialDependency(std::string path) {
        if (!path.empty()) DeclaredMaterialAssetPaths.emplace(std::move(path));
        if (AssetsAcquired && !AssetMutationDeferred && Vulkan::Get().IsInitialized()) (void)Vulkan::Get().AcquireLevelAssets(*this);
    }
    void Level::DeclareTextureDependency(std::string path) {
        if (!path.empty()) DeclaredTextureAssetPaths.emplace(std::move(path));
        if (AssetsAcquired && !AssetMutationDeferred && Vulkan::Get().IsInitialized()) (void)Vulkan::Get().AcquireLevelAssets(*this);
    }

    std::shared_ptr<Level> Level::LoadLevel(std::string_view name, unsigned int uid, bool spawnPlayer, bool spawnEntities) {
        auto level = std::shared_ptr<Level>(new Level(std::string(name), uid));

        // TEST LEVEL
        auto spawn = std::make_unique<SpawnLocation>();
        spawn->SetPosition(Vector3(10, 0, 5));
        spawn->SetSize(Vector3(5, 5, 5));
        level->AddObject(std::move(spawn));
        auto plane = std::make_unique<Mesh>();
        plane->SetSize(Vector3(100, 100, 0));
        plane->SetMeshType(Mesh::Plane);
        plane->SetMaterialAsset("Engine/Materials/Grass.material");
        level->AddObject(std::move(plane));
        auto cube = std::make_unique<Mesh>();
        cube->SetPosition(Vector3(10, 0, 5));
        cube->SetRotation(Vector3(0, 45, 0));
        cube->SetMeshType(Mesh::Cube);
        cube->SetMaterialAsset("Engine/Materials/Test.material");
        level->AddObject(std::move(cube));
        auto monkey = std::make_unique<Mesh>();
        monkey->SetRotation(Vector3(90, 0, 0));
        monkey->SetPosition(Vector3(0, 0, 5));
        monkey->SetMeshAsset("Engine/Assets/monkey.obj");
        monkey->SetMaterialAsset("Engine/Materials/RandomShit.material");
        level->AddObject(std::move(monkey));
        auto teapot = std::make_unique<Mesh>();
        teapot->SetRotation(Vector3(90, 0, 0));
        teapot->SetPosition(Vector3(5, 0, 4));
        teapot->SetSize(Vector3(.5, .5, .5));
        teapot->SetMeshAsset("Engine/Assets/teapot.obj");
        level->AddObject(std::move(teapot));
        auto multimaterialtest = std::make_unique<Mesh>();
        multimaterialtest->SetRotation(Vector3(90, 0, 0));
        multimaterialtest->SetPosition(Vector3(-5, 0, 5));
        multimaterialtest->SetSize(Vector3(.5, .5, .5));
        multimaterialtest->SetMeshAsset("Engine/Assets/multimaterialtest.obj");
        multimaterialtest->SetMaterialAsset("Engine/Materials/Test.material");
        multimaterialtest->SetMaterialAsset("Engine/Materials/TestToo.material", 1);
        level->AddObject(std::move(multimaterialtest));
        auto sphere = std::make_unique<Mesh>();
        sphere->SetPosition(Vector3(15, 0, 5));
        sphere->SetMeshType(Mesh::Sphere);
        level->AddObject(std::move(sphere));
        auto cylinder = std::make_unique<Mesh>();
        cylinder->SetPosition(Vector3(20, 0, 5));
        cylinder->SetMeshType(Mesh::Cylinder);
        level->AddObject(std::move(cylinder));
        auto light = std::make_unique<Light>();
        light->SetPosition(Vector3(22.5, 0, 5));
        light->SetColor(Vector3(255, 0, 0));
        level->AddObject(std::move(light));
        auto wedge = std::make_unique<Mesh>();
        wedge->SetPosition(Vector3(25, 0, 5));
        wedge->SetMeshType(Mesh::Wedge);
        level->AddObject(std::move(wedge));
        auto cornerwedge = std::make_unique<Mesh>();
        cornerwedge->SetPosition(Vector3(30, 0, 5));
        cornerwedge->SetMeshType(Mesh::CornerWedge);
        level->AddObject(std::move(cornerwedge));
        auto pyramid = std::make_unique<Mesh>();
        pyramid->SetPosition(Vector3(35, 0, 5));
        pyramid->SetMeshType(Mesh::Pyramid);
        level->AddObject(std::move(pyramid));
        auto cone = std::make_unique<Mesh>();
        cone->SetPosition(Vector3(40, 0, 5));
        cone->SetMeshType(Mesh::Cone);
        level->AddObject(std::move(cone));
        auto error = std::make_unique<Mesh>();
        error->SetPosition(Vector3(-10, 5, 2));
        error->SetMeshType(Mesh::Custom);
        level->AddObject(std::move(error));
        level->GetCurrentSky()->SetMode(Sky::SkyBox);
        // level->GetCurrentSky()->SetMaterialAsset("Engine/Materials/SkyTest.material");
        // level->GetCurrentSky()->SetSunTextureAsset("Engine/Main/YuY.png");
        auto whitewall = std::make_unique<Mesh>();
        whitewall->SetPosition(Vector3(0, 10, 2.5));
        whitewall->SetSize(Vector3(10, 1, 5));
        whitewall->SetMeshType(Mesh::Cube);
        whitewall->SetMaterialAsset("Engine/Materials/White.material");
        level->AddObject(std::move(whitewall));
        auto redcube = std::make_unique<Mesh>();
        redcube->SetPosition(Vector3(0, 8.5, 1));
        redcube->SetSize(Vector3(2, 2, 2));
        redcube->SetMeshType(Mesh::Cube);
        redcube->SetMaterialAsset("Engine/Materials/Red.material");
        level->AddObject(std::move(redcube));

        if (spawnPlayer) {
            std::unique_ptr<Player> playerInstance;
            if (level->CustomPlayerFactoryOverride) playerInstance = level->CustomPlayerFactoryOverride();
            else {
                auto& manager = LevelManager::GetInstance();
                if (manager.CustomPlayerFactory) playerInstance = manager.CustomPlayerFactory();
                else playerInstance = std::make_unique<Player>();
            }
            if (playerInstance) level->SetCurrentPlayer(std::move(playerInstance), true);
        }
        (void)spawnEntities;
        return level;
    }

    void Level::Update(double deltaTime) noexcept {
        if (isPaused) return;
        if (CurrentPlayer) CurrentPlayer->Update(deltaTime);
        for (auto& obj : Objects) if (obj) obj->Update(deltaTime);
    }

    void Level::Mesh::SetMeshType(const MeshType& newMeshType) {
        BaseMeshType = newMeshType;
        CustomMesh.clear();
        MeshAssetId = 0;
        if (ParentLevel && ParentLevel->AssetsAcquired && !ParentLevel->AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*ParentLevel);
    }

    void Level::Mesh::SetMeshAsset(std::string mesh) {
        CustomMesh = std::move(mesh);
        BaseMeshType = MeshType::Custom;
        MeshAssetId = 0;
        if (ParentLevel && ParentLevel->AssetsAcquired && !ParentLevel->AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*ParentLevel);
    }

    void Level::Mesh::SetMaterialAsset(std::string material, std::uint32_t index) {
        if (index >= MaterialAssets.size()) MaterialAssets.resize(static_cast<std::size_t>(index) + 1);
        MaterialAssets[index] = std::move(material);
        if (index >= MaterialIds.size()) MaterialIds.resize(static_cast<std::size_t>(index) + 1, 0);
        MaterialIds[index] = 0;
        if (ParentLevel && ParentLevel->AssetsAcquired && !ParentLevel->AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*ParentLevel);
    }

    void Level::Sky::SetMaterialAsset(std::string material) {
        MaterialAsset = std::move(material);
        MaterialId = 0;
        HasAppliedSunTextureState = false;
        if (ParentLevel && ParentLevel->AssetsAcquired && !ParentLevel->AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*ParentLevel);
    }

    void Level::Sky::SetSunTextureAsset(std::string texture) {
        SunTextureAsset = std::move(texture);
        HasAppliedSunTextureState = false;
        if (ParentLevel && ParentLevel->AssetsAcquired && !ParentLevel->AssetMutationDeferred && Vulkan::Get().IsInitialized())
            (void)Vulkan::Get().AcquireLevelAssets(*ParentLevel);
    }

    std::shared_ptr<Level> LevelManager::LoadLevel(std::string_view name, bool spawnPlayer, bool spawnEntities) {
        const unsigned int uid = ++lastUID;
        std::shared_ptr<Level> newLevel = Level::LoadLevel(name, uid, spawnPlayer, spawnEntities);
        if (!newLevel) return nullptr;
        if (Vulkan::Get().IsInitialized() && !Vulkan::Get().AcquireLevelAssets(*newLevel))
            LOG_CATEGORY_ERROR("Assets", "Failed to acquire dependencies for level '{}'", name);
        LoadedLevels.push_back(newLevel);
        LevelsByUID[uid] = newLevel;
        LevelsByName.emplace(std::string(name), newLevel);
        return newLevel;
    }

    void LevelManager::UnloadLevel(unsigned int uid) {
        auto it = LevelsByUID.find(uid);
        if (it != LevelsByUID.end()) UnloadLevel(it->second);
    }

    void LevelManager::UnloadLevel(std::string_view name) {
        auto level = GetLevelByName(name);
        if (level) UnloadLevel(level);
    }

    void LevelManager::UnloadLevel(const std::shared_ptr<Level>& levelToUnload) {
        if (!levelToUnload) return;
        if (Vulkan::Get().IsInitialized() && !Vulkan::Get().ReleaseLevelAssets(*levelToUnload)) return;
        LevelsByUID.erase(levelToUnload->GetId());
        auto range = LevelsByName.equal_range(levelToUnload->GetName());
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == levelToUnload) { LevelsByName.erase(it); break; }
        }
        LoadedLevels.erase(std::remove(LoadedLevels.begin(), LoadedLevels.end(), levelToUnload), LoadedLevels.end());
    }

    void LevelManager::UnloadAllLevels() {
        while (!LoadedLevels.empty()) {
            const auto level = LoadedLevels.back();
            UnloadLevel(level);
            if (!LoadedLevels.empty() && LoadedLevels.back() == level) {
                LOG_CATEGORY_ERROR("Assets", "UnloadAllLevels stopped because GPU resources for level '{}' could not be released safely", level ? level->GetName() : "");
                return;
            }
        }
        LevelsByUID.clear();
        LevelsByName.clear();
    }
}
