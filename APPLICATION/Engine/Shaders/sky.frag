#version 450

layout(set = 0, binding = 0, std140) uniform MaterialBlock {
    vec4 matUV; // XY = SCALE; ZW = OFFSET
    vec4 color; // RGBA
    vec4 colorMapUV; // XY = SCALE; ZW = OFFSET
    vec4 textureFlags[2];
    vec4 params[32];
} material;

layout(set = 0, binding = 1) uniform sampler2D textures[8];

layout(push_constant) uniform SkyPushConstants {
    mat4 viewProjection;
    vec4 cameraPosition;         // XYZ = CAMERA POS; W = SKY MODE
    vec4 sunDirectionAndSize;    // XYZ = WORLD SUN DIR, W = ANGULAR / SCREEN RADIUS
    vec4 timeData;               // X = ELAPSED TIME; Y = DELTA TIME; Z = SIN( ELAPSED TIME ); W = SIN( DELTA TIME )
    vec4 screenData;             // X = RENDER WIDTH; Y = RENDER HEIGHT; Z = ASPECT RATIO
    vec4 frameData;              // X = FRAME INDEX
} pc;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPosition;
layout(location = 2) in vec3 inLocalPosition;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outWorldPosition;
layout(location = 2) out vec4 outSkyColor;

const float PI = 3.14159265358979323846;

// SKY COLORS
const vec3 dayZenith    = vec3(0.035, 0.145, 0.43);
const vec3 dayHorizon   = vec3(0.48,  0.68,  0.86);
const vec3 duskZenith   = vec3(0.075, 0.095, 0.20);
const vec3 duskHorizon  = vec3(1.00,  0.255, 0.075);
const vec3 nightZenith  = vec3(0.002, 0.005, 0.018);
const vec3 nightHorizon = vec3(0.008, 0.014, 0.035);

// SUN COLORS
const vec3 highSunColor    = vec3(1.0, 1.0,  1.0);
const vec3 horizonSunColor = vec3(1.0, 0.35, 0.05);
const vec3 deepSunsetColor = vec3(1.0, 0.10, 0.02);

