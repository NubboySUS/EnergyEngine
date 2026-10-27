#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D worldPositionTexture;
layout(set = 0, binding = 3) uniform sampler2D lightmapInfoTexture;
layout(set = 1, binding = 0) uniform sampler2DArray lightmapAtlas;

layout(set = 2, binding = 0, std140) uniform LightmapRuntimeBlock {
    vec4 matUV;
    vec4 color;
    vec4 colorMapUV;
    vec4 textureFlags[2];
    vec4 params[32];
} lightmapRuntime;

const int MAX_REALTIME_LIGHTS = 16;
layout(set = 3, binding = 0, std140) uniform RealtimeLightingBlock {
    vec4 positionRadius[MAX_REALTIME_LIGHTS];
    vec4 colorIntensity[MAX_REALTIME_LIGHTS];
    vec4 sunDirectionIntensity;
    vec4 sunColor;
    vec4 metadata; // X = point-light count, Y = realtime sun enabled
} realtimeLighting;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

const float LIGHTMAP_RGBM_RANGE = 16.0;

vec3 DecodeLightmap(vec4 encoded) {
    return encoded.rgb * encoded.a * LIGHTMAP_RGBM_RANGE;
}

vec3 SampleLightmap(vec2 uv, int layer) {
    ivec3 dimensions = textureSize(lightmapAtlas, 0);
    if (layer < 0 || layer >= dimensions.z) return vec3(0.0);

    vec2 size = vec2(dimensions.xy);
    vec2 clampedUV = clamp(uv, vec2(0.5) / size, vec2(1.0) - vec2(0.5) / size);
    vec2 texelPosition = clampedUV * size - vec2(0.5);
    ivec2 p0 = clamp(ivec2(floor(texelPosition)), ivec2(0), dimensions.xy - ivec2(1));
    ivec2 p1 = min(p0 + ivec2(1), dimensions.xy - ivec2(1));
    vec2 blend = fract(texelPosition);

    vec3 c00 = DecodeLightmap(texelFetch(lightmapAtlas, ivec3(p0, layer), 0));
    vec3 c10 = DecodeLightmap(texelFetch(lightmapAtlas, ivec3(p1.x, p0.y, layer), 0));
    vec3 c01 = DecodeLightmap(texelFetch(lightmapAtlas, ivec3(p0.x, p1.y, layer), 0));
    vec3 c11 = DecodeLightmap(texelFetch(lightmapAtlas, ivec3(p1, layer), 0));
    return mix(mix(c00, c10, blend.x), mix(c01, c11, blend.x), blend.y);
}

vec3 LinearToSRGB(vec3 value) {
    value = max(value, vec3(0.0));
    bvec3 low = lessThanEqual(value, vec3(0.0031308));
    vec3 linearPart = value * 12.92;
    vec3 nonlinearPart = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(nonlinearPart, linearPart, low);
}

vec3 EvaluateIllumination(vec2 lightmapUV, int page, vec3 worldPosition) {
    int pageCount = max(int(lightmapRuntime.params[31].x + 0.5), 1);
    int staticLightCount = clamp(int(lightmapRuntime.params[31].y + 0.5), 0, 15);
    int hasStaticSun = lightmapRuntime.params[31].z > 0.5 ? 1 : 0;

    vec3 illumination = SampleLightmap(lightmapUV, page);
    if (hasStaticSun != 0) {
        int sunLayer = pageCount + page;
        illumination += SampleLightmap(lightmapUV, sunLayer) * max(lightmapRuntime.params[30].rgb, vec3(0.0));
    }

    for (int i = 0; i < staticLightCount; ++i) {
        vec4 positionRadius = lightmapRuntime.params[i * 2];
        vec4 colorIntensity = lightmapRuntime.params[i * 2 + 1];
        float radius = max(positionRadius.w, 0.001);
        float distanceToLight = length(worldPosition - positionRadius.xyz);
        float edge = max(1.0 - distanceToLight / radius, 0.0);
        float radial = edge * edge;
        int groupIndex = 1 + hasStaticSun + i;
        int lightLayer = groupIndex * pageCount + page;
        vec3 response = SampleLightmap(lightmapUV, lightLayer);
        illumination += response * max(colorIntensity.rgb, vec3(0.0)) *
            max(colorIntensity.w, 0.0) * radial;
    }
    return max(illumination, vec3(0.0));
}

