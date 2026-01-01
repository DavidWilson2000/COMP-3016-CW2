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
    vec3 L = normalize(-uLightDir);

    float diff = max(dot(N, L), 0.0);

    vec3 V = normalize(uViewPos - vPosWS);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), uShininess);

    vec3 ambient = uAmbientStrength * albedo;
    vec3 diffuse = diff * albedo * uLightColor;
    vec3 specular = uSpecStrength * spec * uLightColor;

    vec3 color = ambient + diffuse + specular;

    // ------------------------------------------------------------
    // Lighthouse point light (Blinn-Phong point light)
    // ------------------------------------------------------------
    vec3 LpVec = uPointLightPos - vPosWS;
    float distP = length(LpVec);

    vec3 Lp = (distP > 0.0001) ? (LpVec / distP) : vec3(0.0, 1.0, 0.0);

    float atten = 1.0 / (1.0 + 0.05 * distP + 0.005 * distP * distP);

    float diffP = max(dot(N, Lp), 0.0);

    // Blinn-Phong spec for point light (keep your style consistent)
    vec3 Hp = normalize(Lp + V);
    float specP = pow(max(dot(N, Hp), 0.0), uShininess);

    vec3 pointDiffuse  = diffP * albedo * uPointLightColor;
    vec3 pointSpecular = uSpecStrength * specP * uPointLightColor;

vec3 pointLight = (pointDiffuse + pointSpecular) * atten * uPointLightIntensity;

// ---- Spotlight cone mask ----
// Direction FROM light TO fragment:
vec3 lightToFrag = normalize(vPosWS - uPointLightPos);

// 1 = aligned with beam axis, 0 = 90 degrees off
float cosAng = dot(lightToFrag, normalize(uBeamDir));

// 0 outside cone, 1 near center (soft edge)
float spot = smoothstep(uBeamOuterCos, uBeamInnerCos, cosAng);

// Extra distance shaping so it doesn’t "wash" everything
float beamAtten = 1.0 / (1.0 + 0.08 * distP + 0.01 * distP * distP);

// Tiny “lantern” glow only (prevents the block look)
float lantern = 0.08; // tweak 0.03..0.15

color += pointLight * (lantern + spot * beamAtten);


    if (uFogEnabled > 0.5)
    {
        float dist = length(uViewPos - vPosWS);
        float fogFactor = exp(-uFogDensity * dist);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        color = mix(uFogColor, color, fogFactor);
    }

    FragColor = vec4(color, 1.0);
}
