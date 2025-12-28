#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <memory>
#include <algorithm>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Shader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

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
    float waveStrength = 1.2f;   // controls vertical amplitude in shader
    float waveSpeed = 1.0f;       // controls time scaling in shader

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
};


enum class IslandBiome : int
{
    Forest = 0,
    Grassland = 1,
    Snow = 2,
    Desert = 3
};

static const char* IslandBiomeName(IslandBiome b)
{
    switch (b)
    {
    case IslandBiome::Forest: return "Forest";
    case IslandBiome::Grassland: return "Grassland";
    case IslandBiome::Snow: return "Snow";
    case IslandBiome::Desert: return "Desert";
    default: return "Unknown";
    }
}

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    float moisture = 0.0f; // supports biomes 
};

struct TreeVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
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


// Camera and controls 

class Camera
{
public:
    glm::vec3 pos{ 0.0f, 6.0f, 14.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    float yaw = -90.0f;
    float pitch = -20.0f;

    // takes a speed multiplier
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

// Day/Night system
 
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

 
// Noise helpers (value noise + fbm)
 
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

static float Dist2XZ(const glm::vec3& a, const glm::vec3& b)
{
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return dx * dx + dz * dz;
}


// Terrain (PCG island, normals, moisture attribute, maxHeight)

class Terrain
{
public:
    float seaLevel = 2.5f;
    float globalVerticalMul = 3.0f;

    const std::vector<Vertex>& Verts() const { return verts; }
    float MaxHeight() const { return maxHeight; }
    float Spacing() const { return spacing; }

    // sampling helpers for gameplay systems (slope, biome)
    glm::vec3 SampleNormalAtWorldXZ(float worldX, float worldZ) const
    {
        int idx = SampleIndex(worldX, worldZ);
        return verts[idx].normal;
    }

    float SampleHeightAtWorldXZ(float worldX, float worldZ) const
    {
        int idx = SampleIndex(worldX, worldZ);
        return verts[idx].pos.y;
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
        // Biome tuning knobs 
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
        }

        for (int z = 0; z <= gridSize; z++)
        {
            for (int x = 0; x <= gridSize; x++)
            {
                float wx = x * spacing - half;
                float wz = z * spacing - half;

               float dist = sqrt(wx * wx + wz * wz);

             
               float ax = fabs(wx);
               float az = fabs(wz);

               // 0 at center, 1 at square edge
               float t = glm::clamp(glm::max(ax, az) / half, 0.0f, 1.0f);

               //  Handles Falloff and slope from middle
               float mask = 1.0f - glm::smoothstep(0.0f, 1.0f, t);
               mask = pow(mask, 0.2f);   // 2.5 to 4.0

                //  Noise 
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

                // Lift the center a bit
                height += (4.2f + baseLift) * mask * globalVerticalMul;


                //  Apply mask so edges return to sea level
                float land = seaLevel + (height - seaLevel) * mask;

                //  Coast flatten / beach zone 
                float coastStart = 0.05f; 
                float coast = glm::smoothstep(coastStart, 1.0f, t);
                land = glm::mix(land, seaLevel, coast);

               
                float rim = glm::smoothstep(0.88f, 1.0f, t); 
                land = glm::mix(land, seaLevel, rim);

                // Moisture
                float m = fbm(wx * 0.035f, wz * 0.035f, seed + 7777);
                float altitude01 = glm::clamp((land - seaLevel) / 10.0f, 0.0f, 1.0f);
                m = glm::mix(m, m * 0.6f, altitude01);

                m *= moistureMul;
                m = glm::clamp(m, 0.0f, 1.0f);

                Vertex v;
                v.pos = glm::vec3(wx, land, wz);
                v.normal = glm::vec3(0, 1, 0);
                v.moisture = m;

                verts.push_back(v);
                maxHeight = std::max(maxHeight, land);
            }
        }

        // Indices
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
        float islandSeed)
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
        // Convert world XZ -> grid coords
        float half = gridSize * spacing * 0.5f;
        int gx = (int)round((worldX + half) / spacing);
        int gz = (int)round((worldZ + half) / spacing);

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
        float fogDensity)
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

static void BuildTreeMesh(std::vector<TreeVertex>& outVerts, std::vector<unsigned int>& outIdx)
{
    outVerts.clear();
    outIdx.clear();

    const int sides = 10;
    const float trunkH = 1.2f;
    const float trunkR = 0.12f;

    const float coneH = 1.8f;
    const float coneR = 0.7f;

    int baseStart = (int)outVerts.size();
    for (int i = 0; i < sides; i++)
    {
        float a = (float)i / (float)sides * glm::two_pi<float>();
        float x = cos(a) * trunkR;
        float z = sin(a) * trunkR;
        glm::vec3 n = glm::normalize(glm::vec3(x, 0.0f, z));

        outVerts.push_back({ glm::vec3(x, 0.0f, z), n });
        outVerts.push_back({ glm::vec3(x, trunkH, z), n });
    }

    for (int i = 0; i < sides; i++)
    {
        int i0 = baseStart + i * 2;
        int i1 = baseStart + i * 2 + 1;
        int i2 = baseStart + ((i + 1) % sides) * 2;
        int i3 = baseStart + ((i + 1) % sides) * 2 + 1;

        outIdx.push_back((unsigned int)i0);
        outIdx.push_back((unsigned int)i2);
        outIdx.push_back((unsigned int)i1);

        outIdx.push_back((unsigned int)i1);
        outIdx.push_back((unsigned int)i2);
        outIdx.push_back((unsigned int)i3);
    }

    int coneBaseStart = (int)outVerts.size();
    for (int i = 0; i < sides; i++)
    {
        float a = (float)i / (float)sides * glm::two_pi<float>();
        float x = cos(a) * coneR;
        float z = sin(a) * coneR;

        glm::vec3 sideN = glm::normalize(glm::vec3(x, coneR / coneH, z));
        outVerts.push_back({ glm::vec3(x, trunkH, z), sideN });
    }

    int tipIndex = (int)outVerts.size();
    outVerts.push_back({ glm::vec3(0.0f, trunkH + coneH, 0.0f), glm::vec3(0,1,0) });

    for (int i = 0; i < sides; i++)
    {
        int i0 = coneBaseStart + i;
        int i1 = coneBaseStart + ((i + 1) % sides);
        outIdx.push_back((unsigned int)i0);
        outIdx.push_back((unsigned int)i1);
        outIdx.push_back((unsigned int)tipIndex);
    }
}

class TreeSystem
{
public:
    void BuildMesh()
    {
        Destroy();

        BuildTreeMesh(treeVerts, treeIdx);

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glGenBuffers(1, &instanceVBO);

        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, treeVerts.size() * sizeof(TreeVertex), treeVerts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, treeIdx.size() * sizeof(unsigned int), treeIdx.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TreeVertex), (void*)offsetof(TreeVertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TreeVertex), (void*)offsetof(TreeVertex, normal));
        glEnableVertexAttribArray(1);

       
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

