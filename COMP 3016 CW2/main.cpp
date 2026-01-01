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
 
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Shader.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#ifndef HAS_ASSIMP
#define HAS_ASSIMP 1
#endif

#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>
#include <glm/glm/gtc/constants.hpp>

#include <irrKlang/irrKlang.h>
#pragma comment(lib, "irrKlang.lib")
using namespace irrklang;



struct WorldConfig
{
    float oceanHalfSize = 600.0f;

    int islandCount = 7;
    float islandSpawnRadius = 420.0f;
    float islandMinSpacing = 160.0f;

    // Terrain
    int terrainGrid = 250;
    float terrainSpacing = 0.4f;
    float seaLevel = 2.5f;

    // Water
    float waterSpacing = 1.0f;
    float waveStrength = 1.2f;
    float waveSpeed = 1.0f;

    // Rendering / atmosphere
    bool fogEnabled = true;
    float fogDensity = 0.028f;
    glm::vec3 fogColor = glm::vec3(0.02f, 0.03f, 0.06f);

    // Day/Night Speed
    float timeSpeed = 0.05f;

    // PCG seed
    int seed = 1337;

    // Storm mode
    bool stormMode = false;
    float stormFogMultiplier = 2.5f;
    float stormWaveMultiplier = 1.8f;

    // Lighthouse placement / lighting
    float lighthouseChancePerIsland = 0.55f; // 0..1
    float lighthouseScale = 2.70f;
    float lighthouseLanternHeight = 10.0f;  
    float lighthouseLightStrength = 25.0f;    // brightness multiplier at full night

    // Lighthouse beam tuning
    float lighthouseBeamSpinSpeed = 0.35f;  // radians/sec
    float lighthouseBeamLength = 40.0f;  // used as a scale multiplier 
    float lighthouseBeamRadius = 6.0f;  // used as a scale multiplier 
    float lighthouseBeamStrength = 6.5f;  // brightness of the visible cone


};

enum class IslandBiome : int
{
    Forest = 0,
    Grassland = 1,
    Snow = 2,
    Desert = 3,
    Village = 4
};

static const char* IslandBiomeName(IslandBiome b)
{
    switch (b)
    {
    case IslandBiome::Forest: return "Forest";
    case IslandBiome::Grassland: return "Grassland";
    case IslandBiome::Snow: return "Snow";
    case IslandBiome::Desert: return "Desert";
    case IslandBiome::Village: return "Village";
    default: return "Unknown";
    }
}



struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    float moisture = 0.0f;
};

struct GLMesh
{
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0;
    GLenum indexType = GL_UNSIGNED_INT;

    void Destroy()
    {
        if (ebo) glDeleteBuffers(1, &ebo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = ebo = 0;
        indexCount = 0;
    }

    void Bind() const { glBindVertexArray(vao); }
};

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



class Camera
{
public:
    glm::vec3 pos{ 0.0f, 6.0f, 14.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    float yaw = -90.0f;
    float pitch = -20.0f;

    void ProcessKeyboard(GLFWwindow* window, float dt, float speedMul)
    {
        float speed = 10.0f * dt * speedMul;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) pos += speed * front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) pos -= speed * front;

