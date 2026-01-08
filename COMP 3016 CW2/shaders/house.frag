#version 410 core

in vec3 vPosWS;
in vec3 vNormalWS;

out vec4 FragColor;

uniform vec3  uViewPos;
uniform vec3  uLightDir;
uniform vec3  uLightColor;

uniform float uAmbientStrength;
uniform float uSpecStrength;
uniform float uShininess;

// Fog
uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

// Night / lantern point light
uniform float uNightFactor;
uniform vec3  uLanternPosWS;
uniform vec3  uLanternColor;
uniform float uLanternIntensity;

// Per-house tint
uniform vec3 uTint;

vec3 safeNormalize(vec3 v)
{
    float m2 = dot(v, v);
    if (m2 < 1e-10) return vec3(0.0, 1.0, 0.0);
    return v * inversesqrt(m2);
}

vec3 HousePaint(vec3 wsPos, vec3 N)
{
    // Keep tint safe (avoid accidental HDR blowout)
    vec3 wall = clamp(uTint, 0.0, 1.0);

    // Roof = faces that point upward
    float roofMask = smoothstep(0.55, 0.92, max(N.y, 0.0));
    vec3 roof = wall * vec3(0.65, 0.58, 0.52);

    // subtle variation across walls
    float v = 0.92 + 0.08 * sin(wsPos.x * 0.30 + wsPos.z * 0.25);
    wall *= v;

    return mix(wall, roof, roofMask);
}

void main()
{
    vec3 N = safeNormalize(vNormalWS);
    vec3 V = safeNormalize(uViewPos - vPosWS);

    vec3 albedo = HousePaint(vPosWS, N);

    // -------- Directional (sun) light --------
    vec3 L = safeNormalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);

    vec3 H = safeNormalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), max(uShininess, 1.0));

    vec3 color =
        (uAmbientStrength * albedo) +
        (diff * albedo * uLightColor) +
        (uSpecStrength * spec * uLightColor);

    // -------- Lantern point light (night only) --------
    vec3 toL = (uLanternPosWS - vPosWS);
    float dist = length(toL);
    vec3 Lp = toL / max(dist, 1e-4);

    float diffP = max(dot(N, Lp), 0.0);

    // attenuation tuned for village-ish distances
    float atten = 1.0 / (1.0 + 0.10 * dist + 0.02 * dist * dist);

    float lantern = max(uLanternIntensity, 0.0) * clamp(uNightFactor, 0.0, 1.0);
    color += (diffP * albedo) * uLanternColor * (lantern * atten);

    // -------- Fog --------
    if (uFogEnabled > 0.5)
    {
        float d = length(uViewPos - vPosWS);
        float f = exp(-uFogDensity * d);
        f = clamp(f, 0.0, 1.0);
        color = mix(uFogColor, color, f);
    }

    // -------- Simple tonemap + gamma (prevents white blowout) --------
    color = max(color, vec3(0.0));
    color = color / (color + vec3(1.0));          // Reinhard
    color = pow(color, vec3(1.0 / 2.2));          // gamma

    FragColor = vec4(color, 1.0);
}
