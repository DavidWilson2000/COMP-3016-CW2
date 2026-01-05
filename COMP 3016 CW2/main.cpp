#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include "RingSystem.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stbImage/stb_image.h"
#include "Camera.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "WorldTypes.h"
#include "Shader.h"
#include "AudioSystem.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#ifndef HAS_ASSIMP
#define HAS_ASSIMP 1
#endif

#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>
#include <glm/glm/gtc/constants.hpp>


// -------------------- ERROR / LOG HELPERS --------------------
static void LogInfo(const std::string& m) { std::cout << "[INFO] " << m << "\n"; }
static void LogWarn(const std::string& m) { std::cout << "[WARN] " << m << "\n"; }
static void LogError(const std::string& m) { std::cerr << "[ERROR] " << m << "\n"; }
static void GLFWErrorCallback(int error, const char* description)
{
    std::cerr << "[GLFW] error " << error << ": " << (description ? description : "(no description)") << "\n";
}

static bool FileExists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

static std::string GLErrorToString(GLenum e)
{
    switch (e)
    {
    case GL_NO_ERROR: return "GL_NO_ERROR";
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    default: return "GL_UNKNOWN_ERROR(" + std::to_string((int)e) + ")";
    }
}

// Call this occasionally, not every drawcall (marker-friendly + performance-safe).
static bool CheckGLErrorsThrottled(const char* where, float dt, float& accum, float intervalSec = 1.0f)
{
    accum += dt;
    if (accum < intervalSec) return true;
    accum = 0.0f;

    bool ok = true;
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
    {
        ok = false;
        LogWarn(std::string(where) + " -> " + GLErrorToString(err));
    }
    return ok;
}
static bool ValidateGLMesh(const GLMesh& m, const std::string& name)
{
    if (m.vao == 0 || m.vbo == 0 || m.ebo == 0)
    {
        LogWarn(name + " mesh has invalid GL buffers (vao/vbo/ebo = 0).");
        return false;
    }
    if (m.indexCount <= 0)
    {
        LogWarn(name + " mesh has no indices.");
        return false;
    }
    return true;
}

static void APIENTRY GLDebugCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar* message, const void* userParam)
{
    // ignore noisy notifications if you want:
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

    std::cerr << "[GL DEBUG] id=" << id
        << " type=" << type
        << " severity=" << severity
        << " msg=" << message << "\n";
}











struct PrintThrottle
{
    float accum = 0.0f;

    // returns true once every `intervalSec`
    bool Tick(float dt, float intervalSec)
    {
        accum += dt;
        if (accum >= intervalSec)
        {
            accum = 0.0f;
            return true;
        }
        return false;
    }
};

struct KeyLatch
{
    bool last = false;
    bool JustPressed(bool now)
    {
        bool jp = (now && !last);
        last = now;
        return jp;
    }
};





//  Day and night cycle
static glm::vec3 SunColor(float t)
{
    glm::vec3 night(0.10f, 0.10f, 0.30f);
    glm::vec3 sunrise(1.00f, 0.70f, 0.40f);
    glm::vec3 noon(1.00f, 1.00f, 0.95f);
    glm::vec3 sunset(1.00f, 0.60f, 0.30f);

    if (t < 0.25f) return glm::mix(night, sunrise, t / 0.25f);
    if (t < 0.50f) return glm::mix(sunrise, noon, (t - 0.25f) / 0.25f);
    if (t < 0.75f) return glm::mix(noon, sunset, (t - 0.50f) / 0.25f);
    return glm::mix(sunset, night, (t - 0.75f) / 0.25f);
}

struct TimeOfDaySystem
{
    float t01 = 0.25f;
    float speed = 0.05f;

    void Update(float dt)
    {
        t01 += speed * dt;
        if (t01 > 1.0f) t01 -= 1.0f;
    }

    glm::vec3 LightDir() const
    {
        float angle = t01 * glm::two_pi<float>();
        return glm::normalize(glm::vec3(cos(angle), sin(angle), sin(angle * 0.5f)));
    }

    glm::vec3 LightColor() const
    {
        return SunColor(t01);
    }
};

// Smooth night factor: 0 in day, 1 at full night
static float NightFactor(float t01)
{
    // fade in around sunset (0.78->0.88), fade out around sunrise (0.12->0.22)
    float dusk = glm::smoothstep(0.78f, 0.88f, t01);
    float dawn = 1.0f - glm::smoothstep(0.12f, 0.22f, t01);
    float nf = dusk * dawn;
    return glm::clamp(nf, 0.0f, 1.0f);
}

//  NOISE 

static float hash2D(int x, int z, int seed)
{
    int h = x * 374761393 + z * 668265263 + seed * 1442695041;
    h = (h ^ (h >> 13)) * 1274126177;
    h ^= (h >> 16);
    return (h & 0x00FFFFFF) / 16777215.0f;
}

static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static float valueNoise2D(float x, float z, int seed)
{
    int x0 = (int)floor(x);
    int z0 = (int)floor(z);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    float sx = smooth(x - x0);
    float sz = smooth(z - z0);

    float n00 = hash2D(x0, z0, seed);
    float n10 = hash2D(x1, z0, seed);
    float n01 = hash2D(x0, z1, seed);
    float n11 = hash2D(x1, z1, seed);

    return lerp(lerp(n00, n10, sx), lerp(n01, n11, sx), sz);
}

static float fbm(float x, float z, int seed, int octaves = 6, float lacunarity = 2.0f, float gain = 0.5f)
{
    float amp = 0.5f;
    float freq = 1.0f;
    float sum = 0.0f;

    for (int i = 0; i < octaves; i++)
    {
        sum += amp * valueNoise2D(x * freq, z * freq, seed + i * 31);
        freq *= lacunarity;
        amp *= gain;
    }
    return sum;
}

// Hard coded tree pallet

static GLuint CreateTreePaletteTexture_3x3()
{
    static const unsigned char TREE_PALETTE_RGBA[3 * 3 * 4] =
    {

        36,138,41,255,   1,2,1,255,     0,0,0,255,

        0,0,0,255,       0,0,0,255,     0,0,0,255,

        86,53,4,255,     1,0,0,255,     0,0,0,255
    };

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 3, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, TREE_PALETTE_RGBA);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}





// Skybox

class Skybox
{
public:
    void Build()
    {
        float skyVerts[] = {
            -1, -1, -1,  1, -1, -1,  1,  1, -1,  1,  1, -1, -1,  1, -1, -1, -1, -1,
            -1, -1,  1,  1, -1,  1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1, -1,  1,

            -1,  1,  1, -1,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  1, -1,  1,  1,
             1,  1,  1,  1,  1, -1,  1, -1, -1,  1, -1, -1,  1, -1,  1,  1,  1,  1,

            -1, -1, -1,  1, -1, -1,  1, -1,  1,  1, -1,  1, -1, -1,  1, -1, -1, -1,
            -1,  1, -1,  1,  1, -1,  1,  1,  1,  1,  1,  1, -1,  1,  1, -1,  1, -1
        };

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    void Draw(Shader& shader, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& sunDir, float time01)
    {
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);

        glm::mat4 skyView = glm::mat4(glm::mat3(view));

        shader.Use();
        shader.SetMat4("uView", glm::value_ptr(skyView));
        shader.SetMat4("uProj", glm::value_ptr(proj));
        shader.SetVec3("uSunDir", sunDir.x, sunDir.y, sunDir.z);
        shader.SetFloat("uTime01", time01);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    void Destroy()
    {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
    }

private:
    GLuint vao = 0, vbo = 0;
};

//  OBJ / Assimp Model


//  OBJ Model


static bool LoadOBJ_Minimal(const std::string& path,
    std::vector<ModelVertex>& outVerts,
    std::vector<unsigned int>& outIdx)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        LogError("Failed to open OBJ: " + path);

        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;

