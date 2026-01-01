#version 410 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in mat4 iModel;

uniform mat4 uView;
uniform mat4 uProj;

uniform float uTreeMinY;
uniform float uTreeMaxY;

out vec3 vNormalWS;
out vec3 vPosWS;
out float vHeight01;

void main()
{
    vec4 worldPos = iModel * vec4(aPos, 1.0);
    vPosWS = worldPos.xyz;

    mat3 nrm = mat3(transpose(inverse(iModel)));
    vNormalWS = normalize(nrm * aNormal);

    vHeight01 = (aPos.y - uTreeMinY) / max(uTreeMaxY - uTreeMinY, 0.0001);

    gl_Position = uProj * uView * worldPos;
}
