//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Make engine know how to look        //
//                                              //
//==============================================//

#include "Camera.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace Engine
{
    double CameraPlane::SignedDistance(const Vector3 point) const noexcept { return normal.x * point.x + normal.y * point.y + normal.z * point.z - distance; }
    CameraPlane MakePlane(const Vector3 normal, const Vector3 point) noexcept {
        Vector3 normalized = normal.Unit();
        return { normalized, normalized.Dot(point) };
    }
    void Camera::Move(Vector3 delta) noexcept {
        Position = Position + delta.FiniteOr();
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::MoveForward(double units) noexcept {
        Position = Position + GetViewForward() * FiniteOr(units);
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::MoveRight(double units) noexcept {
        Position = Position + GetViewRight() * FiniteOr(units);
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::MoveUp(double units) noexcept {
        Position = Position + GetViewUp() * FiniteOr(units);
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetRotationY(double degrees) noexcept {
        const double clamped = std::clamp(FiniteOr(degrees), -90.0, 90.0);
        if (clamped == Y)
            return;
        Y = clamped;
        OrientationNeedsRebuilding = true;
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetRotationZ(double degrees) noexcept {
        const double wrapped = WrapDegrees(FiniteOr(degrees));
        if (wrapped == Z)
            return;
        Z = wrapped;
        OrientationNeedsRebuilding = true;
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetRotationX(double degrees) noexcept {
        const double wrapped = WrapDegrees(FiniteOr(degrees));
        if (wrapped == X)
            return;
        X = wrapped;
        OrientationNeedsRebuilding = true;
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetViewPoint(Vector3 point) noexcept {
        if (Position.NearlyEqual(point.FiniteOr(), EPSILON))
            return;
        Position = point.FiniteOr();
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetViewTarget(Vector3 target) noexcept {
        Vector3 dir = target.FiniteOr() - Position;
        const double lenSquare = dir.Dot(dir);
        if (lenSquare <= EPSILON)
            return;
        dir = dir / std::sqrt(lenSquare);
        const double yaw = std::atan2(dir.x, dir.y) * (180.0 / M_PI);
        const double pitch = -std::asin(std::clamp(dir.z, -1.0, 1.0)) * (180.0 / M_PI);
        Z = WrapDegrees(yaw);
        Y = std::clamp(pitch, -90.0, 90.0);
        OrientationNeedsRebuilding = true;
        ViewNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    Vector3 Camera::GetViewForward() const noexcept {
        if (ViewNeedsRebuilding)
            RebuildView();
        return Orientation.Rotate(Vector3::YAxis());
    }
    Vector3 Camera::GetViewRight() const noexcept {
        if (ViewNeedsRebuilding)
            RebuildView();
        return Orientation.Rotate(Vector3::XAxis());
    }
    Vector3 Camera::GetViewUp() const noexcept {
        if (ViewNeedsRebuilding)
            RebuildView();
        return Orientation.Rotate(Vector3::ZAxis());
    }
    void Camera::SetViewportSize(Vector2 size) noexcept {
        size = size.Max(Vector2(1, 1));
        if (ViewportSize.x == static_cast<double>(size.x) && ViewportSize.y == static_cast<double>(size.y))
            return;
        ViewportSize = Vector2(static_cast<double>(size.x), static_cast<double>(size.y));
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetPerspective(double FovDegrees, double nearClip, double farClip) noexcept {
        ProjectionMode = ProjectionMode::Perspective;
        FOV = std::clamp(FiniteOr(FovDegrees, 90.0), 0.1, 180.0);
        NearClipPlane = std::max(FiniteOr(nearClip, 0.1), 0.001);
        FarClipPlane = std::max(FiniteOr(farClip, 5000.0), NearClipPlane + 0.001);
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetOrthographic(double zoom, double nearClip, double farClip) noexcept {
        ProjectionMode = ProjectionMode::Orthographic;
        Zoom = std::max(FiniteOr(zoom, 1.0), 0.001);
        NearClipPlane = std::max(FiniteOr(nearClip, 0.1), 0.001);
        FarClipPlane = std::max(FiniteOr(farClip, 5000.0), NearClipPlane + 0.001);
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetNearClip(double nearClip) noexcept {
        nearClip = std::clamp(FiniteOr(nearClip, 0.1), 0.001, FarClipPlane - 0.001);
        if (nearClip == NearClipPlane)
            return;
        NearClipPlane = nearClip;
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetFarClip(double farClip) noexcept {
        farClip = std::max(FiniteOr(farClip, 5000.0), NearClipPlane + 0.001);
        if (farClip == FarClipPlane)
            return;
        FarClipPlane = farClip;
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetFOV(double FovDegrees) noexcept {
        FovDegrees = std::clamp(FiniteOr(FovDegrees, 90.0), 0.1, 180.0);
        if (FovDegrees == FOV)
            return;
        FOV = FovDegrees;
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    void Camera::SetZoom(double zoom) noexcept {
        zoom = std::max(FiniteOr(zoom, 1.0), 0.001);
        if (zoom == Zoom)
            return;
        Zoom = zoom;
        ProjectionNeedsRebuilding = true;
        CombinedNeedsRebuilding = true;
        FrustumNeedsRebuilding = true;
    }
    const Matrix4& Camera::GetViewMatrix() const {
        if (ViewNeedsRebuilding)
            RebuildView();
        return ViewMatrix;
    }
    const Matrix4& Camera::GetProjMatrix() const {
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        return ProjectionMatrix;
    }
    const Matrix4& Camera::GetViewProjMatrix() const {
        if (ViewNeedsRebuilding)
            RebuildView();
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        if (CombinedNeedsRebuilding)
            RebuildCombined();
        return ViewProjectionMatrix;
    }
    CameraFrustum Camera::GetFrustum() const {
        if (ViewNeedsRebuilding)
            RebuildView();
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        if (FrustumNeedsRebuilding)
            RebuildFrustum();
        return Frustum;
    }
    Visibility Camera::ClassifyAABB(Vector3 mins, Vector3 maxs) const {
        if (ViewNeedsRebuilding)
            RebuildView();
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        if (FrustumNeedsRebuilding)
            RebuildFrustum();
        mins = mins.FiniteOr();
        maxs = maxs.FiniteOr();
        bool isPartial = false;
        for (const CameraPlane& plane : Frustum.planes) {
            if (plane.SignedDistance(Vector3(plane.normal.x >= 0.0 ? maxs.x : mins.x, plane.normal.y >= 0.0 ? maxs.y : mins.y, plane.normal.z >= 0.0 ? maxs.z : mins.z)) < 0.0)
                return Visibility::Outside;
            if (plane.SignedDistance(Vector3(plane.normal.x >= 0.0 ? mins.x : maxs.x, plane.normal.y >= 0.0 ? mins.y : maxs.y, plane.normal.z >= 0.0 ? mins.z : maxs.z)) < 0.0)
                isPartial = true;
        }
        return isPartial ? Visibility::Partial : Visibility::Total;
    }
    ScreenPoint Camera::WorldToScreen(Vector3 world) const {
        const Vector3 ndc = WorldToNDC(world);
        ScreenPoint result;
        result.pixel.x = 0.5 * (ndc.x + 1.0) * ViewportSize.x;
        result.pixel.y = 0.5 * (1.0 - ndc.y) * ViewportSize.y;
        result.depth = ndc.z;
        return result;
    }
    Vector3 Camera::WorldToView(Vector3 world) const {
        if (ViewNeedsRebuilding)
            RebuildView();
        return ViewMatrix.TransformPoint(world);
    }
    Vector3 Camera::WorldToNDC(Vector3 world) const {
        if (ViewNeedsRebuilding)
            RebuildView();
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        if (CombinedNeedsRebuilding)
            RebuildCombined();
        return ViewProjectionMatrix.TransformPoint(world);
    }
    Vector3 Camera::ViewToWorld(Vector2 pixel, double depth) const {
        const double ndcX = 2.0 * pixel.x / ViewportSize.x - 1.0;
        const double ndcY = 1.0 - 2.0 * pixel.y / ViewportSize.y;
        const double ndcZ = std::clamp(FiniteOr(depth, 0.0), 0.0, 1.0);
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        const Vector3 right = GetViewRight();
        const Vector3 up = GetViewUp();
        const Vector3 forward = GetViewForward();
        if (IsOrthographic())
            return Position + right * (ndcX / ProjectionMatrix[0][0]) + up * (ndcY / ProjectionMatrix[1][1]) - forward * ((ndcZ - ProjectionMatrix[2][3]) / ProjectionMatrix[2][2]);
        const double d = ndcZ + ProjectionMatrix[2][2];
        if (std::abs(d) <= EPSILON) {
            const CameraRay ray = BuildRay({ (ndcX + 1.0) * 0.5 * ViewportSize.x, (1.0 - ndcY) * 0.5 * ViewportSize.y });
            return ray.origin + ray.direction * std::numeric_limits<double>::max();
        }
        const double dist = ProjectionMatrix[2][3] / d;
        return Position + right * (ndcX * dist / ProjectionMatrix[0][0]) + up * (ndcY * dist / ProjectionMatrix[1][1]) + forward * dist;
    }
    CameraRay Camera::BuildRay(Vector2 pixel) const {
        const double ndcX = 2.0 * pixel.x / ViewportSize.x - 1.0;
        const double ndcY = 1.0 - 2.0 * pixel.y / ViewportSize.y;
        const Vector3 right = GetViewRight();
        const Vector3 up = GetViewUp();
        const Vector3 forward = GetViewForward();
        CameraRay ray;
        if (ProjectionNeedsRebuilding)
            RebuildProjection();
        if (IsOrthographic()) {
            ray.origin = Position + right * (ndcX / ProjectionMatrix[0][0]) + up * (ndcY / ProjectionMatrix[1][1]) + forward * NearClipPlane;
            ray.direction = forward;
            return ray;
        }
        ray.origin = Position;
        ray.direction = (forward * NearClipPlane + right * (ndcX * NearClipPlane / ProjectionMatrix[0][0]) + up * (ndcY * NearClipPlane / ProjectionMatrix[1][1])).Unit();
        return ray;
    }
    void Camera::RebuildView() const {
        if (OrientationNeedsRebuilding) {
            const Quaternion yaw = Quaternion::FromAxisAngle(Vector3::ZAxis(), -DegreesToRadians(Z));
            const Quaternion pitch = Quaternion::FromAxisAngle(yaw.Rotate(Vector3::XAxis()), -DegreesToRadians(Y));
            const Quaternion yawPitch = (pitch * yaw).Normalized();
            Orientation = (Quaternion::FromAxisAngle(yawPitch.Rotate(Vector3::YAxis()), -DegreesToRadians(X)) * yawPitch).Normalized();
            OrientationNeedsRebuilding = false;
        }
        Vector3 right = Orientation.Rotate(Vector3::XAxis());
        Vector3 up = Orientation.Rotate(Vector3::ZAxis());
        Vector3 forward = Orientation.Rotate(Vector3::YAxis());
        Vector3 backward = forward * -1.0;
        ViewMatrix = Matrix4::Identity();
        ViewMatrix[0][0] = right.x;
        ViewMatrix[0][1] = right.y;
        ViewMatrix[0][2] = right.z;
        ViewMatrix[0][3] = -(right.Dot(Position));
        ViewMatrix[1][0] = up.x;
        ViewMatrix[1][1] = up.y;
        ViewMatrix[1][2] = up.z;
        ViewMatrix[1][3] = -(up.Dot(Position));
        ViewMatrix[2][0] = backward.x;
        ViewMatrix[2][1] = backward.y;
        ViewMatrix[2][2] = backward.z;
        ViewMatrix[2][3] = -(backward.Dot(Position));
        ViewNeedsRebuilding = false;
    }
    void Camera::RebuildProjection() const {
        ProjectionMatrix = Matrix4{};
        if (IsOrthographic()) {
            ProjectionMatrix[0][0] = 2.0 * Zoom / ViewportSize.x;
            ProjectionMatrix[1][1] = 2.0 * Zoom / ViewportSize.y;
            if (DepthMode == DepthMode::ReverseZeroToOne) {
                ProjectionMatrix[2][2] = 1.0 / (FarClipPlane - NearClipPlane);
                ProjectionMatrix[2][3] = FarClipPlane / (FarClipPlane - NearClipPlane);
            }
            else {
                ProjectionMatrix[2][2] = 1.0 / (NearClipPlane - FarClipPlane);
                ProjectionMatrix[2][3] = NearClipPlane / (NearClipPlane - FarClipPlane);
            }
            ProjectionMatrix[3][3] = 1.0;
        }
        else {
            const double aspect = ViewportSize.x / ViewportSize.y;
            double fovHorizRad = 0.0;
            double fovVertRad = 0.0;
            switch (FovMode)
            {
            case FovMode::Horizontal:
                fovHorizRad = DegreesToRadians(FOV);
                fovVertRad = 2.0 * std::atan(std::tan(fovHorizRad * 0.5) / aspect);
                break;
            case FovMode::Vertical:
                fovVertRad = DegreesToRadians(FOV);
                fovHorizRad = 2.0 * std::atan(std::tan(fovVertRad * 0.5) * aspect);
                break;
            case FovMode::Diagonal: {
                const double scale = std::tan(DegreesToRadians(FOV) * 0.5) / std::sqrt(aspect * aspect + 1.0);
                fovHorizRad = 2.0 * std::atan(scale * aspect);
                fovVertRad = 2.0 * std::atan(scale);
                break;
            }
            }
            const double xScale = 1.0 / std::tan(fovHorizRad * 0.5);
            const double yScale = 1.0 / std::tan(fovVertRad * 0.5);
            ProjectionMatrix[0][0] = xScale;
            ProjectionMatrix[1][1] = yScale;
            if (DepthMode == DepthMode::ReverseZeroToOne) {
                ProjectionMatrix[2][2] = NearClipPlane / (FarClipPlane - NearClipPlane);
                ProjectionMatrix[2][3] = (NearClipPlane * FarClipPlane) / (FarClipPlane - NearClipPlane);
            }
            else {
                ProjectionMatrix[2][2] = FarClipPlane / (NearClipPlane - FarClipPlane);
                ProjectionMatrix[2][3] = (NearClipPlane * FarClipPlane) / (NearClipPlane - FarClipPlane);
            }
            ProjectionMatrix[3][2] = -1.0;
        }
        ProjectionNeedsRebuilding = false;
    }
    void Camera::RebuildCombined() const {
        ViewProjectionMatrix = ProjectionMatrix * ViewMatrix;
        CombinedNeedsRebuilding = false;
    }
    void Camera::RebuildFrustum() const {
        const Vector3 right = GetViewRight();
        const Vector3 up = GetViewUp();
        const Vector3 forward = GetViewForward();
        if (IsOrthographic()) {
            const Vector3 halfw = right * 0.5 * ViewportSize.x / Zoom;
            const Vector3 halfh = up * 0.5 * ViewportSize.y / Zoom;
            const Vector3 center = Position + forward * ((NearClipPlane + FarClipPlane) * 0.5);
            Frustum.planes[0] = MakePlane(right, center - halfw);
            Frustum.planes[1] = MakePlane(right * -1.0, center + halfw);
            Frustum.planes[2] = MakePlane(up, center - halfh);
            Frustum.planes[3] = MakePlane(up * -1.0, center + halfh);
        }
        else {
            const double aspect = ViewportSize.x / ViewportSize.y;
            double halfhor = 0.0;
            double halfver = 0.0;
            switch (FovMode)
            {
            case FovMode::Horizontal:
                halfhor = DegreesToRadians(FOV) * 0.5;
                halfver = std::atan(std::tan(halfhor) / aspect);
                break;
            case FovMode::Vertical:
                halfver = DegreesToRadians(FOV) * 0.5;
                halfhor = std::atan(std::tan(halfver) * aspect);
                break;
            case FovMode::Diagonal: {
                const double scale = std::tan(DegreesToRadians(FOV) * 0.5) / std::sqrt(aspect * aspect + 1.0);
                halfhor = std::atan(scale * aspect);
                halfver = std::atan(scale);
                break;
            }
            }
            const Vector3 sinhor = forward * std::sin(halfhor);
            const Vector3 coshor = right * std::cos(halfhor);
            const Vector3 sinver = forward * std::sin(halfver);
            const Vector3 cosver = up * std::cos(halfver);
            Frustum.planes[0] = MakePlane(Vector3(sinhor + coshor).Unit(), Position);
            Frustum.planes[1] = MakePlane(Vector3(sinhor - coshor).Unit(), Position);
            Frustum.planes[2] = MakePlane(Vector3(sinver + cosver).Unit(), Position);
            Frustum.planes[3] = MakePlane(Vector3(sinver - cosver).Unit(), Position);
        }
        Frustum.planes[4] = MakePlane(forward, Position + forward * NearClipPlane);
        Frustum.planes[5] = MakePlane(forward * -1.0, Position + forward * FarClipPlane);
        FrustumNeedsRebuilding = false;
    }
}