        std::size_t vec4Size = sizeof(glm::vec4);

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(0));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(vec4Size));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(2 * vec4Size));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(3 * vec4Size));

        glVertexAttribDivisor(2, 1);
        glVertexAttribDivisor(3, 1);
        glVertexAttribDivisor(4, 1);
        glVertexAttribDivisor(5, 1);

        glBindVertexArray(0);

        instances.clear();
    }

    void PlaceOnTerrain(const Terrain& terrain, int seed, const glm::vec3& worldOffset)
    {

        instances.clear();
        instances.reserve(2500);

        const auto& verts = terrain.Verts();
        float spacing = terrain.Spacing();

        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> pick(0, (int)verts.size() - 1);

        std::uniform_real_distribution<float> jitter(-spacing * 0.45f, spacing * 0.45f);
        std::uniform_real_distribution<float> rot(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> scaleR(0.8f, 1.5f);
        std::uniform_real_distribution<float> chance01(0.0f, 1.0f);

        const float minSpacing = 0.7f;
        const float minSpacing2 = minSpacing * minSpacing;

        const float slopeLimit = 0.80f;
        const float minMoisture = 0.45f;
        const float minHeight = terrain.seaLevel + 0.12f;
        const int desiredTrees = 800;
        const int maxTries = desiredTrees * 8;

        float cellSize = minSpacing;
        int gridW = (int)std::ceil((terrain.Spacing() * terrain.Spacing() * 0.0f) + 1.0f);
     
        float half = (terrain.Spacing() * (float)( /* gridSize */ 1)) * 0.0f;

        for (int tries = 0; tries < maxTries && (int)instances.size() < desiredTrees; tries++)
        {
            int idx = pick(rng);

            const glm::vec3 p = verts[idx].pos;
            const glm::vec3 n = verts[idx].normal;
            const float m = verts[idx].moisture;

            if (p.y < minHeight) continue;
            if (n.y < slopeLimit) continue;
            if (m < minMoisture) continue;

            float prob = glm::clamp((m - minMoisture) / (1.0f - minMoisture), 0.0f, 1.0f);
            prob = prob * prob;
            if (chance01(rng) > prob) continue;

            glm::vec3 candidate = p;
            candidate.x += jitter(rng);
            candidate.z += jitter(rng);

            // Move trees into world space for forest
            candidate += worldOffset;

            bool ok = true;
            for (const glm::mat4& M : instances)
            {
                glm::vec3 other = glm::vec3(M[3]);
                if (Dist2XZ(candidate, other) < minSpacing2)
                {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;

            float s = scaleR(rng);
            float r = rot(rng);

            glm::mat4 T = glm::translate(glm::mat4(1.0f), candidate);
            glm::mat4 R = glm::rotate(glm::mat4(1.0f), r, glm::vec3(0, 1, 0));
            glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(s));

            instances.push_back(T * R * S);
        }

        UploadInstances();
        std::cout << "Trees placed: " << instances.size() << "\n";
    }

    void ClearInstances()
    {
        instances.clear();
        UploadInstances(); // ensure VBO updates to empty
    }

    void Draw(Shader& shader,
        const glm::mat4& view,
        const glm::mat4& proj,
        const Camera& cam,
        const glm::vec3& lightDir,
        const glm::vec3& lightCol,
        bool fogEnabled,
        const glm::vec3& fogColor,
        float fogDensity,
        float timeSeconds) const
    {
        if (instances.empty()) return;

        shader.Use();
        shader.SetMat4("uView", glm::value_ptr(view));
        shader.SetMat4("uProj", glm::value_ptr(proj));
        shader.SetVec3("uViewPos", cam.pos.x, cam.pos.y, cam.pos.z);
        shader.SetVec3("uLightDir", lightDir.x, lightDir.y, lightDir.z);
        shader.SetVec3("uLightColor", lightCol.x, lightCol.y, lightCol.z);

        shader.SetFloat("uAmbientStrength", 0.25f);
        shader.SetFloat("uSpecStrength", 0.15f);
        shader.SetFloat("uShininess", 16.0f);

        shader.SetFloat("uFogEnabled", fogEnabled ? 1.0f : 0.0f);
        shader.SetVec3("uFogColor", fogColor.x, fogColor.y, fogColor.z);
        shader.SetFloat("uFogDensity", fogDensity);

        // Wind sway
        shader.SetFloat("uTime", timeSeconds);

        glBindVertexArray(vao);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)treeIdx.size(), GL_UNSIGNED_INT, 0, (GLsizei)instances.size());
        glBindVertexArray(0);
    }

    void Destroy()
    {
        if (instanceVBO) glDeleteBuffers(1, &instanceVBO);
        if (ebo) glDeleteBuffers(1, &ebo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = ebo = instanceVBO = 0;
        instances.clear();
    }

private:
    GLuint vao = 0, vbo = 0, ebo = 0, instanceVBO = 0;
    std::vector<TreeVertex> treeVerts;
    std::vector<unsigned int> treeIdx;
    std::vector<glm::mat4> instances;

    void UploadInstances()
    {
        glBindVertexArray(vao);

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER,
            instances.size() * sizeof(glm::mat4),
            instances.empty() ? nullptr : instances.data(),
            GL_DYNAMIC_DRAW);

        glBindVertexArray(0);
    }
};