        glm::vec3 right = glm::normalize(glm::cross(front, up));
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) pos -= speed * right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) pos += speed * right;
    }

    void ProcessMouse(float xpos, float ypos)
    {
        if (firstMouse)
        {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;

        lastX = xpos;
        lastY = ypos;

        float sensitivity = 0.1f;
        yaw += xoffset * sensitivity;
        pitch += yoffset * sensitivity;

        pitch = glm::clamp(pitch, -89.0f, 89.0f);

        glm::vec3 f;
        f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        f.y = sin(glm::radians(pitch));
        f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(f);
    }

    glm::mat4 ViewMatrix() const
    {
        return glm::lookAt(pos, pos + front, up);
    }

private:
    float lastX = 640.0f, lastY = 360.0f;
    bool firstMouse = true;
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

//  Terrain 

class Terrain
{
public:
    float seaLevel = 2.5f;
    float globalVerticalMul = 3.0f;

    float HalfSize() const { return gridSize * spacing * 0.5f; }

    const std::vector<Vertex>& Verts() const { return verts; }
    float MaxHeight() const { return maxHeight; }
    float Spacing() const { return spacing; }

    glm::vec3 SampleNormalAtWorldXZ(float worldX, float worldZ) const
    {
        int idx = SampleIndex(worldX, worldZ);
        return verts[idx].normal;
    }

    float SampleHeightAtWorldXZ(float worldX, float worldZ) const
    {
        float half = gridSize * spacing * 0.5f;

        float gx = (worldX + half) / spacing;
        float gz = (worldZ + half) / spacing;

        gx = glm::clamp(gx, 0.0f, (float)gridSize - 0.0001f);
        gz = glm::clamp(gz, 0.0f, (float)gridSize - 0.0001f);

        int x0 = (int)floor(gx);
        int z0 = (int)floor(gz);

        float tx = gx - x0;
        float tz = gz - z0;

        int row0 = z0 * (gridSize + 1);
        int row1 = (z0 + 1) * (gridSize + 1);

        const Vertex& v00 = verts[row0 + x0];
        const Vertex& v10 = verts[row0 + (x0 + 1)];
        const Vertex& v01 = verts[row1 + x0];
        const Vertex& v11 = verts[row1 + (x0 + 1)];

        float h = 0.0f;

        if (tx + tz <= 1.0f)
        {
            float w00 = 1.0f - tx - tz;
            float w01 = tz;
            float w10 = tx;
            h = w00 * v00.pos.y + w01 * v01.pos.y + w10 * v10.pos.y;
        }
        else
        {
            float w11 = tx + tz - 1.0f;
            float w10 = 1.0f - tz;
            float w01 = 1.0f - tx;
            h = w10 * v10.pos.y + w01 * v01.pos.y + w11 * v11.pos.y;
        }

        return h;
    }
     
    float SampleMoistureAtWorldXZ(float worldX, float worldZ) const
    {
        int idx = SampleIndex(worldX, worldZ);
        return verts[idx].moisture;
    }

    void Build(int gridSize, float spacing, int seed, IslandBiome islandBiome)
    {
        this->gridSize = gridSize;
        this->spacing = spacing;
        this->seed = seed;

        float half = gridSize * spacing * 0.5f;

        verts.clear();
        indices.clear();

        verts.reserve((gridSize + 1) * (gridSize + 1));
        indices.reserve(gridSize * gridSize * 6);

        maxHeight = -1e9f;

        float globalHeightScale = 0.65f;
        float heightMul = 1.0f;
        float ridgeMul = 1.0f;
        float moistureMul = 1.0f;
        float baseLift = 0.0f;

        switch (islandBiome)
        {
        case IslandBiome::Forest:
            moistureMul = 1.25f;
            break;
        case IslandBiome::Grassland:
            moistureMul = 1.05f;
            heightMul = 0.95f;
            break;
        case IslandBiome::Snow:
            heightMul = 1.35f;
            ridgeMul = 1.25f;
            moistureMul = 0.90f;
            baseLift = 0.2f;
            break;
        case IslandBiome::Desert:
            heightMul = 0.85f;
            ridgeMul = 0.60f;
            moistureMul = 0.40f;
            break;
        case IslandBiome::Village:
            // Flatter terrain with moderate moisture (good for grass + town)
            heightMul = 0.80f;
            ridgeMul = 0.55f;
            moistureMul = 0.95f;
            baseLift = 0.10f;
            break;
        }

        for (int z = 0; z <= gridSize; z++)
        {
            for (int x = 0; x <= gridSize; x++)
            {
                float wx = x * spacing - half;
                float wz = z * spacing - half;

                float ax = fabs(wx);
                float az = fabs(wz);

                float t = glm::clamp(glm::max(ax, az) / half, 0.0f, 1.0f);

                float mask = 1.0f - glm::smoothstep(0.0f, 1.0f, t);
                mask = pow(mask, 0.2f);

                float nBig = fbm(wx * 0.012f, wz * 0.012f, seed + 1000) * 2.0f - 1.0f;
                float nMid = fbm(wx * 0.045f, wz * 0.045f, seed + 2000) * 2.0f - 1.0f;
                float nSmall = fbm(wx * 0.160f, wz * 0.160f, seed + 3000) * 2.0f - 1.0f;

                float ridge = 1.0f - fabs(nMid);
                ridge = ridge * ridge;

                float height =
                    (nBig * 5.0f * heightMul) +
                    (nMid * 3.5f * heightMul) +
                    (ridge * 4.5f * ridgeMul) +
                    (nSmall * 0.9f * heightMul);

                height *= globalHeightScale * globalVerticalMul;
                height += (4.2f + baseLift) * mask * globalVerticalMul;

                float land = seaLevel + (height - seaLevel) * mask;

                float coastStart = 0.05f;
                float coast = glm::smoothstep(coastStart, 1.0f, t);
                land = glm::mix(land, seaLevel, coast);

                float rim = glm::smoothstep(0.88f, 1.0f, t);
                land = glm::mix(land, seaLevel, rim);

                float m = fbm(wx * 0.035f, wz * 0.035f, seed + 7777);
                float altitude01 = glm::clamp((land - seaLevel) / 10.0f, 0.0f, 1.0f);
                m = glm::mix(m, m * 0.6f, altitude01);

                m *= moistureMul;
                m = glm::clamp(m, 0.0f, 1.0f);

				// Village biome gets a flattened area in the center
                if (islandBiome == IslandBiome::Village)
                {
                    float r01 = glm::clamp(glm::length(glm::vec2(wx, wz)) / half, 0.0f, 1.0f);

                    float flatMask = 1.0f - glm::smoothstep(0.75f, 0.92f, r01);

                    float target = seaLevel + 2.2f;

                    // allow a tiny bit of variation
                    float micro = (fbm(wx * 0.08f, wz * 0.08f, seed + 4242) - 0.5f) * 0.25f;

                    land = glm::mix(land, target + micro, flatMask * 0.95f);
                }

                Vertex v;
                v.pos = glm::vec3(wx, land, wz);
                v.normal = glm::vec3(0, 1, 0);
                v.moisture = m;

                verts.push_back(v);
                maxHeight = std::max(maxHeight, land);
            }
        }

        for (int z = 0; z < gridSize; z++)
        {
            for (int x = 0; x < gridSize; x++)
            {
                int r1 = z * (gridSize + 1);
                int r2 = (z + 1) * (gridSize + 1);

                unsigned int i0 = (unsigned int)(r1 + x);
                unsigned int i1 = (unsigned int)(r2 + x);
                unsigned int i2 = (unsigned int)(r1 + x + 1);
                unsigned int i3 = (unsigned int)(r2 + x + 1);

                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
            }
        }

        ComputeNormals();
        Upload();
    }

    void Draw(Shader& shader,
        const glm::mat4& model,
        const glm::mat4& view,
        const glm::mat4& proj,
        const Camera& cam,
        const glm::vec3& lightDir,
        const glm::vec3& lightCol,
        bool fogEnabled,
        const glm::vec3& fogColor,
        float fogDensity,
        float islandBiomeId,
        float islandSeed,
       
        const glm::vec3& lhPosWS,
        const glm::vec3& lhCol,
        float lhIntensity,
        const glm::vec3& beamDirWS,
        float beamInnerCos,
        float beamOuterCos)
    {
        shader.Use();
        shader.SetMat4("uModel", glm::value_ptr(model));
        shader.SetMat4("uView", glm::value_ptr(view));
        shader.SetMat4("uProj", glm::value_ptr(proj));

        shader.SetVec3("uViewPos", cam.pos.x, cam.pos.y, cam.pos.z);
        shader.SetVec3("uLightDir", lightDir.x, lightDir.y, lightDir.z);
        shader.SetVec3("uLightColor", lightCol.x, lightCol.y, lightCol.z);

        shader.SetFloat("uAmbientStrength", 0.20f);
        shader.SetFloat("uSpecStrength", 0.35f);
        shader.SetFloat("uShininess", 32.0f);

        shader.SetFloat("uSeaLevel", seaLevel);

        shader.SetFloat("uFogEnabled", fogEnabled ? 1.0f : 0.0f);
        shader.SetVec3("uFogColor", fogColor.x, fogColor.y, fogColor.z);
        shader.SetFloat("uFogDensity", fogDensity);

        shader.SetFloat("uIslandBiome", islandBiomeId);
        shader.SetFloat("uIslandSeed", islandSeed);

        // lighthouse point light uniforms for terrain
        shader.SetVec3("uPointLightPos", lhPosWS.x, lhPosWS.y, lhPosWS.z);
        shader.SetVec3("uPointLightColor", lhCol.x, lhCol.y, lhCol.z);
        shader.SetFloat("uPointLightIntensity", lhIntensity);
        shader.SetVec3("uBeamDir", beamDirWS.x, beamDirWS.y, beamDirWS.z);
        shader.SetFloat("uBeamInnerCos", beamInnerCos);
        shader.SetFloat("uBeamOuterCos", beamOuterCos);


        mesh.Bind();
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void Destroy()
    {
        mesh.Destroy();
    }

private:
    int gridSize = 0;
    float spacing = 0.0f;
    int seed = 0;

    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;

    GLMesh mesh;
    float maxHeight = 0.0f;

    void ComputeNormals()
    {
        for (auto& v : verts) v.normal = glm::vec3(0);

        for (size_t i = 0; i < indices.size(); i += 3)
        {
            auto& a = verts[indices[i]];
            auto& b = verts[indices[i + 1]];
            auto& c = verts[indices[i + 2]];
            glm::vec3 n = glm::normalize(glm::cross(b.pos - a.pos, c.pos - a.pos));
            a.normal += n; b.normal += n; c.normal += n;
        }

        for (auto& v : verts) v.normal = glm::normalize(v.normal);
    }

    int SampleIndex(float worldX, float worldZ) const
    {
        float half = gridSize * spacing * 0.5f;
        int gx = (int)floor((worldX + half) / spacing);
        int gz = (int)floor((worldZ + half) / spacing);

        gx = glm::clamp(gx, 0, gridSize);
        gz = glm::clamp(gz, 0, gridSize);

        return gz * (gridSize + 1) + gx;
    }

    void Upload()
    {
        mesh.Destroy();

        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);

        glBindVertexArray(mesh.vao);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, moisture));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);

        mesh.indexCount = (GLsizei)indices.size();
    }
};