    struct Key { int v, vt, vn; };
    struct KeyHash {
        size_t operator()(Key const& k) const {
            return (size_t)k.v * 73856093u ^ (size_t)k.vt * 19349663u ^ (size_t)k.vn * 83492791u;
        }
    };
    struct KeyEq {
        bool operator()(Key const& a, Key const& b) const {
            return a.v == b.v && a.vt == b.vt && a.vn == b.vn;
        }
    };

    std::unordered_map<Key, unsigned int, KeyHash, KeyEq> remap;

    outVerts.clear();
    outIdx.clear();

    std::string line;
    while (std::getline(in, line))
    {
        if (line.size() < 2) continue;

        std::istringstream ss(line);
        std::string type;
        ss >> type;

        if (type == "v")
        {
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (type == "vn")
        {
            glm::vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(glm::normalize(n));
        }
        else if (type == "vt")
        {
            glm::vec2 t;
            ss >> t.x >> t.y;
            t.y = 1.0f - t.y;
            uvs.push_back(t);
        }
        else if (type == "f")
        {
            std::vector<Key> face;
            face.reserve(4);

            for (int i = 0; i < 4; i++)
            {
                std::string vtx;
                if (!(ss >> vtx)) break;

                int v = 0, vt = 0, vn = 0;

                size_t p1 = vtx.find('/');
                size_t p2 = (p1 == std::string::npos) ? std::string::npos : vtx.find('/', p1 + 1);

                if (p1 == std::string::npos)
                {
                    v = std::stoi(vtx);
                }
                else
                {
                    v = std::stoi(vtx.substr(0, p1));
                    if (p2 == std::string::npos)
                    {
                        std::string sVT = vtx.substr(p1 + 1);
                        if (!sVT.empty()) vt = std::stoi(sVT);
                    }
                    else
                    {
                        std::string sVT = vtx.substr(p1 + 1, p2 - (p1 + 1));
                        std::string sVN = vtx.substr(p2 + 1);
                        if (!sVT.empty()) vt = std::stoi(sVT);
                        if (!sVN.empty()) vn = std::stoi(sVN);
                    }
                }

                Key k;
                k.v = v - 1;
                k.vt = (vt != 0) ? (vt - 1) : -1;
                k.vn = (vn != 0) ? (vn - 1) : -1;
                face.push_back(k);
            }

            auto emit = [&](const Key& k) -> unsigned int
                {
                    auto it = remap.find(k);
                    if (it != remap.end()) return it->second;

                    ModelVertex mv{};
                    if (k.v < 0 || k.v >= (int)positions.size())
                    {
                        LogWarn("OBJ parse: position index out of range in " + path);
                        mv.pos = glm::vec3(0, 0, 0);
                    }
                    else
                    {
                        mv.pos = positions[k.v];
                    }

                    mv.normal = (k.vn >= 0 && k.vn < (int)normals.size()) ? normals.at(k.vn) : glm::vec3(0, 1, 0);
                    mv.uv = (k.vt >= 0 && k.vt < (int)uvs.size()) ? uvs.at(k.vt) : glm::vec2(0, 0);

                    unsigned int idx = (unsigned int)outVerts.size();
                    outVerts.push_back(mv);
                    remap.insert({ k, idx });
                    return idx;
                };

            if (face.size() == 3)
            {
                outIdx.push_back(emit(face[0]));
                outIdx.push_back(emit(face[1]));
                outIdx.push_back(emit(face[2]));
            }
            else if (face.size() == 4)
            {
                unsigned int i0 = emit(face[0]);
                unsigned int i1 = emit(face[1]);
                unsigned int i2 = emit(face[2]);
                unsigned int i3 = emit(face[3]);

                outIdx.push_back(i0); outIdx.push_back(i1); outIdx.push_back(i2);
                outIdx.push_back(i0); outIdx.push_back(i2); outIdx.push_back(i3);
            }
        }
    }

    std::cout << "Loaded OBJ: " << path << " verts=" << outVerts.size() << " idx=" << outIdx.size() << "\n";
    return !outVerts.empty() && !outIdx.empty();
}

#if HAS_ASSIMP
static bool LoadModel_Assimp_AllMeshesMerged(
    const std::string& path,
    std::vector<ModelVertex>& outVerts,
    std::vector<unsigned int>& outIdx)
{
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_OptimizeMeshes |
        aiProcess_OptimizeGraph |
        aiProcess_FlipUVs |
        aiProcess_PreTransformVertices   // IMPORTANT: bakes node transforms into vertices
    );

    if (!scene || !scene->HasMeshes())
    {
        std::cerr << "Assimp failed: " << importer.GetErrorString() << "\n";
        return false;
    }

    outVerts.clear();
    outIdx.clear();

    auto safeStoi = [&](const std::string& s, int& out) -> bool
        {
            try {
                size_t pos = 0;
                out = std::stoi(s, &pos);
                return pos == s.size();
            }
            catch (...) { return false; }
        };


    size_t totalVerts = 0;
    size_t totalIdx = 0;
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
    {
        aiMesh* m = scene->mMeshes[mi];
        totalVerts += m->mNumVertices;
        totalIdx += (size_t)m->mNumFaces * 3;
    }
    outVerts.reserve(totalVerts);
    outIdx.reserve(totalIdx);

    unsigned int baseVertex = 0;

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
    {
        aiMesh* m = scene->mMeshes[mi];
        if (!m) continue;

        // vertices
        for (unsigned int i = 0; i < m->mNumVertices; ++i)
        {
            ModelVertex v{};
            v.pos = glm::vec3(m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z);

            if (m->HasNormals())
                v.normal = glm::vec3(m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z);
            else
                v.normal = glm::vec3(0, 1, 0);

            if (m->HasTextureCoords(0))
                v.uv = glm::vec2(m->mTextureCoords[0][i].x, m->mTextureCoords[0][i].y);
            else
                v.uv = glm::vec2(0, 0);

            outVerts.push_back(v);
        }

        // indices
        for (unsigned int f = 0; f < m->mNumFaces; ++f)
        {
            const aiFace& face = m->mFaces[f];
            if (face.mNumIndices != 3) continue;

            outIdx.push_back(baseVertex + face.mIndices[0]);
            outIdx.push_back(baseVertex + face.mIndices[1]);
            outIdx.push_back(baseVertex + face.mIndices[2]);
        }

        baseVertex += m->mNumVertices;
    }

    std::cout << "Loaded Assimp model (ALL meshes): " << path
        << " meshes=" << scene->mNumMeshes
        << " verts=" << outVerts.size()
        << " idx=" << outIdx.size() << "\n";

    return !outVerts.empty() && !outIdx.empty();
}
#endif


static bool LoadModelAny_FirstMesh(
    const std::string& path,
    std::vector<ModelVertex>& outVerts,
    std::vector<unsigned int>& outIdx)
{
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".obj")
        return LoadOBJ_Minimal(path, outVerts, outIdx);

#if HAS_ASSIMP
    return LoadModel_Assimp_AllMeshesMerged(path, outVerts, outIdx);
#else

    std::cerr << "No Assimp: cannot load '" << path << "' (only .obj supported in this build)\n";
    return false;
#endif
}




//  Tree System 



//  Island 


