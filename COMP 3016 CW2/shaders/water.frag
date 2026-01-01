#version 410 core

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
} fs_in;

out vec4 FragColor;

uniform vec3  uViewPos;
uniform vec3  uLightDir;
uniform vec3  uLightColor;

uniform float uAmbientStrength;
uniform float uSpecStrength;
uniform float uShininess;

// Lighthouse / beam (NEW)
uniform vec3  uPointLightPos;
uniform vec3  uPointLightColor;
uniform float uPointLightIntensity;
uniform vec3  uBeamDir;
uniform float uBeamInnerCos;
uniform float uBeamOuterCos;

// Fog
uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

void main()
{
    vec3 N = normalize(fs_in.normal);
    vec3 V = normalize(uViewPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);

    // Water base colour (slightly varies with view angle)
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 deep = vec3(0.02, 0.10, 0.16);
    vec3 shallow = vec3(0.05, 0.22, 0.28);
    vec3 baseCol = mix(shallow, deep, fresnel);

    vec3 ambient = uAmbientStrength * baseCol;

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * baseCol * uLightColor;

    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), uShininess);
    vec3 specular = uSpecStrength * spec * uLightColor;

    vec3 color = ambient + diffuse + specular;

    // ----------------------------
    // Lighthouse spotlight on water
    // ----------------------------
    vec3 LpVec = uPointLightPos - fs_in.worldPos;
    float distP = length(LpVec);

    if (uPointLightIntensity > 0.0001 && distP > 0.0001)
    {
      vec3 Lp = LpVec / distP;

// gentler attenuation so it actually shows on water
float atten = 1.0 / (1.0 + 0.02 * distP + 0.0008 * distP * distP);

// spotlight cone test (same as terrain)
vec3 lightToFrag = normalize(fs_in.worldPos - uPointLightPos);
float cosAng = dot(lightToFrag, normalize(uBeamDir));
float spot = smoothstep(uBeamOuterCos, uBeamInnerCos, cosAng);

// stronger spec hit on water looks good
vec3 Hp = normalize(Lp + V);
float specP = pow(max(dot(N, Hp), 0.0), uShininess * 2.0);

float diffP = max(dot(N, Lp), 0.0);

vec3 pointDiffuse  = diffP * baseCol * uPointLightColor;
vec3 pointSpecular = (uSpecStrength * 1.5) * specP * uPointLightColor;

vec3 beamLight = (pointDiffuse + pointSpecular) * atten * uPointLightIntensity;

color += beamLight * spot;

    }

    // Fog
    if (uFogEnabled > 0.5)
    {
        float dist = length(uViewPos - fs_in.worldPos);
        float fogFactor = exp(-uFogDensity * dist);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        color = mix(uFogColor, color, fogFactor);
    }

    // Gamma
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