vec3 EvaluateRealtimeDiffuse(vec3 worldPosition, vec3 normal) {
    vec3 direct = vec3(0.0);
    int lightCount = clamp(int(realtimeLighting.metadata.x + 0.5), 0, MAX_REALTIME_LIGHTS);
    for (int i = 0; i < lightCount; ++i) {
        vec4 positionRadius = realtimeLighting.positionRadius[i];
        vec4 colorIntensity = realtimeLighting.colorIntensity[i];
        vec3 toLight = positionRadius.xyz - worldPosition;
        float distanceSquared = dot(toLight, toLight);
        float radius = max(positionRadius.w, 0.001);
        if (distanceSquared <= 1e-8 || distanceSquared >= radius * radius) continue;
        float distanceToLight = sqrt(distanceSquared);
        vec3 lightDirection = toLight / distanceToLight;
        float nDotL = max(dot(normal, lightDirection), 0.0);
        float edge = max(1.0 - distanceToLight / radius, 0.0);
        direct += max(colorIntensity.rgb, vec3(0.0)) * max(colorIntensity.w, 0.0) *
            (nDotL * edge * edge / (distanceSquared + 0.25));
    }
    if (realtimeLighting.metadata.y > 0.5) {
        vec3 sunDirection = normalize(realtimeLighting.sunDirectionIntensity.xyz);
        direct += max(realtimeLighting.sunColor.rgb, vec3(0.0)) * (3.0 * max(dot(normal, sunDirection), 0.0));
    }
    return max(direct, vec3(0.0));
}

void main() {
    vec4 color = texture(sceneColor, inUV);
    if (texture(worldPositionTexture, inUV).a < 0.5) {
        outColor = color;
        return;
    }
    ivec2 extent = textureSize(sceneColor, 0);
    ivec2 pixel = clamp(ivec2(inUV * vec2(extent)), ivec2(0), extent - ivec2(1));
    vec4 world = texelFetch(worldPositionTexture, pixel, 0);
    vec4 lightmapInfo = texelFetch(lightmapInfoTexture, pixel, 0);
    bool visualizeOnly = lightmapRuntime.params[31].w > 0.5;
    int pageCount = int(lightmapRuntime.params[31].x + 0.5);
    if (lightmapRuntime.params[31].w > 0.5) {
        if (pageCount < 1 || lightmapInfo.a < 0.75) {
            outColor = vec4(1.0);
            return;
        }
        int page = clamp(int(round(lightmapInfo.z * 255.0)), 0, 255);
        vec3 radiance = EvaluateIllumination(lightmapInfo.xy, page, world.xyz);
        vec3 displayRadiance = radiance / (vec3(1.0) + radiance);
        outColor = vec4(LinearToSRGB(displayRadiance), 1.0);
        return;
    }
    if (lightmapInfo.a < 0.25) {
        outColor = color;
        return;
    }
    vec3 bakedIllumination = vec3(1.0);
    if (pageCount > 0 && lightmapInfo.a > 0.75) {
        int page = clamp(int(round(lightmapInfo.z * 255.0)), 0, 255);
        bakedIllumination = EvaluateIllumination(lightmapInfo.xy, page, world.xyz);
    }
    vec3 realtimeDiffuse = vec3(0.0);
    if (realtimeLighting.metadata.x > 0.5 || realtimeLighting.metadata.y > 0.5) {
        vec3 dWorldDx = dFdx(world.xyz);
        vec3 dWorldDy = dFdy(world.xyz);
        vec3 normal = cross(dWorldDx, dWorldDy);
        float normalLengthSquared = dot(normal, normal);
        if (normalLengthSquared > 1e-12) {
            normal *= inversesqrt(normalLengthSquared);
            vec3 toCamera = lightmapRuntime.matUV.xyz - world.xyz;
            if (dot(normal, toCamera) < 0.0) normal = -normal;
            realtimeDiffuse = EvaluateRealtimeDiffuse(world.xyz, normal);
        }
    }
    color.rgb = color.rgb * bakedIllumination + color.rgb * realtimeDiffuse;
    outColor = color;
}