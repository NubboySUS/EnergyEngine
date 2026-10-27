#version 450

layout(location = 1) in vec3 inWorldPosition;
layout(location = 2) in vec2 inLightmapUV;
layout(location = 3) flat in float inLightmapPage;
layout(location = 0) out vec4 outLightmapInfo;
layout(location = 1) out vec4 outWorldPosition;

void main() {
    outWorldPosition = vec4(inWorldPosition, 1.0);
    if (inLightmapPage < -1.5) {
        // Negative page -2 means this material is lit but has no baked chart
        // (for example, a movable mesh). It can still receive realtime diffuse light.
        outLightmapInfo = vec4(0.0, 0.0, 0.0, 0.5);
        return;
    }
    if (inLightmapPage < 0.0) {
        // Negative page -1 means explicitly unlit: no lightmap or diffuse shading.
        outLightmapInfo = vec4(0.0);
        return;
    }
    outLightmapInfo = vec4(inLightmapUV, clamp(floor(inLightmapPage + 0.5), 0.0, 255.0) / 255.0, 1.0);
}
