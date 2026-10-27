#version 450
layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D worldPositionTexture;
layout(set = 0, binding = 2) uniform sampler2D skyColorTexture;

layout(push_constant) uniform PostProcessConstants {
    vec4 cameraPosition;
    vec4 fogParams; // X = FOG START, Y = FOG END
    vec4 fogColor;  // XYZ = COLOR RGB, W = COLOR OVERRIDE MULTIPLIER
} pc;
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = texture(sceneColor, inUV);
    vec4 world = texture(worldPositionTexture, inUV);
    if (world.a < 0.5) {
        outColor = color;
        return;
    }
    float fog = clamp((distance(world.xyz, pc.cameraPosition.xyz) - pc.fogParams.x) / max(pc.fogParams.y - pc.fogParams.x, 0.00001), 0.0, 1.0);
    vec3 skyFogColor = mix(texture(skyColorTexture, inUV).rgb, pc.fogColor.xyz, pc.fogColor.w);
    outColor = vec4(mix(color.rgb, skyFogColor, fog), color.a);
}
