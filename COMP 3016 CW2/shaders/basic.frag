#version 410 core

in vec3 vNormal;
out vec4 FragColor;

void main()
{
    vec3 n = normalize(vNormal);
    FragColor = vec4(n * 0.5 + 0.5, 1.0);
}
