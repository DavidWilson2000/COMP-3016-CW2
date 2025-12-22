#version 410 core
out vec4 FragColor;

in vec3 vWorldPos;
in vec3 vNormal;

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

    float diff = max(dot(N, L), 0.0);

    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), uShininess);

    // simple colour: trunk darker near base, foliage greener higher up
    vec3 trunk = vec3(0.25, 0.18, 0.10);
    vec3 leaf  = vec3(0.10, 0.35, 0.12);
    float t = smoothstep(0.4, 1.6, vWorldPos.y); // blends as y increases
    vec3 baseColor = mix(trunk, leaf, t);

    vec3 ambient = uAmbientStrength * baseColor;
    vec3 lit = (diff * baseColor + uSpecStrength * spec) * uLightColor;

    FragColor = vec4(ambient + lit, 1.0);
}
