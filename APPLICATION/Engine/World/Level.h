//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Manage levels, objects, and assets  //
//                                              //
//==============================================//

#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include "../Main/Utils.h"

namespace Engine {
    class Camera;
    class Vulkan;

    class Level {
    public:
        enum class SunType : std::uint8_t { RealTime, Static, Baked };
        class LevelObject {
        public:
            virtual ~LevelObject() = default;
            virtual std::string ClassName() const { return "LevelObject"; }
            virtual std::string SubClassName() const { return ""; }
            virtual bool HasPosition() const { return false; }
            virtual bool HasRotation() const { return false; }
            virtual bool HasSize() const { return false; }
            const Vector3& GetPosition() const noexcept { return Position; }
            const Vector3& GetRotation() const noexcept { return Rotation; }
            const Vector3& GetSize() const noexcept { return Size; }
            virtual void SetPosition(const Vector3& newPosition) { Position = newPosition; }
            virtual void SetRotation(const Vector3& newRotation) { Rotation = newRotation; }
            virtual void SetSize(const Vector3& newSize) { Size = newSize; }
            virtual void Update(double deltaTime) { (void)deltaTime; }
        protected:
            virtual void OnSpawned() {}
            virtual void OnRemoved() {}
            Vector3 Position = Vector3::Zero();
            Vector3 Rotation = Vector3::Zero();
            Vector3 Size = Vector3::One();
            friend class Vulkan;
            friend class Level;
            Level* ParentLevel = nullptr;
        };

        class Mesh : public LevelObject {
        public:
            enum MeshType { Custom, Cube, Plane, Sphere, Cylinder, Wedge, CornerWedge, Cone, Pyramid };
            enum class Mobility : std::uint8_t { Movable, Stationary, Static };
            std::string ClassName() const override { return "Mesh"; }
            bool HasPosition() const override { return true; }
            bool HasRotation() const override { return true; }
            bool HasSize() const override { return true; }
            const MeshType& GetMeshType() const noexcept { return BaseMeshType; }
            Mobility GetMobility() const noexcept { return MeshMobility; }
            void SetMobility(Mobility mobility) noexcept { MeshMobility = mobility; }
            const std::string& GetCustomMesh() const noexcept { return CustomMesh; }
            const std::string& GetMaterialAsset(std::uint32_t index = 0) const noexcept { static const std::string empty; return index < MaterialAssets.size() ? MaterialAssets[index] : empty; }
            std::size_t GetMaterialAssetCount() const noexcept { return MaterialAssets.size(); }
            std::uint32_t GetMeshAssetId() const noexcept { return MeshAssetId; }
            std::uint32_t GetMaterialId(std::uint32_t index = 0) const noexcept { return index < MaterialIds.size() ? MaterialIds[index] : 0; }
            const std::vector<std::uint32_t>& GetMaterialIds() const noexcept { return MaterialIds; }
            virtual void SetMeshType(const MeshType& newMeshType);
            void SetMeshAsset(std::string mesh);
            void SetMaterialAsset(std::string material, std::uint32_t index = 0);
            void SetMeshAssetId(std::uint32_t id) noexcept {
                MeshAssetId = id;
                if (ParentLevel && id != 0) ParentLevel->EverUsedMeshAssetIds.emplace(id);
            }
            void SetMaterialAssetId(std::uint32_t id, std::uint32_t index = 0) {
                if (index >= MaterialIds.size()) MaterialIds.resize(static_cast<std::size_t>(index) + 1, 0);
                MaterialIds[index] = id;
                if (ParentLevel && id != 0) ParentLevel->EverUsedMaterialIds.emplace(id);
            }
        protected:
            MeshType BaseMeshType = MeshType::Cube;
            Mobility MeshMobility = Mobility::Static;
            std::string CustomMesh;
            std::vector<std::string> MaterialAssets;
            std::uint32_t MeshAssetId = 0;
            std::vector<std::uint32_t> MaterialIds;
            friend class Vulkan;
            friend class Level;
        };

