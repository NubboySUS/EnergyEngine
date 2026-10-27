#version 450

layout(set = 0, binding = 0, std140) uniform MaterialBlock {
    vec4 matUV; // XY = SCALE; ZW = OFFSET
    vec4 color; // RGBA
    vec4 colorMapUV; // XY = SCALE; ZW = OFFSET
    vec4 textureFlags[2];
    vec4 params[32];
} material;

layout(set = 1, binding = 0) uniform sampler2DArray lightmapAtlas;
layout(set = 0, binding = 1) uniform sampler2D textures[8];
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 2) in vec2 inLightmapUV;
layout(location = 3) flat in float inLightmapPage;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outWorldPosition;

void main() {
    outColor = vec4(mix(material.color.rgb, vec3(material.color.b, material.color.r * 0.45, 1.0), step(0.48, fract((inUV.x + inUV.y * 0.65 + material.params[0].x) * 9.0))), material.color.a) * texture(textures[0], inUV);
    outWorldPosition = vec4(inWorldPosition, 1.0);
}