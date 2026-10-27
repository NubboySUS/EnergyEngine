//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Class to look at stuff              //
//                                              //
//==============================================//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "../Main/Utils.h"

namespace Engine
{
    enum FovMode
    {
        Horizontal,
        Vertical,
        Diagonal,
    };
    enum ProjectionMode : std::uint8_t
    {
        Perspective,
        Orthographic,
    };
    enum DepthMode : std::uint8_t
    {
        ZeroToOne,
        ReverseZeroToOne,
    };
    enum Visibility : std::uint8_t
    {
        Outside,
        Partial,
        Total,
    };
    struct CameraRay
    {
        Vector3 origin{};
        Vector3 direction{};
    };
    struct ScreenPoint
    {
        Vector2 pixel{};
        double depth{ 0.0 };
    };
    struct CameraPlane
    {
        Vector3 normal{};
        double distance{ 0.0 };
        double SignedDistance(const Vector3 point) const noexcept;
    };
    struct CameraFrustum
    {
        std::array<CameraPlane, 6> planes{};
    };
    class Camera
    {
    public:
        Camera() noexcept {
            ViewMatrix = Matrix4::Identity();
            ProjectionMatrix = Matrix4::Identity();
            ViewProjectionMatrix = Matrix4::Identity();
        }
        ~Camera() = default;
        Camera(const Camera&) = default;
        Camera& operator=(const Camera&) = default;
        Camera(Camera&&) noexcept = default;
        Camera& operator=(Camera&&) noexcept = default;
        void Move(Vector3 delta) noexcept;
        void MoveForward(double units) noexcept;
        void MoveRight(double units) noexcept;
        void MoveUp(double units) noexcept;
        void AddPitch(double degrees) noexcept { SetRotationY(Y + degrees); }
        void AddYaw(double degrees) noexcept { SetRotationZ(Z + degrees); }
        void AddRoll(double degrees) noexcept { SetRotationX(X + degrees); }
        void AddRotationY(double degrees) noexcept { SetRotationY(Y + degrees); }
        void AddRotationZ(double degrees) noexcept { SetRotationZ(Z + degrees); }
        void AddRotationX(double degrees) noexcept { SetRotationX(X + degrees); }
        void SetPitch(double degrees) noexcept { SetRotationY(degrees); }
        void SetYaw(double degrees) noexcept { SetRotationZ(degrees); }
        void SetRoll(double degrees) noexcept { SetRotationX(degrees); }
        void SetRotationY(double degrees) noexcept;
        void SetRotationZ(double degrees) noexcept;
        void SetRotationX(double degrees) noexcept;
        double GetPitch() const noexcept { return Y; }
        double GetYaw() const noexcept { return Z; }
        double GetRoll() const noexcept { return X; }
        double GetRotationY() const noexcept { return Y; }
        double GetRotationZ() const noexcept { return Z; }
        double GetRotationX() const noexcept { return X; }
        Vector3 GetRotation() const noexcept { return { Y, Z, X }; }
        void SetViewPoint(Vector3 point) noexcept;
        void SetViewTarget(Vector3 target) noexcept;
        Vector3 GetViewPoint() const noexcept { return Position; }
        Vector3 GetViewForward() const noexcept;
        Vector3 GetViewRight() const noexcept;
        Vector3 GetViewUp() const noexcept;
        void SetViewportSize(Vector2 size) noexcept;
        Vector2 GetViewportSize() const noexcept { return ViewportSize; }
        void SetPerspective(double FovDegrees, double nearClip, double farClip) noexcept;
        void SetOrthographic(double zoom, double nearClip, double farClip) noexcept;
        void SetProjectionMode(ProjectionMode mode) noexcept {
            if (ProjectionMode == mode)
                return;
            ProjectionMode = mode;
            ProjectionNeedsRebuilding = true;
            CombinedNeedsRebuilding = true;
            FrustumNeedsRebuilding = true;
        }
        ProjectionMode GetProjectionMode() const noexcept { return ProjectionMode; }
        bool IsOrthographic() const noexcept { return ProjectionMode == ProjectionMode::Orthographic; }
        void SetDepthMode(DepthMode mode) noexcept {
            if (DepthMode == mode)
                return;
            DepthMode = mode;
            ProjectionNeedsRebuilding = true;
            CombinedNeedsRebuilding = true;
            FrustumNeedsRebuilding = true;
        }
        DepthMode GetDepthMode() const noexcept { return DepthMode; }
        void SetNearClip(double nearClip) noexcept;
        void SetFarClip(double farClip) noexcept;
        double GetNearClip() const noexcept { return NearClipPlane; }
        double GetFarClip() const noexcept { return FarClipPlane; }
        void SetFOV(double FovDegrees) noexcept;
        double GetFOV() const noexcept { return FOV; }
        void SetZoom(double zoom) noexcept;
        void AddZoom(double delta) noexcept { SetZoom(Zoom + delta); }
        double GetZoom() const noexcept { return Zoom; }
        const Matrix4& GetViewMatrix() const;
        const Matrix4& GetProjMatrix() const;
        const Matrix4& GetViewProjMatrix() const;
        CameraFrustum GetFrustum() const;
        Visibility ClassifyAABB(Vector3 mins, Vector3 maxs) const;
        ScreenPoint WorldToScreen(Vector3 world) const;
        Vector3 WorldToView(Vector3 world) const;
        Vector3 WorldToNDC(Vector3 world) const;
        Vector3 ViewToWorld(Vector2 pixel, double depth) const;
        CameraRay BuildRay(Vector2 pixel) const;
    private:
        Vector3 Position = Vector3::Zero();
        double Y = 0.0;
        double Z = 0.0;
        double X = 0.0;
        mutable Quaternion Orientation = Quaternion::Identity();
        ProjectionMode ProjectionMode = ProjectionMode::Perspective;
        DepthMode DepthMode = DepthMode::ReverseZeroToOne;
        double FOV = 90.0;
        FovMode FovMode = FovMode::Horizontal;
        double NearClipPlane = 0.1;
        double FarClipPlane = 5000.0;
        double Zoom = 1.0;
        Vector2 ViewportSize = Vector2(1280, 720);
        mutable Matrix4 ViewMatrix = Matrix4::Identity();
        mutable Matrix4 ProjectionMatrix = Matrix4::Identity();
        mutable Matrix4 ViewProjectionMatrix = Matrix4::Identity();
        mutable CameraFrustum Frustum = CameraFrustum();
        mutable bool OrientationNeedsRebuilding = false;
        mutable bool ViewNeedsRebuilding = true;
        mutable bool ProjectionNeedsRebuilding = true;
        mutable bool CombinedNeedsRebuilding = true;
        mutable bool FrustumNeedsRebuilding = true;
        void RebuildView() const;
        void RebuildProjection() const;
        void RebuildCombined() const;
        void RebuildFrustum() const;
    };
}
