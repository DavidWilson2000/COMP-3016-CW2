#version 410 core

in vec3 vWorldPos;
in vec3 vNormal;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;

uniform float uAmbientStrength;
uniform float uSpecStrength;
uniform float uShininess;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), uShininess);

    // Fresnel-ish: stronger reflections at grazing angles
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);

    vec3 deep = vec3(0.02, 0.10, 0.18);
    vec3 shallow = vec3(0.05, 0.25, 0.30);
    vec3 base = mix(deep, shallow, clamp((vWorldPos.y + 0.5) * 0.5, 0.0, 1.0));

    vec3 ambient  = uAmbientStrength * uLightColor;
    vec3 diffuse  = diff * uLightColor;
    vec3 specular = uSpecStrength * spec * uLightColor;

    vec3 col = (ambient + diffuse) * base + specular * (0.6 + 1.2 * fresnel);
    col += fresnel * vec3(0.25, 0.35, 0.45);

    FragColor = vec4(col, 0.95);
}
