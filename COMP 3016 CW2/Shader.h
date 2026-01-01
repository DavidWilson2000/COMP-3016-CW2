#pragma once
#include <string>
#include <GL/glew.h>

class Shader
{
public:
    GLuint ID = 0;
    bool linkedOk = false;

    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    ~Shader();

    void Use() const;

    void SetMat4(const std::string& name, const float* value) const;
    void SetVec3(const std::string& name, float x, float y, float z) const;
    void SetFloat(const std::string& name, float v) const;
    void SetInt(const std::string& name, int v) const;   // ✅ add this

private:
    std::string LoadFile(const std::string& path);
    GLuint Compile(GLenum type, const std::string& source);
};
