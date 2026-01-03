// shaders/water.frag (UPDATED)
// - Fixes "too bright water" by supporting additive lighthouse passes that output ONLY lighthouse contribution
// - Avoids gamma-correcting additive passes (gamma only on the base pass)

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

// Lighthouse / beam
uniform vec3  uPointLightPos;
uniform vec3  uPointLightColor;
uniform float uPointLightIntensity;
uniform vec3  uBeamDir;
uniform float uBeamInnerCos;
uniform float uBeamOuterCos;
uniform float uBeamRange;

// Fog
uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

// NEW: when 1.0, output ONLY lighthouse contribution (for additive blending)
uniform float uAdditiveOnly;

void main()
{
    vec3 N = normalize(fs_in.normal);
    vec3 V = normalize(uViewPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);

    // Water base colour (slightly varies with view angle)
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 deep    = vec3(0.02, 0.10, 0.16);
    vec3 shallow = vec3(0.05, 0.22, 0.28);
    vec3 baseCol = mix(shallow, deep, fresnel);

    vec3 color = vec3(0.0);

    // ----------------------------
    // Base sun/sky lighting (ONLY in the normal pass)
    // ----------------------------
    if (uAdditiveOnly < 0.5)
    {
        vec3 ambient = uAmbientStrength * baseCol;

        float diff = max(dot(N, L), 0.0);
        vec3 diffuse = diff * baseCol * uLightColor;

        vec3 H = normalize(L + V);
        float spec = pow(max(dot(N, H), 0.0), uShininess);
        vec3 specular = uSpecStrength * spec * uLightColor;

        color = ambient + diffuse + specular;
    }

    // ----------------------------
    // Lighthouse spotlight on water
    // (This is what we add during additive passes)
    // ----------------------------
    vec3 LpVec = uPointLightPos - fs_in.worldPos;
    float distP = length(LpVec);

    if (uPointLightIntensity > 0.0001 && distP > 0.0001)
    {
        // Hard stop (cheap early out)
        if (distP <= uBeamRange)
        {
            vec3 Lp = LpVec / distP;

            // attenuation (tune as you like)
            float atten = 1.0 / (1.0 + 0.02 * distP + 0.0008 * distP * distP);

            // cone test
            vec3 lightToFrag = normalize(fs_in.worldPos - uPointLightPos);
            float cosAng = dot(lightToFrag, normalize(uBeamDir));
            float spot = smoothstep(uBeamOuterCos, uBeamInnerCos, cosAng);

            // range fade so it dies smoothly near cutoff
            float rangeFade = 1.0 - smoothstep(uBeamRange * 0.75, uBeamRange, distP);

            // diffuse + spec from point light
            float diffP = max(dot(N, Lp), 0.0);

            vec3 Hp = normalize(Lp + V);
            float specP = pow(max(dot(N, Hp), 0.0), uShininess * 2.0);

            vec3 pointDiffuse  = diffP * baseCol * uPointLightColor;
            vec3 pointSpecular = (uSpecStrength * 1.5) * specP * uPointLightColor;

            vec3 beamLight = (pointDiffuse + pointSpecular) * atten * uPointLightIntensity;

            color += beamLight * spot * rangeFade;
        }
    }

    // Fog (apply in both passes so additive respects atmosphere)
    if (uFogEnabled > 0.5)
    {
        float dist = length(uViewPos - fs_in.worldPos);
        float fogFactor = exp(-uFogDensity * dist);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        color = mix(uFogColor, color, fogFactor);
    }

    // Gamma: ONLY do this in the base pass.
    // Additive passes should stay linear so blending behaves.
    if (uAdditiveOnly < 0.5)
    {
        color = pow(color, vec3(1.0 / 2.2));
    }

    FragColor = vec4(color, 1.0);
}
