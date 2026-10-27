#version 450
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 fogParams;
    vec4 fogColor;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inInstancePosition;
layout(location = 3) in vec3 inInstanceSize;
layout(location = 4) in vec3 inInstanceRotation;
layout(location = 5) in vec4 inInstanceLightmapScaleOffset;
layout(location = 6) in vec2 inLightmapUV;
layout(location = 7) in float inInstanceLightmapPage;
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPosition;
layout(location = 2) out vec2 outLightmapUV;
layout(location = 3) flat out float outLightmapPage;

vec3 RotateEuler(vec3 p, vec3 degrees) {
    vec3 r = radians(degrees);
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    p = vec3(p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx);
    p = vec3(p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy);
    p = vec3(p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z);
    return p;
}

void main() {
    vec3 worldPosition = RotateEuler(inPosition * inInstanceSize, inInstanceRotation) + inInstancePosition;
    vec4 clipPosition = pc.viewProjection * vec4(worldPosition, 1.0);
    gl_Position = vec4(clipPosition.x, -clipPosition.y, clipPosition.z, clipPosition.w);
    outUV = inUV;
    outWorldPosition = worldPosition;
    outLightmapUV = inLightmapUV * inInstanceLightmapScaleOffset.xy + inInstanceLightmapScaleOffset.zw;
    outLightmapPage = inInstanceLightmapPage;
}
