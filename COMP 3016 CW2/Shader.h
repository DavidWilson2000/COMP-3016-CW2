#pragma once
#pragma once

#include <string>
#include <GL/glew.h>

class Shader
{
public:
    GLuint ID;

    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    void Use() const;
    void SetMat4(const std::string& name, const float* value) const;

private:
    std::string LoadFile(const std::string& path);
    GLuint Compile(GLenum type, const std::string& source);
};
