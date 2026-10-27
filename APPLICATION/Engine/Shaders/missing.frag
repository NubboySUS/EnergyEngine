#version 450

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 2) in vec2 inLightmapUV;
layout(location = 3) flat in float inLightmapPage;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outWorldPosition;

void main() {
    ivec2 cell = ivec2(floor(fract(inUV) * 2.0));
    outColor = (((cell.x + cell.y) & 1) == 0) ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    outWorldPosition = vec4(inWorldPosition, 1.0);
}
