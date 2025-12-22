#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;

float wave(vec2 p, float t)
{
    float w1 = sin(p.x * 0.35 + t * 1.2) * 0.12;
    float w2 = cos(p.y * 0.28 + t * 1.5) * 0.10;
    float w3 = sin((p.x + p.y) * 0.18 + t * 0.8) * 0.08;
    return w1 + w2 + w3;
}

void main()
{
    vec3 pos = aPos;
    pos.y += wave(pos.xz, uTime);

    vec4 worldPos = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;

    // Approx normal from slope (cheap but good enough)
    float eps = 0.25;
    float hL = wave(pos.xz - vec2(eps, 0.0), uTime);
    float hR = wave(pos.xz + vec2(eps, 0.0), uTime);
    float hD = wave(pos.xz - vec2(0.0, eps), uTime);
    float hU = wave(pos.xz + vec2(0.0, eps), uTime);

    vec3 n = normalize(vec3(hL - hR, 2.0 * eps, hD - hU));
    vNormal = mat3(transpose(inverse(uModel))) * n;

    gl_Position = uProj * uView * worldPos;
}