struct Island
{
    Terrain terrain;
    TreeSystem trees;
    glm::mat4 model = glm::mat4(1.0f);
    glm::vec2 centerXZ = glm::vec2(0.0f);
    int seed = 0;

    IslandBiome biome = IslandBiome::Forest; // NEW
};

class App
{
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
            if (self)
            {
                self->width = w;
                self->height = h;
            }
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

        // Shaders
        terrainShader = std::make_unique<Shader>("shaders/basic.vert", "shaders/basic.frag");
        skyShader = std::make_unique<Shader>("shaders/sky.vert", "shaders/sky.frag");
        waterShader = std::make_unique<Shader>("shaders/water.vert", "shaders/water.frag");
        treeShader = std::make_unique<Shader>("shaders/tree.vert", "shaders/tree.frag");

        // Build static GPU meshes once
        sky.Build();

        // Build world (terrain + tree placement can change)
        RebuildWorld(cfg.seed);

        // Sync time speed from config
        tod.speed = cfg.timeSpeed;

        std::cout << "\nControls:\n"
            << "  WASD + Mouse: move/look\n"
            << "  R: regenerate island (new seed)\n"
            << "  F: toggle fog\n"
            << "  P: toggle wireframe\n"
            << "  O: toggle storm mode\n"
            << "  T/G: time speed +/-\n"
            << "  Y/H: wave strength +/-\n"
            << "  U/J: wave speed +/-\n"
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

