#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::string Shader::LoadFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " << path << "\n";
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint Shader::Compile(GLenum type, const std::string& source)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char infoLog[2048];
        glGetShaderInfoLog(shader, 2048, nullptr, infoLog);
        std::cerr << "Shader compilation error:\n" << infoLog << "\n";

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
{
    std::string vertexCode = LoadFile(vertexPath);
    std::string fragmentCode = LoadFile(fragmentPath);

    if (vertexCode.empty() || fragmentCode.empty())
    {
        std::cerr << "Shader source empty, aborting program creation.\n";
        linkedOk = false;
        ID = 0;
        return;
    }

    GLuint vertex = Compile(GL_VERTEX_SHADER, vertexCode);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentCode);

    if (vertex == 0 || fragment == 0)
    {
        linkedOk = false;
        ID = 0;

        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return;
    }

    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);

    GLint success = 0;
    glGetProgramiv(ID, GL_LINK_STATUS, &success);
    if (!success)
    {
        char infoLog[2048];
        glGetProgramInfoLog(ID, 2048, nullptr, infoLog);
        std::cerr << "Shader linking error:\n" << infoLog << "\n";

        glDeleteProgram(ID);
        ID = 0;
        linkedOk = false;
    }
    else
    {
        linkedOk = true;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader::~Shader()
{
    if (ID) glDeleteProgram(ID);
}

void Shader::Use() const
{
    if (!linkedOk) return;
    glUseProgram(ID);
}

void Shader::SetMat4(const std::string& name, const float* value) const
{
    if (!linkedOk || ID == 0) return;
    GLint loc = glGetUniformLocation(ID, name.c_str());
    if (loc < 0) return;
    glUniformMatrix4fv(loc, 1, GL_FALSE, value);
}

void Shader::SetVec3(const std::string& name, float x, float y, float z) const
{
    if (!linkedOk || ID == 0) return;
    GLint loc = glGetUniformLocation(ID, name.c_str());
    if (loc < 0) return;
    glUniform3f(loc, x, y, z);
}

void Shader::SetFloat(const std::string& name, float v) const
{
    if (!linkedOk || ID == 0) return;
    GLint loc = glGetUniformLocation(ID, name.c_str());
    if (loc < 0) return;
    glUniform1f(loc, v);
}

void Shader::SetInt(const std::string& name, int v) const
{
    if (!linkedOk || ID == 0) return;
    GLint loc = glGetUniformLocation(ID, name.c_str());
    if (loc < 0) return;
    glUniform1i(loc, v);
}
