#version 410 core
in vec3 vPosWS;
in vec3 vLocalPos;

out vec4 FragColor;

uniform vec3  uViewPos;
uniform vec3  uBeamColor;
uniform float uBeamStrength;
uniform float uDebugWire; // 1 = debug cone (use GL_LINE in C++)

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

    // y01 = 0 at base, 1 at tip
    float y01 = clamp(vLocalPos.y / CONE_H, 0.0, 1.0);

    // wide at base -> narrow at tip
    float maxR = (1.0 - y01) * CONE_R;
    float r = length(vLocalPos.xz);

    // inside cone (soft rim)
    float coneInside = 1.0 - smoothstep(maxR * 0.92, maxR, r);

    // beam wedge (angle slice)
    float ang = atan(vLocalPos.z, vLocalPos.x); // -pi..pi
    float halfAngle = radians(10.0);

    // In debug mode: don't discard, show the whole cone (wire comes from GL_LINE)
    if (uDebugWire < 0.5)
    {
        if (abs(ang) > halfAngle) discard;
    }

    float wedge = 1.0 - smoothstep(halfAngle, halfAngle * 1.35, abs(ang));

    // stronger core, softer edge
    float core  = 1.0 - smoothstep(maxR * 0.05, maxR * 0.75, r);

    // fade along length (strong near base, weaker near tip)
    float along = 1.0 - smoothstep(0.15, 1.0, y01);

    float mask = coneInside * wedge * core * along;

    vec3 col = uBeamColor * (uBeamStrength * fadeDist) * mask;

    // fog
    if (uFogEnabled > 0.5)
    {
        float f = exp(-uFogDensity * d);
        f = clamp(f, 0.0, 1.0);
        col = mix(uFogColor, col, f);
    }

    // DEBUG: never make it opaque / never force mask=1
    // This prevents the giant “black box” occluding the world.
    if (uDebugWire > 0.5)
    {
        FragColor = vec4(uBeamColor, 0.35);
        return;
    }

    float a = 0.55 * fadeDist * mask;
    if (a < 0.01) discard;

    FragColor = vec4(col, a);
}
