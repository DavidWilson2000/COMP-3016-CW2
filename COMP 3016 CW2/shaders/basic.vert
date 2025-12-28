#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aMoisture;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    float moisture;
    float height;
} vs_out;

void main()
{
    vec4 wp = uModel * vec4(aPos, 1.0);

    vs_out.worldPos  = wp.xyz;

    // correct normal transform
    mat3 N = mat3(transpose(inverse(uModel)));
    vs_out.normal = normalize(N * aNormal);

    vs_out.moisture = aMoisture;

    // IMPORTANT: height comes from the vertex Y you already baked in C++
    vs_out.height = aPos.y;

    gl_Position = uProj * uView * wp;
}