            // FPS counter
            fpsTimer += dt;
            frameCount++;
            if (fpsTimer >= 1.0f)
            {
                std::cout << "FPS: " << frameCount << "\n";
                frameCount = 0;
                fpsTimer = 0.0f;
            }

            HandleInteraction();

            // Slope-based movement penalty
            Island* isl = NearestIsland(camera.pos.x, camera.pos.z);
            glm::vec3 groundN(0, 1, 0);
            if (isl)
            {
                // convert world position into island-local space:
                float lx = camera.pos.x - isl->centerXZ.x;
                float lz = camera.pos.z - isl->centerXZ.y;
                groundN = isl->terrain.SampleNormalAtWorldXZ(lx, lz);
            }

            float slope = 1.0f - glm::clamp(groundN.y, 0.0f, 1.0f);
            float speedMul = glm::clamp(1.0f - slope * 0.6f, 0.4f, 1.0f);

            camera.ProcessKeyboard(window, dt, speedMul);
            tod.Update(dt);

            UpdateObjective(now);
            UpdateBiome(now);

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

        water.Destroy();
        sky.Destroy();

        terrainShader.reset();
        skyShader.reset();
        waterShader.reset();
        treeShader.reset();

        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        window = nullptr;
    }

private:
    GLFWwindow* window = nullptr;
    int width = 1280, height = 720;

    std::vector<Island> islands;

    WorldConfig cfg;

    Camera camera;
    TimeOfDaySystem tod;

    Water water;
    Skybox sky;
    std::unique_ptr<Shader> terrainShader, skyShader, waterShader, treeShader;

    // Interaction toggles
    bool wireframe = false;

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

    // Latches
    KeyLatch kRegen, kFog, kWire, kStorm;
    KeyLatch kTimeUp, kTimeDown, kWaveUp, kWaveDown, kWaveSpeedUp, kWaveSpeedDown;

    // Objective
    float summitHeight = 0.0f;
    bool summitReached = false;
    float bestHeight = -1e9f;
    float lastHudPrint = 0.0f;

    // FPS
    float fpsTimer = 0.0f;
    int frameCount = 0;

    // Local “tile” biomes 
    enum class Biome { Beach, Grassland, Forest, Mountain };
    bool biomeInit = false;
    Biome lastBiome = Biome::Beach;
    float lastBiomePrint = 0.0f;

