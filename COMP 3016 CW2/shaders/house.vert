#version 410 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV; // even if unused, keep layout consistent

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vPosWS;
out vec3 vNormalWS;

void main()
{
    vec4 ws = uModel * vec4(aPos, 1.0);
    vPosWS = ws.xyz;

    mat3 Nmat = mat3(transpose(inverse(uModel)));
    vNormalWS = normalize(Nmat * aNormal);

    gl_Position = uProj * uView * ws;
}
