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

// --- NIGHT SPOTLIGHT (beam source) ---
uniform vec3  uSpotPos;
uniform vec3  uSpotDir;          // should be normalized
uniform vec3  uSpotColor;
uniform float uSpotIntensity;    // 0 = off
uniform float uSpotInnerCos;     // cos(innerAngle)
uniform float uSpotOuterCos;     // cos(outerAngle)

vec3 LighthousePaint(vec3 wsPos)
{
    // Classic red/white horizontal stripes:
    float stripeScale = 0.18;             // stripe thickness (tweak)
    float t = wsPos.y * stripeScale;
    float s = fract(t);

    vec3 red   = vec3(0.75, 0.08, 0.08);
    vec3 white = vec3(0.92, 0.92, 0.90);

    // crisp stripes:
    return (s < 0.5) ? white : red;
}

void main()
{
    vec3 N = normalize(vNormalWS);
    vec3 V = normalize(uViewPos - vPosWS);

    // fixed “paint” color (not affected by biome)
    vec3 albedo = LighthousePaint(vPosWS);

    // Sun light (directional)
    vec3 L = normalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);

    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), uShininess);

    vec3 color = uAmbientStrength * albedo
               + diff * albedo * uLightColor
               + uSpecStrength * spec * uLightColor;

    // Spotlight contribution (only matters at night when intensity > 0)
    vec3 toFrag = vPosWS - uSpotPos;
    float dist = length(toFrag);
    vec3  dirToFrag = toFrag / max(dist, 0.0001);

    // spot cone test: compare direction with spotlight forward
    float cd = dot(normalize(uSpotDir), normalize(dirToFrag)); // 1 = straight ahead
    float cone = smoothstep(uSpotOuterCos, uSpotInnerCos, cd);

    // distance attenuation (tweak)
    float atten = 1.0 / (1.0 + 0.06 * dist + 0.015 * dist * dist);

    // lambert from spotlight direction (light comes from spot -> frag)
    vec3 Ls = normalize(uSpotPos - vPosWS);
    float diffS = max(dot(N, Ls), 0.0);

    color += (diffS * albedo) * uSpotColor * (uSpotIntensity * cone * atten);

    // Fog
    if (uFogEnabled > 0.5)
    {
        float d = length(uViewPos - vPosWS);
        float f = exp(-uFogDensity * d);
        f = clamp(f, 0.0, 1.0);
        color = mix(uFogColor, color, f);
    }

    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
