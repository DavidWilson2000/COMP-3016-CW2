#version 410 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aMoisture;
layout (location = 3) in vec2 aUV;       

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    float moisture;
    float height;
    vec2 uv;                               
} vs_out;

void main()
{
    vec4 wp = uModel * vec4(aPos, 1.0);
    vs_out.worldPos = wp.xyz;

    mat3 normalMat = transpose(inverse(mat3(uModel)));
    vs_out.normal = normalize(normalMat * aNormal);

    vs_out.moisture = aMoisture;

    // Height already baked in C++
    vs_out.height = aPos.y;

    // Pass UV through
    vs_out.uv = aUV;                      

    gl_Position = uProj * uView * wp;
}