void main() {
    int skyMode = int(pc.cameraPosition.w + 0.5);
    vec3 sunDirection = normalize(pc.sunDirectionAndSize.xyz);
    float sunSize = max(pc.sunDirectionAndSize.w, 0.0);
        float sunUp = clamp(sunDirection.z, -1.0, 1.0);

    vec4 color;
    if (material.textureFlags[0].x < 0.5 || material.textureFlags[0].y < 0.5 || material.textureFlags[0].z < 0.5 || material.textureFlags[0].w < 0.5 || material.textureFlags[1].x < 0.5 || material.textureFlags[1].y < 0.5) {
        if (skyMode != 1)
            color = texture(textures[0], inUV) * material.color;
        else {
            vec3 a = abs(inLocalPosition);
            int faceIndex = inLocalPosition.z >= 0.0 ? 4 : 5; // +Z, -Z
            if (a.x >= a.y && a.x >= a.z)
                faceIndex = inLocalPosition.x >= 0.0 ? 0 : 1; // +X, -X
            else if (a.y >= a.x && a.y >= a.z)
                faceIndex = inLocalPosition.y >= 0.0 ? 2 : 3; // +Y, -Y
            uint faceMask = uint(max(material.params[31u].w, 0.0) + 0.5);
            bool faceHasTexture = (faceMask & (1u << uint(faceIndex))) != 0u;
            switch (faceHasTexture ? faceIndex : 0) {
                case 1: color = texture(textures[1], inUV) * material.color; break;
                case 2: color = texture(textures[2], inUV) * material.color; break;
                case 3: color = texture(textures[3], inUV) * material.color; break;
                case 4: color = texture(textures[4], inUV) * material.color; break;
                case 5: color = texture(textures[5], inUV) * material.color; break;
                default:color = texture(textures[0], inUV) * material.color; break;
            }
        }

        // DO NO INCLUDE CELESTIAL BODIES
        outSkyColor = color;
    } else {
        vec3 viewDirection = vec3(0.0, 0.0, 0.0);
        if (skyMode != 3)
            viewDirection = normalize(inWorldPosition - pc.cameraPosition.xyz);
        else {
            vec2 ndc = vec2(1.0) - inUV * 2.0;
            mat4 inverseViewProjection = inverse(pc.viewProjection);
            vec4 farPoint = inverseViewProjection * vec4(ndc, 0.0, 1.0);
            if (abs(farPoint.w) > 1e-6)
                viewDirection = normalize(farPoint.xyz / farPoint.w - pc.cameraPosition.xyz);
            else
                viewDirection = normalize(farPoint.xyz);
        }
        float viewUp = clamp(viewDirection.z, -1.0, 1.0);

        float daylight = smoothstep(-0.14, 0.10, sunUp);
        float twilight = exp(-abs(sunUp) * 7.0);
        vec3 zenith = mix(nightZenith, dayZenith, daylight);
        vec3 horizon = mix(nightHorizon, dayHorizon, daylight);
        zenith = mix(zenith, duskZenith, twilight * 0.78);
        horizon = mix(horizon, duskHorizon, twilight);
        float positiveViewUp = max(viewUp, 0.0);
        vec3 baseSky = mix(horizon, zenith, pow(positiveViewUp, 0.48));

        // RAYLEIGH SCATTERING
        float mu = clamp(dot(viewDirection, sunDirection), -1.0, 1.0);
        float rayleighPhase = 0.75 * (1.0 + mu * mu);
        float airMass = min(6.0, 1.0 / max(positiveViewUp + 0.15, 0.15));
        vec3 opticalDepth = vec3(0.25, 0.65, 1.25) * airMass;
        vec3 scatteredFraction = vec3(1.0) - exp(-opticalDepth);
        vec3 rayleighTint = vec3(0.16, 0.40, 1.0);
        vec3 sky = baseSky;
        float belowHorizon = 1.0 - smoothstep(-0.16, 0.025, viewUp);
        sky = mix(sky, nightZenith * 0.65, belowHorizon * 0.78);
        sky += scatteredFraction * rayleighTint * rayleighPhase * daylight * 0.22;

        // DO NO INCLUDE CELESTIAL BODIES
        vec3 smoothAboveHorizon = mix(horizon, baseSky, smoothstep(0.0, 0.1, viewUp));
        vec3 deepFogColor = horizon * 0.6;
        vec3 belowHorizonFog = mix(horizon, deepFogColor, smoothstep(0.0, 1.0, max(-viewUp, 0.0)));
        outSkyColor = vec4(max(mix(belowHorizonFog, smoothAboveHorizon, smoothstep(-0.05, 0.0, viewUp)) + scatteredFraction * rayleighTint * rayleighPhase * daylight * 0.22, vec3(0.0)) * material.color.rgb, 1.0);

        // HENYEY-GREENSTEIN MEI PHASE APPROXIMATION
        float g = 0.76;
        float mieDenominator = max(1.0 + g * g - 2.0 * g * mu, 0.08);
        float miePhase = (1.0 - g * g) / (4.0 * PI * mieDenominator * sqrt(mieDenominator));
        vec3 sunTint = mix(vec3(1.0, 0.26, 0.075), max(highSunColor, vec3(0.0)), smoothstep(0.02, 0.30, sunUp));
        sky += sunTint * miePhase * smoothstep(-0.20, 0.05, sunUp) * 4.2 * sunSize;

        // LOW-ALTITUDE WARM BAND
        vec2 viewAzimuth = viewDirection.xy;
        vec2 sunAzimuth = sunDirection.xy;
        float viewAzimuthLength = length(viewAzimuth);
        float sunAzimuthLength = length(sunAzimuth);
        if (viewAzimuthLength > 1e-4 && sunAzimuthLength > 1e-4) {
            float azimuthAlignment = max(dot(viewAzimuth / viewAzimuthLength, sunAzimuth / sunAzimuthLength), 0.0);
            float horizonBand = exp(-abs(viewUp) * 11.0);
            float duskGlow = exp(-abs(sunUp) * 6.0);
            float alignedGlow = pow(azimuthAlignment, 8.0) * horizonBand * duskGlow;
            sky += vec3(1.0, 0.18, 0.035) * alignedGlow * 0.48;
        }

        color = vec4(max(sky, vec3(0.0)) * material.color.rgb, 1.0);
    }

    // DRAW SUN
    if (sunSize > 0.00001) {
        vec2 sunUV = vec2(0.5);
        float sunCoverage = 0.0;
        float distMetric = 0.0;

        if (skyMode == 3) {
            vec4 sunClip = pc.viewProjection * vec4(pc.cameraPosition.xyz + sunDirection * 1000.0, 1.0);
            if (sunClip.w > 0.0001) {
                vec2 delta = vec2(1.0) - inUV * 2.0 - sunClip.xy / sunClip.w;
                delta.x *= max(pc.screenData.z, 0.0001);
                float radius = max(sunSize, 0.0001);
                sunUV = delta / (2.0 * radius) + vec2(0.5);
                
                if (material.textureFlags[1].w < 0.5) {
                    sunCoverage = (all(greaterThanEqual(sunUV, vec2(0.0))) && all(lessThanEqual(sunUV, vec2(1.0)))) ? 1.0 : 0.0;
                } else {
                    distMetric = length(delta) / radius;
                }
            }
        } else {
            vec3 rayDirection = normalize(inWorldPosition - pc.cameraPosition.xyz);
            vec3 right = normalize(cross(sunDirection,
                abs(sunDirection.z) > 0.98 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0)));
            vec3 up = normalize(cross(right, sunDirection));
            sunUV = vec2(dot(rayDirection, right), dot(rayDirection, up)) / (2.0 * max(tan(sunSize), 0.0001)) + vec2(0.5);
            
            if (material.textureFlags[1].w < 0.5) {
                sunCoverage = (all(greaterThanEqual(sunUV, vec2(0.0))) && all(lessThanEqual(sunUV, vec2(1.0)))) ? 1.0 : 0.0;
            } else {
                distMetric = acos(clamp(dot(rayDirection, sunDirection), -1.0, 1.0)) / max(sunSize, 0.0001);
            }
        }

        if (material.textureFlags[1].w < 0.5) {
            if (sunCoverage > 0.0 && all(greaterThanEqual(sunUV, vec2(0.0))) && all(lessThanEqual(sunUV, vec2(1.0)))) {
                vec4 sunImage = texture(textures[7], sunUV);
                color.rgb = mix(color.rgb, sunImage.rgb * highSunColor, sunCoverage * sunImage.a);
            }
        } else {
            float disk = 1.0 - smoothstep(0.85, 1.0, distMetric);
            float bloom = exp(-max(0.0, distMetric - 0.5) * 1.8) * 1.2;
            vec3 highSunColor = max(highSunColor, vec3(0.0));
            vec3 dynamicBloomTint = mix(deepSunsetColor, mix(horizonSunColor, highSunColor, smoothstep(-0.15, 0.35, sunUp)), smoothstep(-0.1, 0.15, sunUp));
            color.rgb = mix(color.rgb + dynamicBloomTint * bloom * (1.0 - disk * 0.5), vec3(1.0), disk);
        }
    }

    outColor = color;
    outWorldPosition = vec4(inWorldPosition, 0.0);
}