private:
    static IslandBiome PickIslandBiome(std::mt19937& rng)
    {
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        float r = u(rng);

        // tweak these odds however you want
        if (r < 0.35f) return IslandBiome::Forest;
        if (r < 0.60f) return IslandBiome::Grassland;
        if (r < 0.80f) return IslandBiome::Snow;
        return IslandBiome::Desert;
    }

    void RebuildWorld(int seed)
    {
        cfg.seed = seed;

        // Build a big ocean using oceanHalfSize
        water.y = cfg.seaLevel + cfg.waveStrength * 0.6f + 0.10f;
        water.BuildFromWorldSize(cfg.oceanHalfSize, cfg.waterSpacing);

        islands.clear();
        islands.resize(cfg.islandCount);

        std::mt19937 rng(cfg.seed);
        std::uniform_real_distribution<float> ang(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> rad(0.0f, cfg.islandSpawnRadius);

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

        summitHeight = -1e9f;
        summitReached = false;
        bestHeight = -1e9f;

        for (int i = 0; i < cfg.islandCount; i++)
        {
            glm::vec2 pos(0.0f);
            bool ok = false;

            // find a non-overlapping position
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

            // choose island-level biome
            isl.biome = PickIslandBiome(rng);

            // terrain
            isl.terrain.seaLevel = cfg.seaLevel;
            isl.terrain.Build(cfg.terrainGrid, cfg.terrainSpacing, isl.seed, isl.biome);

            // Island model transform
            isl.model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, 0.0f, pos.y));

            // trees
         
                isl.trees.BuildMesh();

            bool spawnTrees = (isl.biome == IslandBiome::Forest) || (isl.biome == IslandBiome::Grassland);
            if (spawnTrees)
            {
                glm::vec3 islandOffset(isl.centerXZ.x, 0.0f, isl.centerXZ.y);
                isl.trees.PlaceOnTerrain(isl.terrain, isl.seed + 555, islandOffset);
            }
            else
            {
                isl.trees.ClearInstances();
            }

            // update summit height
            summitHeight = std::max(summitHeight, (float)isl.terrain.MaxHeight());

            std::cout << "Island " << i << " biome: " << IslandBiomeName(isl.biome) << "\n";
        } //

        std::cout << "World rebuilt. Seed=" << cfg.seed
            << " Islands=" << cfg.islandCount
            << " OceanHalfSize=" << cfg.oceanHalfSize << "\n";
    } // 


    void HandleInteraction()
    {
        if (kRegen.JustPressed(glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS))
        {
            cfg.seed = cfg.seed * 1664525 + 1013904223;
            RebuildWorld(cfg.seed);
        }

        if (kFog.JustPressed(glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS))
        {
            cfg.fogEnabled = !cfg.fogEnabled;
            std::cout << "Fog: " << (cfg.fogEnabled ? "ON" : "OFF") << "\n";
        }

        if (kWire.JustPressed(glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS))
        {
            wireframe = !wireframe;
            glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
            std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << "\n";
        }

        // Storm mode toggle
        if (kStorm.JustPressed(glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS))
        {
            cfg.stormMode = !cfg.stormMode;
            std::cout << "Storm mode: " << (cfg.stormMode ? "ON" : "OFF") << "\n";
        }

        if (kTimeUp.JustPressed(glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS))
        {
            tod.speed = std::min(tod.speed + 0.01f, 0.30f);
            std::cout << "Time speed: " << tod.speed << "\n";
        }
        if (kTimeDown.JustPressed(glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS))
        {
            tod.speed = std::max(tod.speed - 0.01f, 0.0f);
            std::cout << "Time speed: " << tod.speed << "\n";
        }

        if (kWaveUp.JustPressed(glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS))
        {
            cfg.waveStrength = std::min(cfg.waveStrength + 0.05f, 2.0f);
            std::cout << "Wave strength: " << cfg.waveStrength << "\n";
        }
        if (kWaveDown.JustPressed(glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS))
        {
            cfg.waveStrength = std::max(cfg.waveStrength - 0.05f, 0.0f);
            std::cout << "Wave strength: " << cfg.waveStrength << "\n";
        }

        if (kWaveSpeedUp.JustPressed(glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS))
        {
            cfg.waveSpeed = std::min(cfg.waveSpeed + 0.2f, 5.0f);
            std::cout << "Wave speed: " << cfg.waveSpeed << "\n";
        }
        if (kWaveSpeedDown.JustPressed(glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS))
        {
            cfg.waveSpeed = std::max(cfg.waveSpeed - 0.2f, 0.0f);
            std::cout << "Wave speed: " << cfg.waveSpeed << "\n";
        }
    }

    void UpdateObjective(float now)
    {
        bestHeight = std::max(bestHeight, camera.pos.y);

        if (!summitReached && camera.pos.y >= summitHeight - 0.35f)
        {
            summitReached = true;
            std::cout << "\n*** SUMMIT REACHED! ***\n"
                << "Seed=" << cfg.seed << "  SummitY=" << summitHeight
                << "  TimeOfDay=" << tod.t01 << "\n\n";
        }

        if (now - lastHudPrint >= 1.0f)
        {
            lastHudPrint = now;
            std::cout << "Y=" << camera.pos.y
                << "  BestY=" << bestHeight
                << "  SummitY=" << summitHeight
                << "  Seed=" << cfg.seed
                << "  Fog=" << (cfg.fogEnabled ? "1" : "0")
                << "  Storm=" << (cfg.stormMode ? "1" : "0")
                << "\n";
        }
    }

    Biome ClassifyBiome(float h, float m) const
    {
        if (h < cfg.seaLevel + 0.30f) return Biome::Beach;

        if (h < cfg.seaLevel + 4.5f)
            return (m > 0.55f) ? Biome::Forest : Biome::Grassland;

        return Biome::Mountain;
    }

    void UpdateBiome(float now)
    {
        if (now - lastBiomePrint < 0.20f) return;
        lastBiomePrint = now;

        Island* isl = NearestIsland(camera.pos.x, camera.pos.z);
        if (!isl) return;

        // convert world to island-local coords
        float lx = camera.pos.x - isl->centerXZ.x;
        float lz = camera.pos.z - isl->centerXZ.y;

        float h = isl->terrain.SampleHeightAtWorldXZ(lx, lz);
        float m = isl->terrain.SampleMoistureAtWorldXZ(lx, lz);

        Biome b = ClassifyBiome(h, m);
        if (!biomeInit || b != lastBiome)
        {
            biomeInit = true;
            lastBiome = b;

            switch (b)
            {
            case Biome::Beach:     std::cout << "Biome: Beach\n"; break;
            case Biome::Grassland: std::cout << "Biome: Grassland\n"; break;
            case Biome::Forest:    std::cout << "Biome: Forest\n"; break;
            case Biome::Mountain:  std::cout << "Biome: Mountain\n"; break;
            }
        }
    }

    void Render(float timeSeconds)
    {
        glm::vec3 lightDir = tod.LightDir();
        glm::vec3 lightCol = tod.LightColor();

        // Storm affects fog & waves
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

        glDisable(GL_SCISSOR_TEST);


        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        glClearColor(cfg.fogColor.r, cfg.fogColor.g, cfg.fogColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


      


        glm::mat4 view = camera.ViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(60.f),
            (float)width / (float)height, 2.0f, 5000.f);

        glm::mat4 model(1.0f);

        sky.Draw(*skyShader, view, proj, lightDir, tod.t01);

        for (auto& isl : islands)
        {
            float islandBiomeId = (float)(int)isl.biome;

            isl.terrain.Draw(*terrainShader, isl.model, view, proj, camera,
                lightDir, lightCol, cfg.fogEnabled, cfg.fogColor, fogDensity,
                islandBiomeId,
                (float)isl.seed);

            GLenum err = glGetError();
            if (err != GL_NO_ERROR) std::cout << "GL error: " << err << "\n";


            // trees only appear on the island where they should
            isl.trees.Draw(*treeShader, view, proj, camera, lightDir, lightCol,
                cfg.fogEnabled, cfg.fogColor, fogDensity, timeSeconds);
        }
      

        water.Draw(*waterShader, model, view, proj, camera, lightDir, lightCol,
            timeSeconds, waveStrength, cfg.waveSpeed,
            cfg.fogEnabled, cfg.fogColor, fogDensity);
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