// Water

class Water
{
public:
    float y = 2.5f;

    void BuildFromWorldSize(float halfSize, float spacing)
    {
        int grid = (int)std::ceil((halfSize * 2.0f) / spacing);
        Build(grid, spacing);
    }

    void Build(int grid, float spacing)
    {
        std::vector<Vertex> verts;
        std::vector<unsigned int> idx;
        float half = grid * spacing * 0.5f;

        verts.reserve((grid + 1) * (grid + 1));
        idx.reserve(grid * grid * 6);

        for (int z = 0; z <= grid; z++)
            for (int x = 0; x <= grid; x++)
                verts.push_back({ {x * spacing - half, y, z * spacing - half}, {0,1,0}, 0.0f });

        for (int z = 0; z < grid; z++)
        {
            for (int x = 0; x < grid; x++)
            {
                int r1 = z * (grid + 1);
                int r2 = (z + 1) * (grid + 1);

                unsigned int i0 = (unsigned int)(r1 + x);
                unsigned int i1 = (unsigned int)(r2 + x);
                unsigned int i2 = (unsigned int)(r1 + x + 1);
                unsigned int i3 = (unsigned int)(r2 + x + 1);

                idx.push_back(i0); idx.push_back(i1); idx.push_back(i2);
                idx.push_back(i2); idx.push_back(i1); idx.push_back(i3);
            }
        }

        Upload(verts, idx);
    }