        class Sky : public LevelObject {
        public:
            enum Mode : std::uint32_t { SkyNone, SkyBox, SkyDome, SkyFlat };
            std::string ClassName() const override { return "Sky"; }
            bool HasRotation() const override { return true; }
            Mode GetMode() const noexcept { return CurrentMode; }
            void SetMode(Mode mode) noexcept { CurrentMode = mode; }
            const std::string& GetMaterialAsset() const noexcept { return MaterialAsset; }
            std::uint32_t GetMaterialId() const noexcept { return MaterialId; }
            void SetMaterialAsset(std::string material);
            void SetMaterialId(std::uint32_t id) noexcept { MaterialId = id; if (ParentLevel && id != 0) ParentLevel->EverUsedMaterialIds.emplace(id); }
            void SetSunSize(float sunSize) noexcept { SunSize = sunSize; }
            float GetSunSize() const noexcept { return SunSize; }
            void SetSunColorMultiplier(const Vector3& multiplier) noexcept { SunColorMultiplier = multiplier; }
            const Vector3& GetSunColorMultiplier() const noexcept { return SunColorMultiplier; }
            void SetSunTextureAsset(std::string texture);
            const std::string& GetSunTextureAsset() const noexcept { return SunTextureAsset; }
        private:
            Vector3 SunColorMultiplier = Vector3::One();
            std::string SunTextureAsset;
            std::string AppliedSunTextureAsset;
            std::uint32_t AppliedSunTextureMaterial = 0;
            std::uint32_t RuntimeMaterialId = 0;
            std::uint32_t RuntimeMaterialSourceId = 0;
            bool HasAppliedSunTextureState = false;
            bool SunTextureOverrideApplied = false;
            friend class Level;
            friend class Vulkan;
            Mode CurrentMode = SkyNone;
            std::string MaterialAsset;
            std::uint32_t MaterialId = 0;
            float SunSize = 0.05f;
        };

        class Light : public LevelObject {
        public:
            enum class Type : std::uint8_t { RealTime, Static, Baked };
            std::string ClassName() const override { return "Light"; }
            bool HasPosition() const override { return true; }
            bool HasRotation() const override { return true; }
            Type GetType() const noexcept { return CurrentType; }
            void SetType(Type type) noexcept { CurrentType = type; }
            Vector3 GetColor() const noexcept { return Color; }
            float GetIntensity() const noexcept { return Intensity; }
            float GetEffectiveRadius() const noexcept { return Radius; }
            bool IsEnabled() const noexcept { return Enabled; }
            void SetColor(Vector3 color) noexcept { Color = color; }
            void SetIntensity(float intensity) noexcept { Intensity = intensity; }
            void SetEffectiveRadius(float radius) noexcept { Radius = radius; }
            void SetEnabled(bool enabled) noexcept { Enabled = enabled; }
        protected:
            friend class Level;
            friend class Vulkan;
            Vector3 Color = Vector3::One();
            float Intensity = 1.0f;
            float Radius = 12.0f;
            bool Enabled = true;
            Type CurrentType = Type::Baked;
        };

        class SpawnLocation : public LevelObject {
        public:
            std::string ClassName() const override { return "SpawnLocation"; }
            bool HasPosition() const override { return true; }
            bool HasSize() const override { return true; }
            const std::string& GetTeam() const noexcept { return Team; }
            virtual void SetTeam(const std::string& newTeam) { Team = newTeam; }
            Vector3 GetMins() const noexcept { return Position - (Size * 0.5); }
            Vector3 GetMaxs() const noexcept { return Position + (Size * 0.5); }
        private:
            std::string Team = "";
        };

