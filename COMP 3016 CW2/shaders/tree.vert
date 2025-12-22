#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

// instance matrix (mat4 = 4 vec4 attributes)
layout(location=2) in vec4 iM0;
layout(location=3) in vec4 iM1;
layout(location=4) in vec4 iM2;
layout(location=5) in vec4 iM3;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;

void main()
{
    mat4 instanceModel = mat4(iM0, iM1, iM2, iM3);

    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    mat3 normalMat = mat3(transpose(inverse(instanceModel)));
    vNormal = normalize(normalMat * aNormal);

    gl_Position = uProj * uView * worldPos;
}
