#version 410 core
out vec4 FragColor;

in vec3 vWorldPos;
in vec3 vWorldNormal;

uniform vec3 uViewPos;

uniform vec3 uLightDir;
uniform vec3 uLightColor;

uniform float uAmbientStrength;
uniform float uSpecStrength;
uniform float uShininess;

// MUST match your C++ sea level
uniform float uSeaLevel;

void main()
{
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(-uLightDir);

    float diff = max(dot(N, L), 0.0);

    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), uShininess);

    // --------- Terrain blending factors ----------
    float height = vWorldPos.y;

    // slope: 1 = flat, 0 = vertical
    float slope = clamp(N.y, 0.0, 1.0);

    // height bands
    float sandLine  = uSeaLevel + 0.25;
    float grassLine = uSeaLevel + 2.5;
    float rockLine  = uSeaLevel + 5.5;
    float snowLine  = uSeaLevel + 7.5;

    // base colors
    vec3 sand = vec3(0.72, 0.62, 0.42);
    vec3 grass = vec3(0.12, 0.35, 0.14);
    vec3 rock = vec3(0.35, 0.33, 0.32);
    vec3 snow = vec3(0.95, 0.95, 0.97);

    // sand -> grass
    float tSandGrass = smoothstep(sandLine, grassLine, height);
    vec3 col = mix(sand, grass, tSandGrass);

    // grass -> rock (height based)
    float tGrassRock = smoothstep(grassLine, rockLine, height);
    col = mix(col, rock, tGrassRock);

    // rock on steep slopes (slope based override)
    float steep = 1.0 - slope; // steeper = closer to 1
    float rockOnSlope = smoothstep(0.35, 0.70, steep);
    col = mix(col, rock, rockOnSlope);

    // rock -> snow (height based)
    float tRockSnow = smoothstep(snowLine - 0.6, snowLine, height);
    col = mix(col, snow, tRockSnow);

    // --------- Lighting ----------
    vec3 ambient = uAmbientStrength * col;
    vec3 lit = (diff * col + uSpecStrength * spec) * uLightColor;

    FragColor = vec4(ambient + lit, 1.0);
}
