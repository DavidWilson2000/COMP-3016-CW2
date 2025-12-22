#version 410 core
in vec3 vDir;
out vec4 FragColor;

uniform vec3 uSunDir;   // direction light travels (FROM sun -> scene)
uniform float uTime01;  // 0..1 time of day

vec3 skyGradient(vec3 dir, float t01)
{
    dir = normalize(dir);

    // day & night colors
    vec3 dayTop = vec3(0.25, 0.55, 0.95);
    vec3 dayHorizon = vec3(0.80, 0.90, 1.00);

    vec3 nightTop = vec3(0.02, 0.03, 0.08);
    vec3 nightHorizon = vec3(0.05, 0.06, 0.10);

    // How "day" is it? based on sun height
    float sunHeight = clamp(-uSunDir.y, 0.0, 1.0); // sun is "up" when -uSunDir.y is >0
    float dayAmount = smoothstep(0.05, 0.35, sunHeight);

    float h = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0); // 0 bottom -> 1 top

    vec3 top = mix(nightTop, dayTop, dayAmount);
    vec3 horizon = mix(nightHorizon, dayHorizon, dayAmount);

    return mix(horizon, top, pow(h, 1.2));
}

void main()
{
    vec3 dir = normalize(vDir);

    // Base gradient
    vec3 col = skyGradient(dir, uTime01);

    // Sun disc and glow
    vec3 sunDir = normalize(-uSunDir); // direction from scene toward sun
    float sunDot = max(dot(dir, sunDir), 0.0);

    float sunDisk = smoothstep(0.9995, 1.0, sunDot);     // small bright core
    float sunGlow = smoothstep(0.98, 1.0, sunDot);       // larger glow

    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    col += sunGlow * sunColor * 0.35;
    col += sunDisk * sunColor * 2.5;

    FragColor = vec4(col, 1.0);
}
