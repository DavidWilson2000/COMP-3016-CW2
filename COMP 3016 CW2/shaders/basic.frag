#version 410 core

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    float moisture;
    float height;
} fs_in;

out vec4 FragColor;

uniform vec3  uViewPos;
uniform vec3  uLightDir;
uniform vec3  uLightColor;

uniform float uAmbientStrength;
uniform float uSpecStrength;
uniform float uShininess;

uniform float uSeaLevel;

// Fog controls
uniform float uFogEnabled; // 1.0 or 0.0
uniform vec3  uFogColor;
uniform float uFogDensity;

// Per-island biome id from C++
// 0=Forest, 1=Grassland, 2=Snow, 3=Desert
uniform float uIslandBiome;

// NEW: per-island seed (pass isl.seed from C++)
uniform float uIslandSeed;

// ------------------------------------------------------------
// Hash / noise helpers (cheap, textureless)
// ------------------------------------------------------------
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float hash11(float n)
{
    return fract(sin(n) * 43758.5453);
}

float noise2f(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm2f(vec2 p)
{
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 5; i++)
    {
        sum += amp * noise2f(p * freq);
        freq *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

// float-safe biome equality mask
float biomeIs(float id)
{
    return 1.0 - step(0.5, abs(uIslandBiome - id));
}

// ------------------------------------------------------------
// Biome shading
// ------------------------------------------------------------
vec3 biomeColor(float h, float m, float slope)
{
    float isF = biomeIs(0.0);
    float isG = biomeIs(1.0);
    float isS = biomeIs(2.0);
    float isD = biomeIs(3.0);

    // ---------------- Water palette (slightly tinted per biome) ----------------
    vec3 deepWaterBase    = vec3(0.02, 0.08, 0.15);
    vec3 shallowWaterBase = vec3(0.05, 0.20, 0.28);

    vec3 deepWater =
        deepWaterBase
        + vec3(0.00, 0.00, 0.03) * isS   // colder
        + vec3(0.02, 0.01, -0.02) * isD; // warmer

    vec3 shallowWater =
        shallowWaterBase
        + vec3(0.02, 0.02, 0.04) * isS
        + vec3(0.03, 0.02, -0.02) * isD;

    // ---------------- Base palettes per island ----------------
    // Forest (deep greens)
    vec3 sand_F   = vec3(0.66, 0.60, 0.40);
    vec3 grass_F  = vec3(0.08, 0.34, 0.11);
    vec3 forest_F = vec3(0.04, 0.18, 0.07);
    vec3 rock_F   = vec3(0.36, 0.36, 0.40);
    vec3 snow_F   = vec3(0.94, 0.94, 0.97);

    // Grassland (yellower, drier)
    vec3 sand_G   = vec3(0.70, 0.63, 0.44);
    vec3 grass_G  = vec3(0.20, 0.44, 0.12);
    vec3 forest_G = vec3(0.10, 0.26, 0.10);
    vec3 rock_G   = vec3(0.40, 0.40, 0.44);
    vec3 snow_G   = vec3(0.94, 0.94, 0.97);

    // Snow (cold rock, bright snow)
    vec3 sand_S   = vec3(0.62, 0.63, 0.60);
    vec3 grass_S  = vec3(0.10, 0.20, 0.12);
    vec3 forest_S = vec3(0.06, 0.14, 0.10);
    vec3 rock_S   = vec3(0.30, 0.34, 0.42);
    vec3 snow_S   = vec3(0.97, 0.97, 0.99);

    // Desert (warm sand + warm rock)
    vec3 sand_D   = vec3(0.82, 0.73, 0.48);
    vec3 grass_D  = vec3(0.28, 0.30, 0.10); // scrub
    vec3 forest_D = vec3(0.18, 0.20, 0.07);
    vec3 rock_D   = vec3(0.52, 0.44, 0.32);
    vec3 snow_D   = vec3(0.94, 0.94, 0.97);

    vec3 sand   = sand_F   * isF + sand_G   * isG + sand_S   * isS + sand_D   * isD;
    vec3 grass  = grass_F  * isF + grass_G  * isG + grass_S  * isS + grass_D  * isD;
    vec3 forest = forest_F * isF + forest_G * isG + forest_S * isS + forest_D * isD;
    vec3 rock   = rock_F   * isF + rock_G   * isG + rock_S   * isS + rock_D   * isD;
    vec3 snow   = snow_F   * isF + snow_G   * isG + snow_S   * isS + snow_D   * isD;

    // ---------------- Height bands ----------------
    float waterBand = uSeaLevel;

    // Snow islands: lower snowline a lot (so it reads as snowy from far away)
    float beachBand = uSeaLevel + mix(0.25, 0.16, isS);
    float rockBand  = uSeaLevel + mix(5.0,  3.8,  isS);
    float snowBand  = uSeaLevel + mix(7.0,  4.9,  isS);

    // Desert: no snow
    snowBand = mix(snowBand, uSeaLevel + 999.0, isD);
    rockBand = mix(rockBand, uSeaLevel + 3.2, isD);

    // ---------------- NEW: per-island band shift (varies treeline/rockline per island) ----------------
    float islandShift = (hash(vec2(uIslandSeed, 12.34)) - 0.5) * 1.2; // ~[-0.6..+0.6]
    beachBand += islandShift * 0.05;
    rockBand  += islandShift * 0.9;
    snowBand  += islandShift * 0.9;

    // ---------------- Moisture usage per biome ----------------
    float wet_F = smoothstep(0.35, 0.68, m);
    float wet_G = smoothstep(0.55, 0.85, m);
    float wet_S = smoothstep(0.55, 0.85, m) * 0.25; // muted veg
    float wet_D = smoothstep(0.85, 0.98, m) * 0.18; // mostly dry

    float wet = wet_F * isF + wet_G * isG + wet_S * isS + wet_D * isD;

    // slope -> rock push
    float steepRock = smoothstep(0.30, 0.70, slope);

    // ---------------- Water ----------------
    if (h < waterBand - 0.30) return deepWater;
    if (h < waterBand)        return shallowWater;

    // ---------------- Beach ----------------
    if (h < beachBand)
    {
        // Snow island: icy shoreline tint
        vec3 icy = vec3(0.80, 0.84, 0.90);
        vec3 shore = mix(sand, icy, isS * 0.55);

        // Desert: warmer shore
        shore += isD * vec3(0.04, 0.02, 0.00);

        return shore;
    }

    // ---------------- Base land ----------------
    vec3 veg = mix(grass, forest, wet);
    vec3 land = mix(veg, rock, steepRock);

    // push higher elevations toward rock (works for all biomes)
    float highRock = smoothstep(rockBand, snowBand, h);
    land = mix(land, rock, highRock);

    // ---------------- FIX: snow cap ONLY on Snow islands ----------------
    if (isS > 0.5 && h > snowBand)
        land = snow;

    // ---------------- Biome signatures ----------------
    // FOREST: canopy patches (big dark blotches)
    if (isF > 0.5)
    {
        float canopy = fbm2f(fs_in.worldPos.xz * 0.03);
        float patchMask  = smoothstep(0.45, 0.72, canopy);
        land *= mix(1.0, 0.78, patchMask * 0.8);
        land += vec3(0.00, 0.02, 0.00) * patchMask;
    }

    // GRASSLAND: dry/yellow variation + subtle banding
    if (isG > 0.5)
    {
        float dry    = fbm2f(fs_in.worldPos.xz * 0.05);
        float band = sin(fs_in.worldPos.x * 0.08 + fs_in.worldPos.z * 0.04) * 0.5 + 0.5;
        float mixv = 0.55 * dry + 0.45 * band;
        land = mix(land, land + vec3(0.08, 0.06, 0.00), mixv * 0.35); // warmer/drier
    }

    // SNOW: “frosting” below snowline on slopes (only for snow islands)
    if (isS > 0.5)
    {
        float frosting = smoothstep(rockBand - 0.2, snowBand - 0.6, h) * smoothstep(0.15, 0.55, slope);
        land = mix(land, snow, frosting * 0.70);
    }

    // DESERT: dunes (directional stripes) + warm baked tint
    if (isD > 0.5)
    {
        float duneCoord = fs_in.worldPos.x * 0.06 + fs_in.worldPos.z * 0.10;
        float dunes = sin(duneCoord) * 0.5 + 0.5;
        dunes = smoothstep(0.35, 0.75, dunes);

        float flatness = 1.0 - steepRock;
        land = mix(land, land + vec3(0.10, 0.06, 0.00), dunes * flatness * 0.55);
        land += flatness * vec3(0.05, 0.03, 0.00);
    }

    // ---------------- NEW: per-island rock character (striated vs blotchy) ----------------
    // Only affects areas that are already "rocky" (steep or high)
    float rockStyle = hash(vec2(uIslandSeed, 99.1)); // 0..1
    float rockNoise = fbm2f(fs_in.worldPos.xz * mix(0.06, 0.12, rockStyle));
    float rockStreak = sin(fs_in.worldPos.x * 0.12 + fs_in.worldPos.z * 0.05) * 0.5 + 0.5;

    float rockMask = max(steepRock, highRock);
    land = mix(land, land * (0.92 + 0.18 * rockNoise), rockMask * 0.60);
    land = mix(land, land + vec3(0.03) * (rockStreak - 0.5), rockMask * mix(0.0, 0.35, rockStyle));

    return land;
}

void main()
{
    vec3 N = normalize(fs_in.normal);

    vec3 V = normalize(uViewPos - fs_in.worldPos);
    vec3 L = normalize(-uLightDir);

    float slope = 1.0 - clamp(N.y, 0.0, 1.0);
    vec3 baseCol = biomeColor(fs_in.height, fs_in.moisture, slope);

    // ------------------------------------------------------------
    // NEW: Per-island tint (subtle overall grading per island)
    // ------------------------------------------------------------
    float islandRand = hash(vec2(uIslandSeed, uIslandSeed * 0.37));
    vec3 tint = mix(vec3(0.96, 0.98, 1.02), vec3(1.04, 0.98, 0.96), islandRand);
    baseCol *= tint;

    // ------------------------------------------------------------
    // Biome-specific micro-variation (controls "texture feel")
    // ------------------------------------------------------------
    float isS = biomeIs(2.0);
    float isD = biomeIs(3.0);

    // add per-island offset so patterns don't repeat across islands
    vec2 islandUV = fs_in.worldPos.xz + vec2(uIslandSeed * 0.013, uIslandSeed * 0.017);

    float vDef  = hash(islandUV * 0.35);
    float vSnow = hash(islandUV * 0.18); // cleaner
    float vDes  = hash(islandUV * 0.70); // grainier

    float v = mix(vDef, vSnow, isS);
    v = mix(v, vDes, isD);

    float amp = 0.08;               // default
    amp = mix(amp, 0.03, isS);      // snow low noise
    amp = mix(amp, 0.14, isD);      // desert higher grain

    baseCol *= mix(1.0 - amp, 1.0 + amp, v);

    // ------------------------------------------------------------
    // Lighting (Blinn-Phong)
    // ------------------------------------------------------------
    vec3 ambient = uAmbientStrength * baseCol;

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * baseCol * uLightColor;

    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), uShininess);
    vec3 specular = uSpecStrength * spec * uLightColor;

    vec3 color = ambient + diffuse + specular;

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
