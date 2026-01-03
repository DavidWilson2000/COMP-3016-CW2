#version 410 core

in vec3 vNormalWS;
in vec3 vPosWS;
in float vHeight01;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;

uniform vec3  uPointLightPos;
uniform vec3  uPointLightColor;
uniform float uPointLightIntensity;

uniform vec3  uBeamDir;
uniform float uBeamInnerCos;
uniform float uBeamOuterCos;

uniform float uAmbientStrength;
uniform float uSpecStrength;
uniform float uShininess;

uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

uniform float uTrunkFrac; // 0..1

void main()
{
    vec3 leafCol = vec3(36.0/255.0, 138.0/255.0, 41.0/255.0);
    vec3 barkCol = vec3(86.0/255.0, 53.0/255.0, 4.0/255.0);

    vec3 albedo = (vHeight01 < uTrunkFrac) ? barkCol : leafCol;

    vec3 N = normalize(vNormalWS);
    vec3 V = normalize(uViewPos - vPosWS);

    // -------------------------
    // Sun / directional light
    // -------------------------
    vec3 L = normalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);

    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), uShininess);

    vec3 ambient  = uAmbientStrength * albedo;
    vec3 diffuse  = diff * albedo * uLightColor;
    vec3 specular = uSpecStrength * spec * uLightColor;

    vec3 color = ambient + diffuse + specular;

    // -------------------------
    // Lighthouse point light + spotlight mask
    // -------------------------
    vec3 LpVec = uPointLightPos - vPosWS;
    float distP = length(LpVec);
    vec3 Lp = (distP > 0.0001) ? (LpVec / distP) : vec3(0.0, 1.0, 0.0);

    float atten = 1.0 / (1.0 + 0.05 * distP + 0.005 * distP * distP);
    float diffP = max(dot(N, Lp), 0.0);

    vec3 Hp = normalize(Lp + V);
    float specP = pow(max(dot(N, Hp), 0.0), uShininess);

    vec3 pointDiffuse  = diffP * albedo * uPointLightColor;
    vec3 pointSpecular = uSpecStrength * specP * uPointLightColor;

    vec3 pointLight = (pointDiffuse + pointSpecular) * atten * uPointLightIntensity;

    // beam cone mask
    vec3 lightToFrag = normalize(vPosWS - uPointLightPos);
    float cosAng = dot(lightToFrag, normalize(uBeamDir));
    float spot = smoothstep(uBeamOuterCos, uBeamInnerCos, cosAng);

    float beamAtten = 1.0 / (1.0 + 0.08 * distP + 0.01 * distP * distP);
    float lantern = 0.08;

    color += pointLight * (lantern + spot * beamAtten);

    // -------------------------
    // Fog + alpha fade
    // -------------------------
   if (uFogEnabled > 0.5)
{
    float d = length(uViewPos - vPosWS);

    // fog visibility (same curve you already use)
    float f = exp(-(uFogDensity * d) * (uFogDensity * d));
    f = clamp(f, 0.0, 1.0);

    // fog color mix
    color = mix(uFogColor, color, f);

    // ---- distance fade range (tune these) ----
    const float fadeStart = 220.0; // start fading further away
    const float fadeEnd   = 520.0; // fully gone even further away

    float fadeT = smoothstep(fadeEnd, fadeStart, d); // 1 near, 0 far

    // dither noise
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898,78.233))) * 43758.5453);

    if (n > fadeT) discard;
}

FragColor = vec4(color, 1.0);
}