        class Player : public LevelObject {
        public:
            std::string ClassName() const override { return "Player"; }
            bool HasPosition() const override { return true; }
            bool HasRotation() const override { return true; }
            bool HasSize() const override { return true; }
            virtual void SetVelocity(Vector3 velocity) noexcept { Velocity = velocity; }
            virtual Vector3 GetVelocity() const noexcept { return Velocity; }
            virtual void ApplyImpulse(Vector3 velocity, Vector3 atPosition) { (void)velocity; (void)atPosition; }
            virtual void Respawn(bool forcePosition = false, const Vector3& position = Vector3::Zero()) noexcept { if (forcePosition) Position = position; IsAlive = true; }
            virtual double GetYaw() const noexcept { return Z; }
            virtual double GetPitch() const noexcept { return Y; }
            virtual double GetIsAlive() const noexcept { return IsAlive; }
            virtual std::shared_ptr<Camera> GetCamera() const noexcept { return CurrentCamera; }
            virtual void AssignCamera(std::shared_ptr<Camera> camera) noexcept { CurrentCamera = std::move(camera); }
        protected:
            Vector3 Velocity = Vector3::Zero();
            double Z = 0.0;
            double Y = 0.0;
            bool IsAlive = false;
            std::shared_ptr<Camera> CurrentCamera;
        };

        template<typename T>
        std::vector<T*> GetObjectsOfType() const {
            std::vector<T*> result;
            for (const auto& obj : Objects) if (auto* derived = dynamic_cast<T*>(obj.get())) result.push_back(derived);
            return result;
        }

        void Update(double deltaTime) noexcept;
        const unsigned int& GetId() const noexcept { return LevelUID; }
        const std::string& GetName() const noexcept { return LevelName; }
        Vector3 PickSpawnPosition() const noexcept;
        const std::vector<std::unique_ptr<LevelObject>>& GetObjects() const noexcept { return Objects; }
        Sky* GetCurrentSky() noexcept { return CurrentSky.get(); }
        const Sky* GetCurrentSky() const noexcept { return CurrentSky.get(); }
        Player* GetCurrentPlayer() noexcept { return CurrentPlayer.get(); }
        const Player* GetCurrentPlayer() const noexcept { return CurrentPlayer.get(); }
        bool GetIsPaused() const noexcept { return isPaused; }
        void SetIsPaused(bool newIsPaused) noexcept { isPaused = newIsPaused; }
        void AddObject(std::unique_ptr<LevelObject> object);
        void SetCurrentSky(std::unique_ptr<Sky> sky);
        void SetCurrentPlayer(std::unique_ptr<Player> player, bool respawn = false);
        const Vector3& GetSunDirection() const noexcept { return SunDirection; }
        void SetSunDirection(const Vector3& direction) noexcept { SunDirection = direction; }
        void SetSunAngles(float azimuthDegrees, float elevationDegrees) noexcept;
        SunType GetSunType() const noexcept { return CurrentSunType; }
        void SetSunType(SunType sunType) noexcept { CurrentSunType = sunType; }
        const std::vector<std::unique_ptr<Light>>& GetLevelLights() const noexcept { return Lights; }
        void Destroy(std::unique_ptr<LevelObject> object) noexcept { (void)object; }
        void DeclareMeshDependency(std::string path);
        void DeclareMaterialDependency(std::string path);
        void DeclareTextureDependency(std::string path);
        using PlayerFactoryOverride = std::function<std::unique_ptr<Level::Player>()>;
        void SetPlayerFactoryOverride(PlayerFactoryOverride factoryOverride) { CustomPlayerFactoryOverride = std::move(factoryOverride); }
        void ClearPlayerFactoryOverride() { CustomPlayerFactoryOverride = nullptr; }
        float GetFogStart() const noexcept { return fogStart; }
        float GetFogEnd() const noexcept { return fogEnd; }
        Vector3 GetFogColor() const noexcept { return fogColor; }
        float GetFogColorOverride() const noexcept { return fogColorOverride; }
        void SetFogStart(float fogstart) noexcept { fogStart = fogstart; }
        void SetFogEnd(float fogend) noexcept { fogEnd = fogend; }
        void SetFogColor(Vector3 fogcolor) noexcept { fogColor = fogcolor; }
        void SetFogColorOverride(float fogcolorOverride) noexcept { fogColorOverride = fogcolorOverride; }
    private:
        explicit Level(std::string name, unsigned int uid);
        static std::shared_ptr<Level> LoadLevel(std::string_view name, unsigned int uid, bool spawnPlayer = true, bool spawnEntities = true);
        friend class LevelManager;
        friend class Vulkan;
        std::string LevelName;
        unsigned int LevelUID = 0;
        std::vector<std::unique_ptr<LevelObject>> Objects;
        std::vector<std::unique_ptr<Light>> Lights;
        std::unique_ptr<Sky> CurrentSky;
        std::unique_ptr<Player> CurrentPlayer;
        PlayerFactoryOverride CustomPlayerFactoryOverride;
        Vector3 SunDirection = Vector3(0.35, 0.15, 0.92);
        SunType CurrentSunType = SunType::Baked;
        bool isPaused = false;
        float fogStart = 35.0f;
        float fogEnd = 100.0f;
        Vector3 fogColor = Vector3::Zero();
        float fogColorOverride = 0.0f;
        std::unordered_set<std::uint32_t> EverUsedMeshAssetIds;
        std::unordered_set<std::uint32_t> EverUsedMaterialIds;
        std::unordered_set<std::string> DeclaredMeshAssetPaths;
        std::unordered_set<std::string> DeclaredMaterialAssetPaths;
        std::unordered_set<std::string> DeclaredTextureAssetPaths;
        std::unordered_set<std::string> AcquiredMeshAssetPaths;
        std::unordered_set<std::string> AcquiredMaterialAssetPaths;
        std::unordered_map<std::string, std::uint32_t> AcquiredTextureHandles;
        bool AssetsAcquired = false;
        bool AssetMutationDeferred = false;
    };