    void Draw(Shader& shader,
        const glm::mat4& model,
        const glm::mat4& view,
        const glm::mat4& proj,
        const Camera& cam,
        const glm::vec3& lightDir,
        const glm::vec3& lightCol,
        float timeSeconds,
        float waveStrength,
        float waveSpeed,
        bool fogEnabled,
        const glm::vec3& fogColor,
        float fogDensity,
        const glm::vec3& lhPosWS,
        const glm::vec3& lhCol,
        float lhIntensity,
        const glm::vec3& beamDirWS,
        float beamInnerCos,
        float beamOuterCos)

    {
        shader.Use();
        shader.SetMat4("uModel", glm::value_ptr(model));
        shader.SetMat4("uView", glm::value_ptr(view));
        shader.SetMat4("uProj", glm::value_ptr(proj));

        shader.SetFloat("uTime", timeSeconds);
        shader.SetFloat("uWaveStrength", waveStrength);
        shader.SetFloat("uWaveSpeed", waveSpeed);

        shader.SetVec3("uViewPos", cam.pos.x, cam.pos.y, cam.pos.z);
        shader.SetVec3("uLightDir", lightDir.x, lightDir.y, lightDir.z);
        shader.SetVec3("uLightColor", lightCol.x, lightCol.y, lightCol.z);

        shader.SetFloat("uAmbientStrength", 0.25f);
        shader.SetFloat("uSpecStrength", 0.6f);
        shader.SetFloat("uShininess", 128.0f);

        shader.SetFloat("uFogEnabled", fogEnabled ? 1.0f : 0.0f);
        shader.SetVec3("uFogColor", fogColor.x, fogColor.y, fogColor.z);
        shader.SetFloat("uFogDensity", fogDensity); 

       
        shader.SetVec3("uPointLightPos", lhPosWS.x, lhPosWS.y, lhPosWS.z);
        shader.SetVec3("uPointLightColor", lhCol.x, lhCol.y, lhCol.z);
        shader.SetFloat("uPointLightIntensity", lhIntensity);
        shader.SetVec3("uBeamDir", beamDirWS.x, beamDirWS.y, beamDirWS.z);
        shader.SetFloat("uBeamInnerCos", beamInnerCos);
        shader.SetFloat("uBeamOuterCos", beamOuterCos);


        mesh.Bind();
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    void Destroy()
    {
        mesh.Destroy();
    }

private:
    GLMesh mesh;

    void Upload(const std::vector<Vertex>& verts, const std::vector<unsigned int>& idx)
    {
        mesh.Destroy();

        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);

        glBindVertexArray(mesh.vao);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);

        mesh.indexCount = (GLsizei)idx.size();
    }
};

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

struct PlacedHouse
{
    glm::mat4 model = glm::mat4(1.0f);
    int variant = 0;
};

//  OBJ Model

struct ModelVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

static bool LoadOBJ_Minimal(const std::string& path,
    std::vector<ModelVertex>& outVerts,
    std::vector<unsigned int>& outIdx)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        std::cerr << "Failed to open OBJ: " << path << "\n";
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
                    mv.pos = positions.at(k.v);
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



struct GLModel
{
    GLMesh mesh;

    void Destroy() { mesh.Destroy(); }

    void Upload(const std::vector<ModelVertex>& verts, const std::vector<unsigned int>& idx)
    {
        mesh.Destroy();

        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);

        glBindVertexArray(mesh.vao);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ModelVertex), verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, pos));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, normal));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, uv));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);

        mesh.indexCount = (GLsizei)idx.size();
        mesh.indexType = GL_UNSIGNED_INT;
    }
};

