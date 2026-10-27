#version 450

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
    float col = 0.25 + 0.25 * sin(pc.timeData.x * 10);
    outColor = vec4(1.0, col, col, 1.0);
    outWorldPosition = vec4(inWorldPosition, 0.0);
}