    class LevelManager {
    public:
        static LevelManager& GetInstance() noexcept { static LevelManager instance; return instance; }
        std::shared_ptr<Level> LoadLevel(std::string_view name, bool spawnPlayer = true, bool spawnEntities = true);
        std::shared_ptr<Level> GetLevelByUID(unsigned int uid) const noexcept { auto it = LevelsByUID.find(uid); return it != LevelsByUID.end() ? it->second : nullptr; }
        std::shared_ptr<Level> GetLevelByName(std::string_view name) const noexcept { auto it = LevelsByName.find(std::string(name)); return it != LevelsByName.end() ? it->second : nullptr; }
        std::vector<std::shared_ptr<Level>> GetAllLevelsByName(std::string_view name) const {
            std::vector<std::shared_ptr<Level>> result;
            auto range = LevelsByName.equal_range(std::string(name));
            for (auto it = range.first; it != range.second; ++it)
                result.push_back(it->second);
            return result;
        }
        void UnloadLevel(unsigned int uid);
        void UnloadLevel(std::string_view name);
        void UnloadAllLevelsWithName(std::string_view name) { auto levels = GetAllLevelsByName(name); for (const auto& level : levels) UnloadLevel(level); }
        void UnloadLevel(const std::shared_ptr<Level>& levelToUnload);
        void UnloadAllLevels();
        void UpdateAllLevels(double deltaTime) noexcept { for (auto& level : LoadedLevels) level->Update(deltaTime); }
        const std::vector<std::shared_ptr<Level>>& GetLoadedLevels() const noexcept { return LoadedLevels; }
        using PlayerFactory = std::function<std::unique_ptr<Level::Player>()>;
        void SetPlayerFactory(PlayerFactory factory) { CustomPlayerFactory = std::move(factory); }
    private:
        LevelManager() = default;
        ~LevelManager() = default;
        LevelManager(const LevelManager&) = delete;
        LevelManager& operator=(const LevelManager&) = delete;
        PlayerFactory CustomPlayerFactory;
        std::vector<std::shared_ptr<Level>> LoadedLevels;
        std::unordered_map<unsigned int, std::shared_ptr<Level>> LevelsByUID;
        std::unordered_multimap<std::string, std::shared_ptr<Level>> LevelsByName;
        unsigned int lastUID = 0;
        friend class Level;
    };
}
