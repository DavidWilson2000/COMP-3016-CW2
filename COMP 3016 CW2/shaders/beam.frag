#version 410 core
in vec3 vPosWS;
in vec3 vLocalPos;

out vec4 FragColor;

uniform vec3  uViewPos;
uniform vec3  uBeamColor;
uniform float uBeamStrength;
uniform float uDebugWire; // 1 = show cone no matter what

// Fog
uniform float uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

void main()
{
    float d = length(uViewPos - vPosWS);
    float fadeDist = 1.0 / (1.0 + 0.015 * d);

    // MUST match BuildConeModel(height=10, radius=6)
    const float CONE_H = 10.0;
    const float CONE_R = 6.0;

    // y01 = 0 at base (lantern), 1 at far end (tip)
    float y01 = clamp(vLocalPos.y / CONE_H, 0.0, 1.0);

    // Correct cone radius: wide at base, narrow at tip
    float maxR = (1.0 - y01) * CONE_R;
    float r = length(vLocalPos.xz);

    // Inside cone volume with soft rim
    float coneInside = 1.0 - smoothstep(maxR * 0.92, maxR, r);

  float ang = atan(vLocalPos.z, vLocalPos.x); // -pi..pi
float halfAngle = radians(10.0);

// Step 3: hard cut outside the beam slice
if (uDebugWire < 0.5)
{
    if (abs(ang) > halfAngle) discard;
}

// optional soft edge (you can keep this)
float wedge = 1.0 - smoothstep(halfAngle, halfAngle * 1.35, abs(ang));

    // Stronger core, softer edge
    float core = 1.0 - smoothstep(maxR * 0.05, maxR * 0.75, r);

    // Fade along beam length so it doesn’t look like a solid “block”
    float along = 1.0 - smoothstep(0.15, 1.0, y01);

    float mask = coneInside * wedge * core * along;

if (uDebugWire > 0.5) mask = 1.0;

    // Color + alpha
    vec3 col = uBeamColor * (uBeamStrength * fadeDist) * mask;

    // Optional fog
    if (uFogEnabled > 0.5)
    {
        float f = exp(-uFogDensity * d);
        f = clamp(f, 0.0, 1.0);
        col = mix(uFogColor, col, f);
    }

float a = (uDebugWire > 0.5) ? 1.0 : (0.55 * fadeDist * mask);

  if (uDebugWire < 0.5)
{
    if (a < 0.01) discard;
}

    FragColor = vec4(col, a);
}