//  Tree System 

class TreeSystem
{
public:
    void InitForMesh(const GLMesh& mesh)
    {
        if (vao == 0) glGenVertexArrays(1, &vao);
        if (instanceVBO == 0) glGenBuffers(1, &instanceVBO);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, normal));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)offsetof(ModelVertex, uv));

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

        std::size_t vec4Size = sizeof(glm::vec4);

        for (int i = 0; i < 4; i++)
        {
            glEnableVertexAttribArray(3 + i);
            glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * vec4Size));
            glVertexAttribDivisor(3 + i, 1);
        }

        glBindVertexArray(0);
    }

    void PlaceOnTerrain(const Terrain& terrain,
        int seed,
        const glm::vec3& worldOffset,
        const glm::vec3& pivotMS)
    {
        instances.clear();
        instances.reserve(2500);

        const auto& verts = terrain.Verts();
        float spacing = terrain.Spacing();

        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> pick(0, (int)verts.size() - 1);

        std::uniform_real_distribution<float> jitter(-spacing * 0.45f, spacing * 0.45f);
        std::uniform_real_distribution<float> rotY(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> scaleR(0.8f, 1.5f);
        std::uniform_real_distribution<float> chance01(0.0f, 1.0f);

        const float slopeLimit = 0.80f;
        const float minMoisture = 0.45f;
        const float minHeight = terrain.seaLevel + 0.12f;
        const int desiredTrees = 800;
        const int maxTries = desiredTrees * 8;

        const float TREE_SHRINK = 0.30f;

        for (int tries = 0; tries < maxTries && (int)instances.size() < desiredTrees; tries++)
        {
            int idx = pick(rng);

            glm::vec3 local = verts[idx].pos;

            local.x += jitter(rng);
            local.z += jitter(rng);

            float half = terrain.HalfSize();

            if (local.x < -half || local.x > half || local.z < -half || local.z > half)
                continue;

            local.y = terrain.SampleHeightAtWorldXZ(local.x, local.z);

            glm::vec3 n2 = terrain.SampleNormalAtWorldXZ(local.x, local.z);
            float m2 = terrain.SampleMoistureAtWorldXZ(local.x, local.z);

            if (local.y < minHeight) continue;
            if (n2.y < slopeLimit) continue;
            if (m2 < minMoisture) continue;

            float prob = glm::clamp((m2 - minMoisture) / (1.0f - minMoisture), 0.0f, 1.0f);
            prob *= prob;
            if (chance01(rng) > prob) continue;

            float s = scaleR(rng) * TREE_SHRINK;
            float r = rotY(rng);

            glm::vec3 world = local + worldOffset;

            glm::mat4 T = glm::translate(glm::mat4(1.0f), world);
            glm::mat4 Rm = glm::rotate(glm::mat4(1.0f), r, glm::vec3(0, 1, 0));
            glm::mat4 Sm = glm::scale(glm::mat4(1.0f), glm::vec3(s));
            glm::mat4 P = glm::translate(glm::mat4(1.0f), -pivotMS);

            instances.push_back(T * Rm * Sm * P);
        }

        std::cout << "Trees placed: " << instances.size() << "\n";
    }

    void UploadInstances()
    {
        if (instanceVBO == 0) return;

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER,
            instances.size() * sizeof(glm::mat4),
            instances.empty() ? nullptr : instances.data(),
            GL_DYNAMIC_DRAW);
    }

    void DrawInstanced(GLsizei indexCount) const
    {
        if (instances.empty() || vao == 0) return;

        glBindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, (GLsizei)instances.size());
        glBindVertexArray(0);
    }

    void ClearInstances()
    {
        instances.clear();
        UploadInstances();
    }

    void Destroy()
    {
        instances.clear();

        if (instanceVBO) glDeleteBuffers(1, &instanceVBO);
        instanceVBO = 0;

        if (vao) glDeleteVertexArrays(1, &vao);
        vao = 0;
    }

private:
    GLuint vao = 0;
    GLuint instanceVBO = 0;
    std::vector<glm::mat4> instances;
};

//  Island 

struct Island
{
    Terrain terrain;
    TreeSystem trees;
    glm::mat4 model = glm::mat4(1.0f);
    glm::vec2 centerXZ = glm::vec2(0.0f);
    int seed = 0;
    IslandBiome biome = IslandBiome::Forest;
    std::vector<PlacedHouse> houses;

    // Lighthouse (one per island max)
    bool hasLighthouse = false;
    glm::vec3 lighthousePosWS{ 0.0f };
    glm::mat4 lighthouseModel = glm::mat4(1.0f);
};

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


// App