static void BuildConeModel(GLModel& out, float height, float radius, int sides)
{
    std::vector<ModelVertex> v;
    std::vector<unsigned int> idx;

    // tip at +Y, base at 0
    ModelVertex tip{};
    tip.pos = glm::vec3(0, height, 0);
    tip.normal = glm::vec3(0, 1, 0);
    tip.uv = glm::vec2(0, 0);
    v.push_back(tip);

    // base ring
    for (int s = 0; s < sides; s++)
    {
        float a = (float)s / (float)sides * glm::two_pi<float>();
        float x = cos(a) * radius;
        float z = sin(a) * radius;

        ModelVertex mv{};
        mv.pos = glm::vec3(x, 0.0f, z);

        // approximate normals pointing outwards
        mv.normal = glm::normalize(glm::vec3(x, radius * 0.6f, z));
        mv.uv = glm::vec2((float)s / (float)sides, 1.0f);
        v.push_back(mv);
    }

    // side triangles
    for (int s = 0; s < sides; s++)
    {
        int i0 = 0; // tip
        int i1 = 1 + s;
        int i2 = 1 + ((s + 1) % sides);
        idx.push_back(i0);
        idx.push_back(i1);
        idx.push_back(i2);
    }

    out.Upload(v, idx);
}
static GLuint CreateFallbackTexture2D()
{
    // 2x2 bright magenta checker = obvious missing texture
    const unsigned char px[] = {
        255, 0, 255, 255,   0, 0, 0, 255,
        0, 0, 0, 255,       255, 0, 255, 255
    };

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static GLuint LoadTexture2D_Safe(const char* path, bool srgb = false)
{
    if (!FileExists(path))
    {
        LogWarn(std::string("Texture missing: ") + path + " -> using fallback texture.");
        return CreateFallbackTexture2D();
    }

    int w, h, n;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &n, 0);
    if (!data)
    {
        LogWarn(std::string("Texture failed to load: ") + path + " -> using fallback texture.");
        return CreateFallbackTexture2D();
    }

    GLenum format = (n == 4) ? GL_RGBA : GL_RGB;
    GLenum internalFormat = format;
    if (srgb)
        internalFormat = (format == GL_RGBA) ? GL_SRGB8_ALPHA8 : GL_SRGB8;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}



// App

