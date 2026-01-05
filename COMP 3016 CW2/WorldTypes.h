#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <random>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>
#include <glm/glm/gtc/constants.hpp>
#include <iostream>
#include <cmath>
#include <cstddef>
#include <GL/glew.h>
#include "Shader.h"
#include "Camera.h"

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
    glm::vec2 uv;
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

struct PlacedHouse
{
    glm::mat4 model = glm::mat4(1.0f);
    int variant = -1; // which house model to use
};

struct ModelVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};
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




// Full version (implemented in main.cpp)
float fbm(float x, float z, int seed,
    int octaves, float lacunarity, float gain);

// 3-arg convenience overload so calls in Terrain compile
inline float fbm(float x, float z, int seed)
{
    return fbm(x, z, seed, 6, 2.0f, 0.5f);
}


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

                const float uvScale = 0.05f;
                v.uv = glm::vec2(wx, wz) * uvScale;

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

    void Draw(Shader & shader,
        const glm::mat4 & model,
        const glm::mat4 & view,
        const glm::mat4 & proj,
        const Camera & cam,
        const glm::vec3 & lightDir,
        const glm::vec3 & lightCol,
        bool fogEnabled,
        const glm::vec3 & fogColor,
        float fogDensity,
        float islandBiomeId,
        float islandSeed,

        const glm::vec3 & lhPosWS,
        const glm::vec3 & lhCol,
        float lhIntensity,
        const glm::vec3 & beamDirWS,
        float beamInnerCos,
        float beamOuterCos,
        float beamRange)
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
        shader.SetFloat("uBeamRange", beamRange);



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

        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
        glEnableVertexAttribArray(3);


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
        {
            for (int x = 0; x <= grid; x++)
            {
                Vertex vv;
                vv.pos = glm::vec3(x * spacing - half, y, z * spacing - half);
                vv.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                vv.moisture = 0.0f;
                vv.uv = glm::vec2((float)x, (float)z) * 0.05f; // harmless if water shader ignores
                verts.push_back(vv);
            }
        }


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
        float beamOuterCos,
        float beamRange)


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
        shader.SetFloat("uBeamRange", beamRange);

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
