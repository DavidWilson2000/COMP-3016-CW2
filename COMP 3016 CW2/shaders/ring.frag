#version 410 core
in vec3 vPosWS;
in vec3 vNormalWS;
out vec4 FragColor;

uniform sampler2D uRingTex;

uniform vec3  uViewPos;
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform float uAmbientStrength;

uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

void main()
{
    float u = atan(vPosWS.z, vPosWS.x) / (2.0 * 3.14159) + 0.5;
    float v = fract(vPosWS.y * 0.25);
    vec2 uv = vec2(u, v);

    vec4 tex = texture(uRingTex, uv);

    // 1) kill transparent pixels so they don’t fog into dirty halos
    if (tex.a < 0.15) discard;

    vec3 albedo = tex.rgb;

    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(-uLightDir);

    float diff = max(dot(N, L), 0.0);
    vec3 color = (uAmbientStrength * albedo) + (diff * albedo * uLightColor);

    // fog (exp2 style)
    if (uFogEnabled > 0.5)
    {
        float d = length(uViewPos - vPosWS);
        float f = exp(-(uFogDensity * d) * (uFogDensity * d));
        f = clamp(f, 0.0, 1.0);
        color = mix(uFogColor, color, f);
    }

    // 2) keep alpha from texture
    FragColor = vec4(color, tex.a);
}
