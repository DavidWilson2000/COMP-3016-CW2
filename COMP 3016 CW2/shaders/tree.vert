#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

// Instance matrix occupies locations 2..5
layout(location = 2) in mat4 iModel;

uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
} vs_out;

void main()
{
    vec3 pos = aPos;

    // Small wind sway: only affect higher vertices (foliage area)
    float swayMask = smoothstep(0.8, 2.8, pos.y);
    pos.x += sin(uTime * 1.6 + pos.y * 2.0) * 0.03 * swayMask;
    pos.z += cos(uTime * 1.3 + pos.y * 1.7) * 0.03 * swayMask;

    vec4 wp = iModel * vec4(pos, 1.0);

    mat3 nMat = transpose(inverse(mat3(iModel)));
    vs_out.normal = normalize(nMat * aNormal);
    vs_out.worldPos = wp.xyz;

    gl_Position = uProj * uView * wp;
}
