#version 410 core
layout (location=0) in vec3 aPos;

out vec3 vPosWS;
out vec3 vLocalPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    vLocalPos = aPos; // IMPORTANT: raw mesh position (cone space)
    vec4 w = uModel * vec4(aPos, 1.0);
    vPosWS = w.xyz;
    gl_Position = uProj * uView * w;
}