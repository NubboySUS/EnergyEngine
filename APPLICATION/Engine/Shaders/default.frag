#version 450

layout(set = 0, binding = 0, std140) uniform MaterialBlock {
    vec4 matUV; // XY = SCALE; ZW = OFFSET
    vec4 color; // RGBA
    vec4 colorMapUV; // XY = SCALE; ZW = OFFSET
    vec4 textureFlags[2];
    vec4 params[32];
} material;

layout(set = 0, binding = 1) uniform sampler2D textures[8];

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    vec4 cameraPosition; // XYZ = CAMERA POS; W = SKY MODE
    vec4 skyParams;      // XYZ = WORLD SUN DIR, W = ANGULAR / SCREEN RADIUS
    vec4 timeData;       // X = ELAPSED TIME; Y = DELTA TIME; Z = SIN( ELAPSED TIME ); W = SIN( DELTA TIME )
    vec4 screenData;     // X = RENDER WIDTH; Y = RENDER HEIGHT; Z = ASPECT RATIO
    vec4 frameData;      // X = FRAME INDEX
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 2) in vec2 inLightmapUV;
layout(location = 3) flat in float inLightmapPage;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outWorldPosition;

void main() {
    vec4 color;
    vec2 uv = inUV * material.colorMapUV.xy + material.colorMapUV.zw;
    if (material.textureFlags[0].x > 0.5)
        color = material.color;
    else
        color = texture(textures[0], uv * material.matUV.xy + material.matUV.zw) * material.color;
    outColor = color;
    outWorldPosition = vec4(inWorldPosition, 1.0);
}