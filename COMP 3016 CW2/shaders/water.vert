#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

uniform float uTime;
uniform float uWaveStrength;
uniform float uWaveSpeed;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
} vs_out;

// Simple wave function
float wave(vec2 xz, float t)
{
    float w = 0.0;
    w += sin(xz.x * 0.18 + t * 1.2);
    w += cos(xz.y * 0.15 + t * 1.0);
    w += sin((xz.x + xz.y) * 0.10 + t * 0.7);
    return w * 0.33;
}

void main()
{
    float t = uTime * uWaveSpeed;

    vec3 pos = aPos;
    pos.y += wave(pos.xz, t) * uWaveStrength;

    vec4 wp = uModel * vec4(pos, 1.0);
    vs_out.worldPos = wp.xyz;

    // Approx normal from wave derivatives (cheap + looks good)
    float eps = 0.05;
    float hL = wave(pos.xz - vec2(eps, 0), t) * uWaveStrength;
    float hR = wave(pos.xz + vec2(eps, 0), t) * uWaveStrength;
    float hD = wave(pos.xz - vec2(0, eps), t) * uWaveStrength;
    float hU = wave(pos.xz + vec2(0, eps), t) * uWaveStrength;

    vec3 dx = vec3(2.0 * eps, hR - hL, 0.0);
    vec3 dz = vec3(0.0, hU - hD, 2.0 * eps);
    vec3 n = normalize(cross(dz, dx));

    mat3 normalMat = transpose(inverse(mat3(uModel)));
    vs_out.normal = normalize(normalMat * n);

    gl_Position = uProj * uView * wp;
}

