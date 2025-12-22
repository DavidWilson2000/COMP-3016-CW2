#version 410 core
layout (location = 0) in vec3 aPos;

out vec3 vDir;

uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    // Remove translation from view so sky stays centered on camera
    mat4 viewNoTrans = mat4(mat3(uView));

    vec4 pos = uProj * viewNoTrans * vec4(aPos, 1.0);
    gl_Position = pos.xyww; // force depth to far plane
    vDir = aPos;
}