class App
{
    float treeModelMinY = 0.0f;
    float treeModelMaxY = 1.0f;
    float treeTrunkMinY = 0.0f;
    glm::vec3 treePivotMS = glm::vec3(0.0f);
    int   waterLightIdx = -1;
    float waterLightDist = 1e30f;
    bool debugLH = false;          
    PrintThrottle lhPrint;



public:
    bool Init()
    {
   


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

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        terrainShader = std::make_unique<Shader>("shaders/basic.vert", "shaders/basic.frag");
        skyShader = std::make_unique<Shader>("shaders/sky.vert", "shaders/sky.frag");
        waterShader = std::make_unique<Shader>("shaders/water.vert", "shaders/water.frag");
        treeShader = std::make_unique<Shader>("shaders/tree.vert", "shaders/tree.frag");
        lighthouseShader = std::make_unique<Shader>("shaders/lighthouse.vert", "shaders/lighthouse.frag");
        beamShader = std::make_unique<Shader>("shaders/beam.vert", "shaders/beam.frag");
        std::cout << "beamShader linkedOk=" << beamShader->linkedOk << " ID=" << beamShader->ID << "\n";


        sky.Build();

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
                treeModelLoaded = true;
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
                lighthouseLoaded = true;
            }

            // Build beam cone model
            BuildConeModel(beamModel, 10.0f, 6.0f, 128);
            beamLoaded = true;


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

        audio = createIrrKlangDevice();
        if (!audio)
        {
            std::cerr << "Failed to start irrKlang.\n";
            return false;
        }

        // --- Ambient loops ---
        oceanLoop = audio->play2D("assets/sfx/ocean.wav", true, false, true); // loop, not paused, track handle
        if (oceanLoop) oceanLoop->setVolume(0.55f);

        // Storm loop starts silent (we fade it in when stormMode toggles)
        stormLoop = audio->play2D("assets/sfx/storm_wind.wav", true, false, true);
        if (stormLoop) stormLoop->setVolume(0.0f);


RebuildWorld(cfg.seed);
        tod.speed = cfg.timeSpeed;

        std::cout << "\nControls:\n"
            << "  WASD + Mouse: move/look\n"
            << "  R: regenerate island (new seed)\n"
            << "  F: toggle fog\n"
            << "  P: toggle wireframe\n"
            << "  O: toggle storm mode\n"
            << "  B: toggle ForceBeamDebug\n"
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

            Render(now);

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

        terrainShader.reset();
        skyShader.reset();
        waterShader.reset();
        treeShader.reset();
        lighthouseShader.reset();

        // ---- AUDIO CLEANUP ----
        for (auto& kv : lighthouseHums)
        {
            if (kv.second) { kv.second->stop(); kv.second->drop(); }
        }
        lighthouseHums.clear();

        if (oceanLoop) { oceanLoop->stop(); oceanLoop->drop(); oceanLoop = nullptr; }
        if (stormLoop) { stormLoop->stop(); stormLoop->drop(); stormLoop = nullptr; }

        if (audio) { audio->drop(); audio = nullptr; }


        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        window = nullptr;
    }

