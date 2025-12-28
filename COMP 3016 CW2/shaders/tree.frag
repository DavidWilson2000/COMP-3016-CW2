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

// Fog
uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

void main()
{
    vec3 N = normalize(fs_in.normal);
    vec3 V = normalize(uViewPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);

    // Two-tone tree colour: trunk darker, foliage green-ish
    // (simple, readable in viva)
    float foliage = smoothstep(1.2, 2.4, fs_in.worldPos.y);
    vec3 baseCol = mix(vec3(0.22, 0.15, 0.08), vec3(0.05, 0.22, 0.08), foliage);

    vec3 ambient = uAmbientStrength * baseCol;

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * baseCol * uLightColor;

    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), uShininess);
    vec3 specular = uSpecStrength * spec * uLightColor;

    vec3 color = ambient + diffuse + specular;

    if (uFogEnabled > 0.5)
    {
        float dist = length(uViewPos - fs_in.worldPos);
        float fogFactor = exp(-uFogDensity * dist);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        color = mix(uFogColor, color, fogFactor);
    }
color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}