class App
{
    RingSystem rings;
    int lastDisplayedScore = -1;
    float treeModelMinY = 0.0f;
    float treeModelMaxY = 1.0f;
    float treeTrunkMinY = 0.0f;
    glm::vec3 treePivotMS = glm::vec3(0.0f);
    int   waterLightIdx = -1;
    float waterLightDist = 1e30f;
    bool debugLH = false;
    PrintThrottle lhPrint;
    float glErrAccum = 0.0f;



public:
    bool Init()
    {

        glfwSetErrorCallback(GLFWErrorCallback);

        if (!glfwInit())
        {
            std::cerr << "Failed to init GLFW\n";
            return false;
        }




        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window = glfwCreateWindow(width, height, "Procedural Island", nullptr, nullptr);
        if (!window)
        {
            std::cerr << "Failed to create window\n";
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);

        width = fbw;
        height = fbh;

        glfwSetWindowUserPointer(window, this);

        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int w, int h) {
            glViewport(0, 0, w, h);
            auto* self = (App*)glfwGetWindowUserPointer(win);
            if (self) { self->width = w; self->height = h; }
            });

        glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
            auto* self = (App*)glfwGetWindowUserPointer(win);
            self->camera.ProcessMouse((float)xpos, (float)ypos);
            });

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK)
        {
            std::cerr << "GLEW init failed: " << glewGetErrorString(glewErr) << "\n";
            return false;
        }
        glGetError();


        if (GLEW_KHR_debug)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(GLDebugCallback, nullptr);
            LogInfo("KHR_debug enabled (GL debug callback active).");
        }
        else
        {
            LogWarn("KHR_debug not available; using glGetError throttling only.");
        }

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        terrainShader = std::make_unique<Shader>("shaders/basic.vert", "shaders/basic.frag");
        skyShader = std::make_unique<Shader>("shaders/sky.vert", "shaders/sky.frag");
        waterShader = std::make_unique<Shader>("shaders/water.vert", "shaders/water.frag");
        treeShader = std::make_unique<Shader>("shaders/tree.vert", "shaders/tree.frag");
        lighthouseShader = std::make_unique<Shader>("shaders/lighthouse.vert", "shaders/lighthouse.frag");
        beamShader = std::make_unique<Shader>("shaders/beam.vert", "shaders/beam.frag");
        ringShader = std::make_unique<Shader>("shaders/ring.vert", "shaders/ring.frag");
        hudShader = std::make_unique<Shader>("shaders/hud.vert", "shaders/hud.frag");


        bool okTerrain = RequireShader("terrainShader", terrainShader);
        bool okSky = RequireShader("skyShader", skyShader);
        bool okWater = RequireShader("waterShader", waterShader);
        bool okTree = RequireShader("treeShader", treeShader);
        bool okLighthouse = RequireShader("lighthouseShader", lighthouseShader);
        bool okBeam = RequireShader("beamShader", beamShader);
        bool okRing = RequireShader("ringShader", ringShader);
        bool okHud = RequireShader("hudShader", hudShader);

        // If critical shaders fail, stop early (prevents broken submission crashes)
        if (!okTerrain || !okSky || !okWater)
        {
            LogError("Critical shaders failed. Exiting Init().");
            return false;
        }

        // Fullscreen quad in NDC (covers whole screen)
        float quad[] =
        {
            // pos.x pos.y   uv.x uv.y
            -1.0f, -1.0f,    0.0f, 0.0f,
             1.0f, -1.0f,    1.0f, 0.0f,
             1.0f,  1.0f,    1.0f, 1.0f,

            -1.0f, -1.0f,    0.0f, 0.0f,
             1.0f,  1.0f,    1.0f, 1.0f,
            -1.0f,  1.0f,    0.0f, 1.0f
        };

        glGenVertexArrays(1, &hudVAO);
        glGenBuffers(1, &hudVBO);

        glBindVertexArray(hudVAO);
        glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);


        std::cout << "beamShader linkedOk=" << beamShader->linkedOk << " ID=" << beamShader->ID << "\n";



        sky.Build();

        rings.InitMesh();
        rings.SetPointsPerRing(10);
        rings.SetCollectRadius(2.5f);
        treePaletteTex = CreateTreePaletteTexture_3x3();
        if (!treePaletteTex)
            std::cerr << "Tree palette texture failed to create.\n";

        std::cout << "CWD = " << std::filesystem::current_path() << "\n";

        //  Load tree OBJ + compute pivot 
        {
            const char* treePath = "assets/models/tree/tree.obj";
            std::cout << "Trying: " << treePath << "\n";
            std::cout << "Exists? " << std::filesystem::exists(treePath) << "\n";

            std::vector<ModelVertex> tv;
            std::vector<unsigned int> ti;

            if (!LoadOBJ_Minimal(treePath, tv, ti))
            {
                std::cerr << "Tree OBJ failed to load: " << treePath << "\n";
                treeModelLoaded = false;
            }
            else
            {
                treeModel.Upload(tv, ti);
                treeModelLoaded = ValidateGLMesh(treeModel.mesh, "TreeModel");
            }

            treeModelMinY = 1e9f;
            treeModelMaxY = -1e9f;
            for (const auto& v : tv)
            {
                treeModelMinY = std::min(treeModelMinY, v.pos.y);
                treeModelMaxY = std::max(treeModelMaxY, v.pos.y);
            }

            float trunkMinY = treeModelMinY;
            float sliceTop = trunkMinY + (treeModelMaxY - treeModelMinY) * 0.03f;

            glm::vec3 baseSum(0.0f);
            int baseCount = 0;

            for (const auto& v : tv)
            {
                if (v.pos.y <= sliceTop)
                {
                    baseSum.x += v.pos.x;
                    baseSum.z += v.pos.z;
                    baseCount++;
                }
            }

            treePivotMS = glm::vec3(0.0f);
            if (baseCount > 0)
            {
                treePivotMS.x = baseSum.x / (float)baseCount;
                treePivotMS.z = baseSum.z / (float)baseCount;
            }
            treePivotMS.y = trunkMinY;

            treeTrunkMinY = trunkMinY;

            std::cout << "Tree minY=" << treeModelMinY
                << " trunkMinY=" << treeTrunkMinY
                << " pivotMS=(" << treePivotMS.x << "," << treePivotMS.y << "," << treePivotMS.z << ")\n";
        }

        // Load lighthouse OBJ
        {
            const char* path = "assets/models/lighthouse/lighthouse.obj";
            std::vector<ModelVertex> v;
            std::vector<unsigned int> i;

            std::cout << "Trying: " << path << "\n";
            std::cout << "Exists? " << std::filesystem::exists(path) << "\n";

            if (!LoadOBJ_Minimal(path, v, i))
            {
                std::cerr << "Lighthouse OBJ failed to load: " << path << "\n";
                lighthouseLoaded = false;
            }
            else
            {
                lighthouseModel.Upload(v, i);
                lighthouseLoaded = ValidateGLMesh(lighthouseModel.mesh, "LighthouseModel");
            }

            // Build beam cone model
            BuildConeModel(beamModel, 10.0f, 6.0f, 128);
            beamLoaded = ValidateGLMesh(beamModel.mesh, "BeamCone");


        }



        {
            houseModels.clear();
            housesLoaded = false;

            const std::vector<std::string> housePaths =
            {
                "assets/models/houses/houseA.glb",  // OBJ (your existing minimal loader)
                "assets/models/houses/houseB.glb",  // GLB (requires Assimp)
                "assets/models/houses/houseC.glb"   // FBX (requires Assimp)
            };

            for (const auto& p : housePaths)
            {
                std::cout << "Trying house: " << p << "\n";
                std::cout << "Exists? " << std::filesystem::exists(p) << "\n";

                std::vector<ModelVertex> v;
                std::vector<unsigned int> i;

                if (LoadModelAny_FirstMesh(p, v, i))
                {
                    GLModel m;
                    m.Upload(v, i);
                    if (!ValidateGLMesh(m.mesh, "HouseModel")) LogWarn("House uploaded but mesh invalid: " + p);
                    houseModels.push_back(std::move(m));
                }
                else
                {
                    std::cerr << "House failed to load: " << p << "\n";
                }
            }

            housesLoaded = !houseModels.empty();
            std::cout << "Houses loaded: " << (housesLoaded ? "YES" : "NO")
                << " count=" << houseModels.size()
                << " (HAS_ASSIMP=" << HAS_ASSIMP << ")\n";
        }

        if (!audio.Init())
            return false;



        // ---- Terrain textures ----
        texSand = LoadTexture2D_Safe("assets/textures/sand.png");
        texGrass = LoadTexture2D_Safe("assets/textures/grass.png");
        texRock = LoadTexture2D_Safe("assets/textures/rock.png");
        texSnow = LoadTexture2D_Safe("assets/textures/snow.png");
        texRing = LoadTexture2D_Safe("assets/textures/ring.png");
        texHelp = LoadTexture2D_Safe("assets/textures/help.png");

        if (!texHelp)
        {
            std::cerr << "Help overlay texture failed to load.\n";
        }



        if (!texSand || !texGrass || !texRock || !texSnow)
        {
            std::cerr << "One or more terrain textures failed to load.\n";
            useTextures = false; // fallback to procedural color
        }


        RebuildWorld(cfg.seed);
        tod.speed = cfg.timeSpeed;

        std::cout << "\nControls:\n"
            << "  WASD + Mouse: move/look\n"
            << "  R: regenerate island (new seed)\n"
            << "  F: toggle fog\n"
            << "  P: toggle wireframe\n"
            << "  O: toggle storm mode\n"
            << "  B: toggle Beam (visible cone)\n"
            << "  ESC: quit\n\n";


        return true;
    }

    void Run()
    {
        float lastFrame = (float)glfwGetTime();

        while (!glfwWindowShouldClose(window))
        {
            float now = (float)glfwGetTime();
            float dt = now - lastFrame;
            lastFrame = now;

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);

            fpsTimer += dt;
            frameCount++;

            HandleInteraction();

            Island* isl = NearestIsland(camera.pos.x, camera.pos.z);
            glm::vec3 groundN(0, 1, 0);
            if (isl)
            {
                float lx = camera.pos.x - isl->centerXZ.x;
                float lz = camera.pos.z - isl->centerXZ.y;
                groundN = isl->terrain.SampleNormalAtWorldXZ(lx, lz);
            }

            float slope = 1.0f - glm::clamp(groundN.y, 0.0f, 1.0f);
            float speedMul = glm::clamp(1.0f - slope * 0.6f, 0.4f, 1.0f);

            camera.ProcessKeyboard(window, dt, speedMul);
            tod.Update(dt);

            int got = rings.UpdateCollect(camera.pos);
            if (got > 0)
                audio.PlayOneShot("assets/sfx/ring_collect.wav");


            // update title when score changes (cheap “UI”)
            int score = rings.GetScore();
            if (score != lastDisplayedScore)
            {
                lastDisplayedScore = score;
                std::string title = "Procedural Island - Score: " + std::to_string(score) +
                    " (" + std::to_string(rings.GetCollected()) + "/" + std::to_string(rings.GetTotal()) + ")";
                glfwSetWindowTitle(window, title.c_str());
            }

            Render(now, dt);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    void Shutdown()
    {
        for (auto& isl : islands)
        {
            isl.trees.Destroy();
            isl.terrain.Destroy();
        }
        islands.clear();

        treeModel.Destroy();
        lighthouseModel.Destroy();
        water.Destroy();
        sky.Destroy();

        if (treePaletteTex) glDeleteTextures(1, &treePaletteTex);
        treePaletteTex = 0;

        ringShader.reset();
        terrainShader.reset();
        skyShader.reset();
        waterShader.reset();
        treeShader.reset();
        lighthouseShader.reset();

        if (texHelp) glDeleteTextures(1, &texHelp);
        texHelp = 0;

        if (hudVBO) glDeleteBuffers(1, &hudVBO);
        if (hudVAO) glDeleteVertexArrays(1, &hudVAO);
        hudVBO = hudVAO = 0;

        hudShader.reset();

        // ---- TEXTURE CLEANUP ----
        if (texSand)  glDeleteTextures(1, &texSand);
        if (texGrass) glDeleteTextures(1, &texGrass);
        if (texRock)  glDeleteTextures(1, &texRock);
        if (texSnow)  glDeleteTextures(1, &texSnow);
        if (texRing) glDeleteTextures(1, &texRing);
        texSand = texGrass = texRock = texSnow = texRing = 0;

        audio.Shutdown();
    }

private:
    GLFWwindow* window = nullptr;
    int width = 1280, height = 720;

    //Audio
    AudioSystem audio;


    // --- HUD overlay ---
    std::unique_ptr<Shader> hudShader;
    GLuint hudVAO = 0, hudVBO = 0;
    GLuint texHelp = 0;

    KeyLatch kHelp;
    bool showHelp = false;


    GLuint texSand = 0, texGrass = 0, texRock = 0, texSnow = 0; GLuint texRing = 0;
    float texTiling = 0.08f;
    bool useTextures = true;

    std::vector<Island> islands;
    WorldConfig cfg;

    Camera camera;
    TimeOfDaySystem tod;

    Water water;
    Skybox sky;

    std::unique_ptr<Shader> terrainShader, skyShader, waterShader, treeShader;
    std::unique_ptr<Shader> lighthouseShader, beamShader;
    std::unique_ptr<Shader> ringShader;

    bool RequireShader(const char* name, const std::unique_ptr<Shader>& s)
    {
        if (!s)
        {
            LogError(std::string(name) + " is null.");
            return false;
        }
        if (!s->linkedOk)
        {
            LogError(std::string(name) + " failed to link. Effects using it will be disabled.");
            return false;
        }
        return true;
    }


    GLModel treeModel;
    bool treeModelLoaded = false;

    GLModel lighthouseModel;
    bool lighthouseLoaded = false;

    GLModel beamModel;
    bool beamLoaded = false;

    std::vector<GLModel> houseModels;
    bool housesLoaded = false;

    GLuint treePaletteTex = 0;

    bool wireframe = false;

    KeyLatch kRegen, kFog, kWire, kStorm, kBeamDbg;
    bool forceBeamDebug = true;

    KeyLatch kBeamWire;
    bool forceBeamWire = false;


    float fpsTimer = 0.0f;
    int frameCount = 0;

private:
    Island* NearestIsland(float x, float z)
    {
        if (islands.empty()) return nullptr;

        glm::vec2 p(x, z);
        Island* best = &islands[0];
        float bestD2 = glm::dot(p - islands[0].centerXZ, p - islands[0].centerXZ);

        for (auto& isl : islands)
        {
            float d2 = glm::dot(p - isl.centerXZ, p - isl.centerXZ);
            if (d2 < bestD2)
            {
                bestD2 = d2;
                best = &isl;
            }
        }
        return best;
    }

    static IslandBiome PickIslandBiome(std::mt19937& rng)
    {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        float r = u(rng);

        if (r < 0.30f) return IslandBiome::Forest;
        if (r < 0.55f) return IslandBiome::Grassland;
        if (r < 0.70f) return IslandBiome::Snow;
        if (r < 0.85f) return IslandBiome::Desert;
        return IslandBiome::Village;
    }

    // Pick a coastline-ish position: near edge, not too steep, just above sea level.
    bool FindLighthouseSpot(const Terrain& t, glm::vec3& outLocalPos) const
    {
        const auto& v = t.Verts();
        if (v.empty()) return false;

        float half = t.HalfSize();
        float sea = t.seaLevel;

        glm::vec3 best(0.0f);
        float bestScore = -1e9f;
        int bestIdx = -1;

        for (int i = 0; i < (int)v.size(); i++)
        {
            const glm::vec3 p = v[i].pos;
            const glm::vec3 n = v[i].normal;


            if (p.y < sea + 0.10f) continue;
            if (p.y > sea + 2.20f) continue;

            float r = glm::length(glm::vec2(p.x, p.z));
            float edge01 = glm::clamp((r - half * 0.70f) / (half * 0.28f), 0.0f, 1.0f);

            float flat01 = glm::clamp((n.y - 0.75f) / (1.0f - 0.75f), 0.0f, 1.0f);

            float score = edge01 * 2.0f + flat01 * 1.5f;

            if (score > bestScore)
            {
                bestScore = score;
                best = p;
                bestIdx = i;
            }
        }

        if (bestIdx < 0) return false;

        outLocalPos = best;
        outLocalPos.y = t.SampleHeightAtWorldXZ(outLocalPos.x, outLocalPos.z);
        return true;

    }


    void RebuildWorld(int seed)
    {
        cfg.seed = seed;

        water.y = cfg.seaLevel + cfg.waveStrength * 0.6f + 0.10f;
        water.BuildFromWorldSize(cfg.oceanHalfSize, cfg.waterSpacing);
        rings.Reset();
        islands.clear();
        islands.resize(cfg.islandCount);

        std::mt19937 rng(cfg.seed);
        std::uniform_real_distribution<float> ang(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> rad(0.0f, cfg.islandSpawnRadius);
        std::uniform_real_distribution<float> chance01(0.0f, 1.0f);

        auto farEnough = [&](const glm::vec2& p, const std::vector<glm::vec2>& placed)
            {
                for (const auto& q : placed)
                {
                    glm::vec2 d = p - q;
                    if (glm::dot(d, d) < cfg.islandMinSpacing * cfg.islandMinSpacing)
                        return false;
                }
                return true;
            };

        std::vector<glm::vec2> placed;
        placed.reserve(cfg.islandCount);

        for (int i = 0; i < cfg.islandCount; i++)
        {
            glm::vec2 pos(0.0f);
            bool ok = false;

            for (int tries = 0; tries < 300; tries++)
            {
                float a = ang(rng);
                float r = rad(rng);
                pos = glm::vec2(cos(a), sin(a)) * r;

                if (farEnough(pos, placed))
                {
                    ok = true;
                    break;
                }
            }

            if (!ok)
            {
                float a = ang(rng);
                float r = rad(rng);
                pos = glm::vec2(cos(a), sin(a)) * r;
            }

            placed.push_back(pos);

            Island& isl = islands[i];
            isl.centerXZ = pos;
            isl.seed = cfg.seed + i * 9991;

            isl.biome = PickIslandBiome(rng);

            isl.terrain.seaLevel = cfg.seaLevel;
            isl.terrain.Build(cfg.terrainGrid, cfg.terrainSpacing, isl.seed, isl.biome);

            isl.model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, 0.0f, pos.y));

            // Spawn rings for this island
            int ringCount = 6;
            if (isl.biome == IslandBiome::Village) ringCount = 10;
            if (isl.biome == IslandBiome::Snow)    ringCount = 7;

            rings.SpawnForIsland(
                i,
                isl.centerXZ,
                isl.terrain.HalfSize(),
                ringCount,
                cfg.seed,
                // height sampler (local xz)
                [&](float lx, float lz) { return isl.terrain.SampleHeightAtWorldXZ(lx, lz); },
                // normal sampler (local xz)
                [&](float lx, float lz) { return isl.terrain.SampleNormalAtWorldXZ(lx, lz); }
            );


            // Trees
            bool spawnTrees = (isl.biome == IslandBiome::Forest) || (isl.biome == IslandBiome::Grassland);
            if (spawnTrees && treeModelLoaded)
            {
                isl.trees.InitForMesh(treeModel.mesh);
                glm::vec3 islandOffset(isl.centerXZ.x, 0.0f, isl.centerXZ.y);
                isl.trees.PlaceOnTerrain(isl.terrain, isl.seed + 555, islandOffset, treePivotMS);
                isl.trees.UploadInstances();
            }
            else
            {
                isl.trees.ClearInstances();
            }


            // -------------------- Village Houses --------------------
            isl.houses.clear();
            if (isl.biome == IslandBiome::Village && housesLoaded)
            {
                // Place a small village on the flatter mid-band area.
                std::uniform_real_distribution<float> chance01(0.0f, 1.0f);
                std::uniform_real_distribution<float> yawR(0.0f, glm::two_pi<float>());
                std::uniform_real_distribution<float> scaleR(2.0f, 3.0f);

                const int desiredHouses = 8;
                const int maxTries = desiredHouses * 30;
                const float minSpacing = 10.0f; // house-to-house spacing in world units

                auto tooClose = [&](const glm::vec3& wpos) -> bool
                    {
                        for (const auto& h : isl.houses)
                        {
                            glm::vec3 p = glm::vec3(h.model[3]);
                            glm::vec2 d = glm::vec2(wpos.x - p.x, wpos.z - p.z);
                            if (glm::dot(d, d) < minSpacing * minSpacing) return true;
                        }
                        return false;
                    };

                float half = isl.terrain.HalfSize();
                glm::vec3 worldOffset(isl.centerXZ.x, 0.0f, isl.centerXZ.y);

                std::uniform_real_distribution<float> pickXZ(-half * 0.55f, half * 0.55f);

                for (int tries = 0; tries < maxTries && (int)isl.houses.size() < desiredHouses; tries++)
                {
                    float lx = pickXZ(rng);
                    float lz = pickXZ(rng);

                    // Prefer mid-band plateau (same idea as Terrain flatten mask)
                    float r01 = glm::clamp(glm::length(glm::vec2(lx, lz)) / half, 0.0f, 1.0f);
                    if (r01 < 0.20f || r01 > 0.70f) continue;

                    float y = isl.terrain.SampleHeightAtWorldXZ(lx, lz);
                    glm::vec3 n = isl.terrain.SampleNormalAtWorldXZ(lx, lz);

                    if (n.y < 0.90f) continue; // too steep
                    if (y < cfg.seaLevel + 1.5f) continue; // avoid coast / low land

                    glm::vec3 posWS = glm::vec3(lx, y, lz) + worldOffset;
                    if (tooClose(posWS)) continue;

                    // Small chance to skip so villages vary per seed
                    if (chance01(rng) > 0.35f) continue;

                    float yaw = yawR(rng);
                    float s = scaleR(rng);

                    glm::mat4 T = glm::translate(glm::mat4(1.0f), posWS);
                    glm::mat4 R = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
                    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(s));

                    PlacedHouse ph;
                    ph.variant = (int)(rng() % (unsigned int)houseModels.size());

                    // ph.variant = (int)(rng() % (unsigned int)houseModels.size());
                    ph.model = T * R * S;

                    isl.houses.push_back(ph);
                }

                std::cout << "Village houses placed: " << isl.houses.size() << "\n";
            }

            // Lighthouse
            isl.hasLighthouse = false;
            if (lighthouseLoaded && chance01(rng) < cfg.lighthouseChancePerIsland)
            {
                glm::vec3 localSpot;
                if (FindLighthouseSpot(isl.terrain, localSpot))
                {
                    glm::vec3 worldOffset(isl.centerXZ.x, 0.0f, isl.centerXZ.y);
                    glm::vec3 posWS = localSpot + worldOffset;


                    glm::vec2 d = glm::normalize(glm::vec2(localSpot.x, localSpot.z));
                    float yaw = atan2(d.y, d.x) + glm::pi<float>(); // face outward

                    glm::mat4 T = glm::translate(glm::mat4(1.0f), posWS);
                    glm::mat4 R = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0, 1, 0));
                    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(cfg.lighthouseScale));

                    isl.lighthouseModel = T * R * S;
                    isl.lighthousePosWS = posWS;
                    isl.hasLighthouse = true;
                }
            }

            std::cout << "Island " << i << " biome: " << IslandBiomeName(isl.biome)
                << (isl.hasLighthouse ? " + Lighthouse" : "") << "\n";
        }

        std::cout << "World rebuilt. Seed=" << cfg.seed
            << " Islands=" << cfg.islandCount
            << " OceanHalfSize=" << cfg.oceanHalfSize << "\n";
    }

    void HandleInteraction()
    {
        if (kHelp.JustPressed(glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS))
        {
            showHelp = !showHelp;
            std::cout << "Help overlay: " << (showHelp ? "ON" : "OFF") << "\n";
            audio.PlayOneShot("assets/sfx/ui_click.wav");
        }

        if (kRegen.JustPressed(glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS))
        {
            cfg.seed = cfg.seed * 1664525 + 1013904223;
            RebuildWorld(cfg.seed);
            audio.PlayOneShot("assets/sfx/regen.wav");

        }
        static KeyLatch kLHDbg;
        if (kLHDbg.JustPressed(glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS))
        {
            debugLH = !debugLH;
            std::cout << "debugLH: " << (debugLH ? "ON" : "OFF") << "\n";
        }


        if (kFog.JustPressed(glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS))
        {
            cfg.fogEnabled = !cfg.fogEnabled;
            std::cout << "Fog: " << (cfg.fogEnabled ? "ON" : "OFF") << "\n";
            audio.PlayOneShot("assets/sfx/ui_click.wav");

        }

        if (kWire.JustPressed(glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS))
        {
            wireframe = !wireframe;
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << "\n";

        }
        if (kBeamWire.JustPressed(glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS))
        {
            forceBeamWire = !forceBeamWire;
            std::cout << "ForceBeamWire: " << (forceBeamWire ? "ON" : "OFF") << "\n";
        }

        if (kStorm.JustPressed(glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS))
        {
            cfg.stormMode = !cfg.stormMode;
            std::cout << "Storm mode: " << (cfg.stormMode ? "ON" : "OFF") << "\n";
            audio.PlayOneShot("assets/sfx/thunder_distant.wav");

        }

    }

    void Render(float timeSeconds, float dt)
    {
        glm::vec3 sunDir = tod.LightDir();
        glm::vec3 sunCol = tod.LightColor();

        float innerCos = glm::cos(glm::radians(12.0f));
        float outerCos = glm::cos(glm::radians(20.0f));

        float spin = timeSeconds * cfg.lighthouseBeamSpinSpeed;
        glm::vec3 flatDir = glm::normalize(glm::vec3(cos(spin), 0.0f, sin(spin)));

        const float tiltDeg = 18.0f;
        glm::vec3 beamDir = glm::normalize(glm::vec3(
            flatDir.x * cos(glm::radians(tiltDeg)),
            -sin(glm::radians(tiltDeg)),
            flatDir.z * cos(glm::radians(tiltDeg))
        ));
        float beamRange = cfg.lighthouseBeamLength * 8.0f;

        // ---- AUDIO LISTENER UPDATE ----
        audio.UpdateListener(camera.pos, camera.front, camera.up);
        audio.UpdateStormMix(dt, cfg.stormMode);


        float fogDensity = cfg.fogDensity * (cfg.stormMode ? cfg.stormFogMultiplier : 1.0f);
        float waveStrength = cfg.waveStrength * (cfg.stormMode ? cfg.stormWaveMultiplier : 1.0f);

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        if (fbw > 0 && fbh > 0)
        {
            width = fbw;
            height = fbh;
            glViewport(0, 0, fbw, fbh);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        glClearColor(cfg.fogColor.r, cfg.fogColor.g, cfg.fogColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = camera.ViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(60.f),
            (float)width / (float)height, 2.0f, 5000.f);
        glm::mat4 model(1.0f);

        sky.Draw(*skyShader, view, proj, sunDir, tod.t01);

        float night = NightFactor(tod.t01);

        float beamVis = 1.0f;
        float lightVis = 1.0f;

        // Lighthouse light color
        glm::vec3 lhCol(1.0f, 0.95f, 0.80f);


        glm::vec3 waterLhPosWS(0.0f, -99999.0f, 0.0f);
        float waterLhIntensity = 0.0f;

        int bestIdx = -1;
        float bestD = 1e30f;

        for (int i = 0; i < (int)islands.size(); i++)
        {
            auto& isl = islands[i];
            if (!isl.hasLighthouse) continue;

            glm::vec3 lhPosWS = isl.lighthousePosWS
                + glm::vec3(0.0f, cfg.lighthouseLanternHeight * cfg.lighthouseScale, 0.0f);

            float d = glm::length(lhPosWS - camera.pos);
            if (d < bestD)
            {
                bestD = d;
                bestIdx = i;
            }
        }

        // keep current lighthouse unless the new one is closer
        if (waterLightIdx == -1)
        {
            waterLightIdx = bestIdx;
            waterLightDist = bestD;
        }
        else
        {
            // if current became invalid, or new is 15% closer, switch
            if (waterLightIdx < 0 || waterLightIdx >= (int)islands.size() ||
                !islands[waterLightIdx].hasLighthouse ||
                (bestIdx != -1 && bestD < waterLightDist * 0.85f))
            {
                waterLightIdx = bestIdx;
                waterLightDist = bestD;
            }
        }

        // -------------------------
        // Aim helper (unchanged)
        // -------------------------
        auto AimMatrixFromDirY = [](const glm::vec3& dir) -> glm::mat4
            {
                glm::vec3 up = glm::normalize(dir);
                glm::vec3 ref(0.0f, 0.0f, 1.0f);
                if (fabs(glm::dot(up, ref)) > 0.98f) ref = glm::vec3(1.0f, 0.0f, 0.0f);

                glm::vec3 right = glm::normalize(glm::cross(ref, up));
                glm::vec3 fwd = glm::normalize(glm::cross(up, right));

                glm::mat4 R(1.0f);
                R[0] = glm::vec4(right, 0.0f);
                R[1] = glm::vec4(up, 0.0f);
                R[2] = glm::vec4(fwd, 0.0f);
                return R;
            };

        // ============================================================
        // 1) OPAQUE WORLD FIRST (terrain / houses / lighthouse)
        // ============================================================
        for (auto& isl : islands)
        {
            float islandBiomeId = (float)(int)isl.biome;

            glm::vec3 lhPosWS(0.0f, -99999.0f, 0.0f);
            float lhIntensity = 0.0f;

            if (isl.hasLighthouse)
            {
                lhPosWS = isl.lighthousePosWS
                    + glm::vec3(0.0f, cfg.lighthouseLanternHeight * cfg.lighthouseScale, 0.0f);

                lhIntensity = lightVis * cfg.lighthouseLightStrength;

                if (isl.hasLighthouse && debugLH && lhPrint.Tick(dt, 1.0f))
                {
                    float distToCam = glm::length(lhPosWS - camera.pos);
                    std::cout << "[LH] distToCam=" << distToCam
                        << " lhIntensity=" << lhIntensity
                        << " night=" << night
                        << "\n";
                }
            }

            // ---- TERRAIN ----
            terrainShader->Use();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texSand);
            terrainShader->SetInt("uTexSand", 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, texGrass);
            terrainShader->SetInt("uTexGrass", 1);

            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, texRock);
            terrainShader->SetInt("uTexRock", 2);

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, texSnow);
            terrainShader->SetInt("uTexSnow", 3);

            terrainShader->SetFloat("uTexTiling", texTiling);
            terrainShader->SetFloat("uUseTextures", useTextures ? 1.0f : 0.0f);

            isl.terrain.Draw(*terrainShader, isl.model, view, proj, camera,
                sunDir, sunCol,
                cfg.fogEnabled, cfg.fogColor, fogDensity,
                islandBiomeId,
                (float)isl.seed,
                lhPosWS, lhCol, lhIntensity,
                beamDir, innerCos, outerCos,
                beamRange);

            // ---- LIGHTHOUSE MODEL (OPAQUE) ----
            if (lighthouseLoaded && isl.hasLighthouse)
            {
                lighthouseShader->Use();
                lighthouseShader->SetMat4("uModel", glm::value_ptr(isl.lighthouseModel));
                lighthouseShader->SetMat4("uView", glm::value_ptr(view));
                lighthouseShader->SetMat4("uProj", glm::value_ptr(proj));

                lighthouseShader->SetVec3("uViewPos", camera.pos.x, camera.pos.y, camera.pos.z);
                lighthouseShader->SetVec3("uLightDir", sunDir.x, sunDir.y, sunDir.z);
                lighthouseShader->SetVec3("uLightColor", sunCol.x, sunCol.y, sunCol.z);

                lighthouseShader->SetFloat("uAmbientStrength", 0.22f);
                lighthouseShader->SetFloat("uSpecStrength", 0.35f);
                lighthouseShader->SetFloat("uShininess", 64.0f);

                lighthouseShader->SetFloat("uFogEnabled", cfg.fogEnabled ? 1.0f : 0.0f);
                lighthouseShader->SetVec3("uFogColor", cfg.fogColor.x, cfg.fogColor.y, cfg.fogColor.z);
                lighthouseShader->SetFloat("uFogDensity", fogDensity);

                lighthouseShader->SetFloat("uNightFactor", night);
                lighthouseShader->SetVec3("uLanternPosWS", lhPosWS.x, lhPosWS.y, lhPosWS.z);
                lighthouseShader->SetVec3("uLanternColor", lhCol.x, lhCol.y, lhCol.z);
                lighthouseShader->SetFloat("uLanternIntensity", lhIntensity);

                lighthouseModel.mesh.Bind();
                glDrawElements(GL_TRIANGLES, lighthouseModel.mesh.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }

            // ---- HOUSES (OPAQUE) ----
            if (housesLoaded && !isl.houses.empty())
            {
                GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
                glDisable(GL_CULL_FACE);

                Shader& hs = *lighthouseShader;
                hs.Use();
                hs.SetMat4("uView", glm::value_ptr(view));
                hs.SetMat4("uProj", glm::value_ptr(proj));
                hs.SetVec3("uViewPos", camera.pos.x, camera.pos.y, camera.pos.z);

                hs.SetVec3("uLightDir", sunDir.x, sunDir.y, sunDir.z);
                hs.SetVec3("uLightColor", sunCol.x, sunCol.y, sunCol.z);

                hs.SetFloat("uAmbientStrength", 0.22f);
                hs.SetFloat("uSpecStrength", 0.25f);
                hs.SetFloat("uShininess", 48.0f);

                hs.SetFloat("uFogEnabled", cfg.fogEnabled ? 1.0f : 0.0f);
                hs.SetVec3("uFogColor", cfg.fogColor.x, cfg.fogColor.y, cfg.fogColor.z);
                hs.SetFloat("uFogDensity", fogDensity);

                hs.SetFloat("uNightFactor", night);
                hs.SetVec3("uLanternPosWS", lhPosWS.x, lhPosWS.y, lhPosWS.z);
                hs.SetVec3("uLanternColor", lhCol.x, lhCol.y, lhCol.z);
                hs.SetFloat("uLanternIntensity", lhIntensity);

                for (const auto& h : isl.houses)
                {
                    int vi = (h.variant >= 0 && h.variant < (int)houseModels.size()) ? h.variant : 0;
                    hs.SetMat4("uModel", glm::value_ptr(h.model));

                    houseModels[vi].mesh.Bind();
                    glDrawElements(GL_TRIANGLES, houseModels[vi].mesh.indexCount, GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
                }

                if (wasCull) glEnable(GL_CULL_FACE);
                else glDisable(GL_CULL_FACE);
            }
        }

        waterShader->Use();
        waterShader->SetFloat("uAdditiveOnly", 0.0f);

        // BASE WATER 
        water.Draw(*waterShader, model, view, proj, camera, sunDir, sunCol,
            timeSeconds, waveStrength, cfg.waveSpeed,
            cfg.fogEnabled, cfg.fogColor, fogDensity,
            glm::vec3(0.0f, -99999.0f, 0.0f), lhCol, 0.0f,
            beamDir, innerCos, outerCos,
            beamRange);



        // ADD ALL LIGHTHOUSES CONTRIBUTION (water) - normalized + distance faded
        int lhCount = 0;
        for (auto& isl : islands) if (isl.hasLighthouse) lhCount++;

        if (lhCount > 0)
        {
            //  debug throttle (prints once per ~1 second) 
            static float waterDbgAccum = 0.0f;
            waterDbgAccum += dt;
            bool doDbg = (waterDbgAccum >= 1.0f);
            if (doDbg) waterDbgAccum = 0.0f;

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);

            // additive pass should not write depth
            glDepthMask(GL_FALSE);

            // IMPORTANT: EQUAL is too fragile for animated water; use LEQUAL
            glDepthFunc(GL_LEQUAL);

            waterShader->Use();
            waterShader->SetFloat("uAdditiveOnly", 1.0f);

            // kill sun lighting during additive passes
            waterShader->SetFloat("uAmbientStrength", 0.0f);
            waterShader->SetFloat("uSpecStrength", 0.0f);
            waterShader->SetVec3("uLightColor", 0.0f, 0.0f, 0.0f);


            float globalWaterLhMul = 1.25f;
            float perLightNorm = 1.0f;


            float fadeStart = 250.0f;
            float fadeEnd = 1500.0f;



            int printed = 0;

            for (int i = 0; i < (int)islands.size(); i++)
            {
                auto& isl = islands[i];
                if (!isl.hasLighthouse) continue;

                glm::vec3 lhPosWS = isl.lighthousePosWS
                    + glm::vec3(0.0f, cfg.lighthouseLanternHeight * cfg.lighthouseScale, 0.0f);

                float d = glm::length(lhPosWS - camera.pos);

                // 1 near, 0 far
                float fade = 1.0f - glm::smoothstep(fadeStart, fadeEnd, d);
                fade = glm::clamp(fade, 0.0f, 1.0f);

                float lhIntensity =
                    cfg.lighthouseLightStrength *
                    globalWaterLhMul *
                    fade;




                if (doDbg && printed == 0)
                {
                    std::cout
                        << "[WATER-LH] islandIndex=" << i
                        << " lhCount=" << lhCount
                        << " d=" << d
                        << " fade=" << fade
                        << " perLightNorm=" << perLightNorm
                        << " lhIntensity=" << lhIntensity
                        << " beamRange=" << beamRange
                        << "\n";
                    printed++;
                }

                water.Draw(*waterShader, model, view, proj, camera, sunDir, sunCol,
                    timeSeconds, waveStrength, cfg.waveSpeed,
                    cfg.fogEnabled, cfg.fogColor, fogDensity,
                    lhPosWS, lhCol, lhIntensity,
                    beamDir, innerCos, outerCos,
                    beamRange);
            }

            // Optional GL error check (prints only when an error occurs)
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                std::cout << "[WATER-LH] GL error after additive pass: " << err << "\n";
            }

            // restore state
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }



        // -------------------- BEAM DRAW (with debug wire toggle) --------------------
        if (beamLoaded && beamShader && beamShader->linkedOk)
        {
            // Beam should not “cut out” the scene
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);

            // Beam cones often need no culling (inside/outside viewing)
            GLboolean wasCull = glIsEnabled(GL_CULL_FACE);
            glDisable(GL_CULL_FACE);

            // Save polygon mode and set wire only for beam
            GLint prevMode[2];
            glGetIntegerv(GL_POLYGON_MODE, prevMode);
            glPolygonMode(GL_FRONT_AND_BACK, forceBeamWire ? GL_LINE : GL_FILL);

            for (auto& isl : islands)
            {
                if (!isl.hasLighthouse) continue;

                glm::vec3 lhPosWS = isl.lighthousePosWS
                    + glm::vec3(0.0f, cfg.lighthouseLanternHeight * cfg.lighthouseScale, 0.0f);


                float scaleY = (cfg.lighthouseBeamLength) / 10.0f;
                float scaleR = (cfg.lighthouseBeamRadius) / 6.0f;

                glm::mat4 T = glm::translate(glm::mat4(1.0f), lhPosWS);


                glm::mat4 R = AimMatrixFromDirY(beamDir);

                glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scaleR, scaleY, scaleR));
                glm::mat4 beamM = T * R * S;

                beamShader->Use();
                beamShader->SetMat4("uModel", glm::value_ptr(beamM));
                beamShader->SetMat4("uView", glm::value_ptr(view));
                beamShader->SetMat4("uProj", glm::value_ptr(proj));

                // REQUIRED uniforms (this is what fixes the black box)
                beamShader->SetVec3("uViewPos", camera.pos.x, camera.pos.y, camera.pos.z);
                beamShader->SetVec3("uBeamColor", lhCol.x, lhCol.y, lhCol.z);
                beamShader->SetFloat("uBeamStrength", cfg.lighthouseBeamStrength);

                // THIS is the debug toggle your shader uses
                beamShader->SetFloat("uDebugWire", forceBeamWire ? 1.0f : 0.0f);



                // Fog uniforms used by beam.frag
                beamShader->SetFloat("uFogEnabled", cfg.fogEnabled ? 1.0f : 0.0f);
                beamShader->SetVec3("uFogColor", cfg.fogColor.x, cfg.fogColor.y, cfg.fogColor.z);
                beamShader->SetFloat("uFogDensity", fogDensity);

                beamModel.mesh.Bind();
                glDrawElements(GL_TRIANGLES, beamModel.mesh.indexCount, GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }

            // Restore state
            glPolygonMode(GL_FRONT_AND_BACK, prevMode[0]);

            if (wasCull) glEnable(GL_CULL_FACE);
            else glDisable(GL_CULL_FACE);

            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }



        // ============================================================
        // 3) TRANSPARENT: RINGS, TREES, BEAM
        // ============================================================

        // ---- RINGS (textured) ----
        if (ringShader && ringShader->linkedOk && texRing)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_TRUE);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texRing);

            ringShader->Use();
            ringShader->SetInt("uRingTex", 0);

            rings.Draw(
                *ringShader,
                view, proj,
                camera,
                sunDir, sunCol,
                cfg.fogEnabled,
                cfg.fogColor,
                fogDensity,
                night
            );

            glBindTexture(GL_TEXTURE_2D, 0);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }


        // ---- TREES (instanced) ----
        for (auto& isl : islands)
        {
            glm::vec3 lhPosWS(0.0f, -99999.0f, 0.0f);
            float lhIntensity = 0.0f;

            if (isl.hasLighthouse)
            {
                lhPosWS = isl.lighthousePosWS
                    + glm::vec3(0.0f, cfg.lighthouseLanternHeight * cfg.lighthouseScale, 0.0f);
                lhIntensity = lightVis * cfg.lighthouseLightStrength;
            }

            if (treeModelLoaded)
            {
                treeShader->Use();

                treeShader->SetMat4("uView", glm::value_ptr(view));
                treeShader->SetMat4("uProj", glm::value_ptr(proj));
                treeShader->SetVec3("uViewPos", camera.pos.x, camera.pos.y, camera.pos.z);
                treeShader->SetVec3("uLightDir", sunDir.x, sunDir.y, sunDir.z);
                treeShader->SetVec3("uLightColor", sunCol.x, sunCol.y, sunCol.z);

                treeShader->SetFloat("uAmbientStrength", 0.25f);
                treeShader->SetFloat("uSpecStrength", 0.15f);
                treeShader->SetFloat("uShininess", 16.0f);

                treeShader->SetFloat("uFogEnabled", cfg.fogEnabled ? 1.0f : 0.0f);
                treeShader->SetVec3("uFogColor", cfg.fogColor.x, cfg.fogColor.y, cfg.fogColor.z);
                treeShader->SetFloat("uFogDensity", fogDensity);

                treeShader->SetFloat("uTime", timeSeconds);

                treeShader->SetFloat("uTreeMinY", treeTrunkMinY);
                treeShader->SetFloat("uTreeMaxY", treeModelMaxY);
                treeShader->SetFloat("uTrunkFrac", 0.35f);

                treeShader->SetVec3("uPointLightPos", lhPosWS.x, lhPosWS.y, lhPosWS.z);
                treeShader->SetVec3("uPointLightColor", lhCol.x, lhCol.y, lhCol.z);
                treeShader->SetFloat("uPointLightIntensity", lhIntensity);
                treeShader->SetVec3("uBeamDir", beamDir.x, beamDir.y, beamDir.z);
                treeShader->SetFloat("uBeamInnerCos", innerCos);
                treeShader->SetFloat("uBeamOuterCos", outerCos);


                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);


                glDisable(GL_BLEND);


                glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);

                isl.trees.DrawInstanced(treeModel.mesh.indexCount);

                glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

            }
        }

        // ---- HELP OVERLAY ----
        if (showHelp && hudShader && hudShader->linkedOk && texHelp)
        {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            hudShader->Use();
            hudShader->SetInt("uTex", 0);
            hudShader->SetFloat("uAlpha", 0.92f);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texHelp);

            glBindVertexArray(hudVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);

            glBindTexture(GL_TEXTURE_2D, 0);

            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }
        CheckGLErrorsThrottled("Render()", dt, glErrAccum, 1.0f);

    }

};

int main()
{
    App app;
    if (!app.Init())
        return -1;

    app.Run();
    app.Shutdown();
    return 0;
}