private:
    GLFWwindow* window = nullptr;
    int width = 1280, height = 720;

    // -------- AUDIO (irrKlang) --------
    ISoundEngine* audio = nullptr;

    ISound* oceanLoop = nullptr;
    ISound* stormLoop = nullptr;

    float stormMix = 0.0f; // 0 = calm, 1 = storm

    // looped 3D sounds per lighthouse island
    std::unordered_map<int, ISound*> lighthouseHums;


    std::vector<Island> islands;
    WorldConfig cfg;

    Camera camera;
    TimeOfDaySystem tod;

    Water water;
    Skybox sky;

    std::unique_ptr<Shader> terrainShader, skyShader, waterShader, treeShader;
    std::unique_ptr<Shader> lighthouseShader, beamShader;


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
    bool forceBeamWire = true;


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
        if (kRegen.JustPressed(glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS))
        {
            cfg.seed = cfg.seed * 1664525 + 1013904223;
            RebuildWorld(cfg.seed);
            if (audio) audio->play2D("assets/sfx/regen.wav", false);

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
            if (audio) audio->play2D("assets/sfx/ui_click.wav", false);

        }

        if (kWire.JustPressed(glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS))
        {
            wireframe = !wireframe;
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << "\n";
        }

        if (kStorm.JustPressed(glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS))
        {
            cfg.stormMode = !cfg.stormMode;
            std::cout << "Storm mode: " << (cfg.stormMode ? "ON" : "OFF") << "\n";
            if (audio) audio->play2D("assets/sfx/thunder_distant.wav", false);

        }
        if (kBeamDbg.JustPressed(glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS))
        {
            forceBeamDebug = !forceBeamDebug;
            std::cout << "ForceBeamDebug: " << (forceBeamDebug ? "ON" : "OFF") << "\n";
        }
        if (kBeamWire.JustPressed(glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS))
        {
            forceBeamWire = !forceBeamWire;
            std::cout << "ForceBeamWire: " << (forceBeamWire ? "ON" : "OFF") << "\n";

        }


    }

    void Render(float timeSeconds)
    {
        glm::vec3 sunDir = tod.LightDir();
        glm::vec3 sunCol = tod.LightColor();

        float innerCos = glm::cos(glm::radians(12.0f));
        float outerCos = glm::cos(glm::radians(20.0f));
        float spin = timeSeconds * cfg.lighthouseBeamSpinSpeed;
        // Horizontal spin direction
        glm::vec3 flatDir = glm::normalize(glm::vec3(cos(spin), 0.0f, sin(spin)));

        // Constant downward tilt (negative Y)
        const float tiltDeg = 18.0f;
        glm::vec3 beamDir = glm::normalize(glm::vec3(
            flatDir.x * cos(glm::radians(tiltDeg)),
            -sin(glm::radians(tiltDeg)),
            flatDir.z * cos(glm::radians(tiltDeg))
        ));

        // ---- AUDIO LISTENER UPDATE ----
        if (audio)
        {
            irrklang::vec3df pos(camera.pos.x, camera.pos.y, camera.pos.z);
            irrklang::vec3df look(camera.front.x, camera.front.y, camera.front.z);
            irrklang::vec3df up(camera.up.x, camera.up.y, camera.up.z);
            irrklang::vec3df vel(0, 0, 0); // optional doppler

            audio->setListenerPosition(pos, look, vel, up);
        }


        static float prevTime = 0.0f;
        float dt = timeSeconds - prevTime;
        prevTime = timeSeconds;
        if (dt < 0.0f) dt = 0.0f;

        float fogDensity = cfg.fogDensity * (cfg.stormMode ? cfg.stormFogMultiplier : 1.0f);
        float waveStrength = cfg.waveStrength * (cfg.stormMode ? cfg.stormWaveMultiplier : 1.0f);
        // ---- AUDIO STORM CROSSFADE ----
        float target = cfg.stormMode ? 1.0f : 0.0f;
        stormMix += (target - stormMix) * glm::clamp(dt * 1.5f, 0.0f, 1.0f);

        if (oceanLoop) oceanLoop->setVolume(0.55f * (1.0f - 0.35f * stormMix));
        if (stormLoop) stormLoop->setVolume(0.75f * stormMix);

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

       
   // Pick one lighthouse to affect the whole water 
  
        glm::vec3 waterLhPosWS(0.0f, -99999.0f, 0.0f);
        float waterLhIntensity = 0.0f;

        int bestIdx = -1;
        float bestD = 1e30f;

        // find closest lighthouse this frame
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

        // output uniforms from the chosen lighthouse
        if (waterLightIdx != -1 && islands[waterLightIdx].hasLighthouse)
        {
            glm::vec3 lhPosWS = islands[waterLightIdx].lighthousePosWS
                + glm::vec3(0.0f, cfg.lighthouseLanternHeight * cfg.lighthouseScale, 0.0f);

            waterLhPosWS = lhPosWS;
            waterLhIntensity = lightVis * cfg.lighthouseLightStrength;
        }

       



        auto AimMatrixFromDirY = [](const glm::vec3& dir) -> glm::mat4
            {
                glm::vec3 up = glm::normalize(dir);          
                glm::vec3 ref(0.0f, 0.0f, 1.0f);            

                // if ref is too close to up, pick another ref
                if (fabs(glm::dot(up, ref)) > 0.98f) ref = glm::vec3(1.0f, 0.0f, 0.0f);

                glm::vec3 right = glm::normalize(glm::cross(ref, up));
                glm::vec3 fwd = glm::normalize(glm::cross(up, right));

                glm::mat4 R(1.0f);
                // columns: right (X), up (Y), forward (Z)
                R[0] = glm::vec4(right, 0.0f);
                R[1] = glm::vec4(up, 0.0f);
                R[2] = glm::vec4(fwd, 0.0f);
                return R;
            };


        for (auto& isl : islands)
        {
            float islandBiomeId = (float)(int)isl.biome;

            // Per-island lighthouse light inputs 
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

            // Terrain and lighthouse interaction
            isl.terrain.Draw(*terrainShader, isl.model, view, proj, camera,
                sunDir, sunCol,
                cfg.fogEnabled, cfg.fogColor, fogDensity,
                islandBiomeId,
                (float)isl.seed,
                lhPosWS, lhCol, lhIntensity,
                beamDir, innerCos, outerCos);

            // Trees and lighthouse interaction
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

                isl.trees.DrawInstanced(treeModel.mesh.indexCount);
            }

            // Draw lighthouse model
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

            
        //  Beam Draw
                if (beamLoaded && beamShader && beamShader->linkedOk && (beamVis > 0.02f))

                {
                    const float CONE_H = 10.0f;  // MUST match BuildConeModel height
                    const float CONE_R = 6.0f;   // MUST match BuildConeModel radius
                    glm::vec3 dir = beamDir; 


                    glm::vec3 beamStartWS = lhPosWS + glm::vec3(0.0f, -10.5f, 0.0f);



                    float scaleY = cfg.lighthouseBeamLength / CONE_H;
                    float scaleXZ = cfg.lighthouseBeamRadius / CONE_R;
                   
                    glm::mat4 BM(1.0f);

                    // Translate to lantern
                    BM = glm::translate(BM, beamStartWS);

                 
                    BM = BM * AimMatrixFromDirY(dir);

                    // Scale cone after aiming
                    BM = BM * glm::scale(glm::mat4(1.0f), glm::vec3(scaleXZ, scaleY, scaleXZ));



                  

                    //  debug print 
                    static float beamDbgAccum = 0.0f;
                    beamDbgAccum += dt;
                    if (beamDbgAccum > 1.0f)
                    {
                        beamDbgAccum = 0.0f;
                        std::cout << "[BEAM] dbg=" << (forceBeamDebug ? 1 : 0)
                            << " night=" << night
                            << " tod=" << tod.t01
                            << " lhPos=(" << lhPosWS.x << "," << lhPosWS.y << "," << lhPosWS.z << ")"
                            << " dir=(" << dir.x << "," << dir.y << "," << dir.z << ")"
                            << " start=(" << beamStartWS.x << "," << beamStartWS.y << "," << beamStartWS.z << ")"
                            << " scaleXZ=" << scaleXZ << " scaleY=" << scaleY
                            << " vao=" << beamModel.mesh.vao
                            << " vbo=" << beamModel.mesh.vbo
                            << " ebo=" << beamModel.mesh.ebo
                            << " idx=" << beamModel.mesh.indexCount
                            << "\n";
                    }

                    glDisable(GL_CULL_FACE);
                    glDepthMask(GL_FALSE);
                    glEnable(GL_BLEND);

                    if (forceBeamWire)
                    {
                        // Always visible wireframe cone
                        glDisable(GL_DEPTH_TEST);          
                        glBlendFunc(GL_ONE, GL_ONE);      
                        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    }
                    else if (forceBeamDebug)
                    {
                        glDisable(GL_DEPTH_TEST);
                        glBlendFunc(GL_ONE, GL_ONE);
                        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    }
                    else
                    {
                        glEnable(GL_DEPTH_TEST);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                    }


                    beamShader->Use();
                    beamShader->SetMat4("uModel", glm::value_ptr(BM));
                    beamShader->SetMat4("uView", glm::value_ptr(view));
                    beamShader->SetMat4("uProj", glm::value_ptr(proj));
                    beamShader->SetVec3("uViewPos", camera.pos.x, camera.pos.y, camera.pos.z);
                    beamShader->SetFloat("uDebugWire", forceBeamWire ? 1.0f : 0.0f);

                    if (forceBeamWire)
                    {
                        beamShader->SetVec3("uBeamColor", 0.0f, 1.0f, 0.0f); // bright green to make it easy to see
                        beamShader->SetFloat("uBeamStrength", 50.0f);
                        beamShader->SetFloat("uFogEnabled", 0.0f);
                    }
                    else if (forceBeamDebug)
                    {
                        beamShader->SetVec3("uBeamColor", 1.0f, 1.0f, 1.0f);
                        beamShader->SetFloat("uBeamStrength", 10.0f);
                        beamShader->SetFloat("uFogEnabled", 0.0f);
                    }
                    else
                    {
                        beamShader->SetVec3("uBeamColor", lhCol.x, lhCol.y, lhCol.z);
                        beamShader->SetFloat("uBeamStrength", cfg.lighthouseBeamStrength * beamVis);
                        beamShader->SetFloat("uFogEnabled", cfg.fogEnabled ? 1.0f : 0.0f);
                    }


                    beamShader->SetVec3("uFogColor", cfg.fogColor.x, cfg.fogColor.y, cfg.fogColor.z);
                    beamShader->SetFloat("uFogDensity", fogDensity);

                    beamModel.mesh.Bind();
                    glDrawElements(GL_TRIANGLES, beamModel.mesh.indexCount, GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);

                    //  Restore 
                    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
                    glDisable(GL_BLEND);
                    glDepthMask(GL_TRUE);
                    glEnable(GL_DEPTH_TEST);
                }



            }

            // -------------------- Draw village houses --------------------
            if (housesLoaded && !isl.houses.empty())
            {
                glDisable(GL_CULL_FACE); // <--- TEST: fixes missing walls if it's culling
                // glEnable(GL_CULL_FACE); glCullFace(GL_BACK); // restore after if you want

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

                // If your lighthouse.frag expects these, keep them valid:
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

                glEnable(GL_CULL_FACE); // restore if your project uses it elsewhere
            
            
            }


        }
         
        water.Draw(*waterShader, model, view, proj, camera, sunDir, sunCol,
            timeSeconds, waveStrength, cfg.waveSpeed,
            cfg.fogEnabled, cfg.fogColor, fogDensity,
            waterLhPosWS, lhCol, waterLhIntensity,
            beamDir, innerCos, outerCos);

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
