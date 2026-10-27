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
layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPosition;
layout(location = 2) out vec3 outLocalPosition;

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
    int skyMode = int(pc.cameraPosition.w + 0.5);
    if (skyMode == 3) {
        const vec2 positions[3] = vec2[3](
            vec2(-1.0, -1.0),
            vec2( 3.0, -1.0),
            vec2(-1.0,  3.0)
        );
        vec2 position = positions[gl_VertexIndex];
        gl_Position = vec4(position, 0.0, 1.0);
        outUV = vec2(1.0) - (position * 0.5 + 0.5);
        outWorldPosition = vec3(0.0);
        outLocalPosition = vec3(0.0);
    } else {
        vec3 worldPosition = RotateEuler(inPosition * inInstanceSize, inInstanceRotation) + inInstancePosition;
        vec4 clipPosition = pc.viewProjection * vec4(worldPosition, 1.0);
        gl_Position = vec4(clipPosition.x, -clipPosition.y, 0.0, clipPosition.w);
        outUV = inUV;
        outWorldPosition = worldPosition;
    }
}