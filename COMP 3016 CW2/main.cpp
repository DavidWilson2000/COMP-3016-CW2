#include <iostream>
#include <vector>
#include <cmath>
#include <random>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "Shader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

//  Vertex formats 
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

struct TreeVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

//  Utility: safe delete helpers 
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

//  Camera 
class Camera
{
public:
    glm::vec3 pos{ 0.0f, 6.0f, 14.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    float yaw = -90.0f;
    float pitch = -20.0f;

    void ProcessKeyboard(GLFWwindow* window, float dt)
    {
        float speed = 10.0f * dt;

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

//  Day/Night 
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

//  Noise
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

//  Terrain 
class Terrain
{
public:
    float seaLevel = 2.5f;

    // useful for tree placement
    const std::vector<Vertex>& Verts() const { return verts; }
    const std::vector<float>& Moisture() const { return moisture; }

    void Build(int gridSize, float spacing)
    {
        this->gridSize = gridSize;
        this->spacing = spacing;
        float half = gridSize * spacing * 0.5f;

        verts.clear();
        indices.clear();
        moisture.clear();

        verts.reserve((gridSize + 1) * (gridSize + 1));
        indices.reserve(gridSize * gridSize * 6);
        moisture.reserve((gridSize + 1) * (gridSize + 1));

        for (int z = 0; z <= gridSize; z++)
        {
            for (int x = 0; x <= gridSize; x++)
            {
                float wx = x * spacing - half;
                float wz = z * spacing - half;

                float dist = sqrt(wx * wx + wz * wz);

                float radius = half * 2.2f;
                float t = glm::clamp(dist / radius, 0.0f, 1.0f);

                float mask = 1.0f - smooth(t);
                mask = pow(mask, 1.4f);

                float nBig = fbm(wx * 0.012f, wz * 0.012f, 1337);
                float nMid = fbm(wx * 0.045f, wz * 0.045f, 9001);
                float nSmall = fbm(wx * 0.160f, wz * 0.160f, 420);

                nBig = nBig * 2.0f - 1.0f;
                nMid = nMid * 2.0f - 1.0f;
                nSmall = nSmall * 2.0f - 1.0f;

                float ridge = 1.0f - fabs(nMid);
                ridge = ridge * ridge;

                float height =
                    (nBig * 5.0f) +
                    (nMid * 3.5f) +
                    (ridge * 4.5f) +
                    (nSmall * 0.9f);

                height += 2.2f * mask;

                float coastStart = 0.78f;
                float coast = glm::smoothstep(coastStart, 1.0f, t);

                float land = glm::mix(height, seaLevel, coast);
                land *= (0.25f + 0.75f * mask);

                float m = fbm(wx * 0.035f, wz * 0.035f, 7777);
                float altitude01 = glm::clamp((land - seaLevel) / 10.0f, 0.0f, 1.0f);
                m = glm::mix(m, m * 0.6f, altitude01);
                m = glm::clamp(m, 0.0f, 1.0f);
                moisture.push_back(m);

                verts.push_back({ {wx, land, wz}, {0,1,0} });
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

    void Draw(Shader& shader, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, const Camera& cam, const glm::vec3& lightDir, const glm::vec3& lightCol)
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
    std::vector<Vertex> verts;
    std::vector<unsigned int> indices;
    std::vector<float> moisture;
    GLMesh mesh;

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

        glBindVertexArray(0);

        mesh.indexCount = (GLsizei)indices.size();
    }
};

//  Water 
class Water
{
public:
    float y = 2.5f;

    void Build(int grid, float spacing)
    {
        std::vector<Vertex> verts;
        std::vector<unsigned int> idx;

        float half = grid * spacing * 0.5f;

        verts.reserve((grid + 1) * (grid + 1));
        idx.reserve(grid * grid * 6);

        for (int z = 0; z <= grid; z++)
            for (int x = 0; x <= grid; x++)
                verts.push_back({ {x * spacing - half, y, z * spacing - half}, {0,1,0} });

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

    void Draw(Shader& shader, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj,
        const Camera& cam, const glm::vec3& lightDir, const glm::vec3& lightCol, float timeSeconds)
    {
        shader.Use();
        shader.SetMat4("uModel", glm::value_ptr(model));
        shader.SetMat4("uView", glm::value_ptr(view));
        shader.SetMat4("uProj", glm::value_ptr(proj));
        shader.SetFloat("uTime", timeSeconds);
        shader.SetVec3("uViewPos", cam.pos.x, cam.pos.y, cam.pos.z);
        shader.SetVec3("uLightDir", lightDir.x, lightDir.y, lightDir.z);
        shader.SetVec3("uLightColor", lightCol.x, lightCol.y, lightCol.z);
        shader.SetFloat("uAmbientStrength", 0.25f);
        shader.SetFloat("uSpecStrength", 0.6f);
        shader.SetFloat("uShininess", 128.0f);

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

//  Skybox 
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

//  Tree mesh building 
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

//  Tree system
class TreeSystem
{
public:
    void BuildMesh()
    {
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

        glBindVertexArray(0);
    }

    void PlaceOnTerrain(const Terrain& terrain)
    {
        instances.clear();
        instances.reserve(2500);

        const auto& verts = terrain.Verts();
        const auto& moisture = terrain.Moisture();

        std::mt19937 rng(12345);
        std::uniform_int_distribution<int> pick(0, (int)verts.size() - 1);
  
        std::uniform_real_distribution<float> jitter(-0.2f * 0.45f, 0.2f * 0.45f);

        std::uniform_real_distribution<float> rot(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> scaleR(0.8f, 1.5f);
        std::uniform_real_distribution<float> chance01(0.0f, 1.0f);

        const float minSpacing = 0.7f;
        const float minSpacing2 = minSpacing * minSpacing;

        const float slopeLimit = 0.80f;
        const float minMoisture = 0.45f;
        const float minHeight = terrain.seaLevel + 0.12f;
        const int desiredTrees = 2000;
        const int maxTries = desiredTrees * 25;

        for (int tries = 0; tries < maxTries && (int)instances.size() < desiredTrees; tries++)
        {
            int idx = pick(rng);

            const glm::vec3 p = verts[idx].pos;
            const glm::vec3 n = verts[idx].normal;
            const float m = moisture[idx];

            if (p.y < minHeight) continue;
            if (n.y < slopeLimit) continue;
            if (m < minMoisture) continue;

            float prob = glm::clamp((m - minMoisture) / (1.0f - minMoisture), 0.0f, 1.0f);
            prob = prob * prob;
            if (chance01(rng) > prob) continue;

            glm::vec3 candidate = p;
            candidate.x += jitter(rng);
            candidate.z += jitter(rng);

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

    void Draw(Shader& shader, const glm::mat4& view, const glm::mat4& proj, const Camera& cam, const glm::vec3& lightDir, const glm::vec3& lightCol) const
    {
        shader.Use();
        shader.SetMat4("uView", glm::value_ptr(view));
        shader.SetMat4("uProj", glm::value_ptr(proj));
        shader.SetVec3("uViewPos", cam.pos.x, cam.pos.y, cam.pos.z);
        shader.SetVec3("uLightDir", lightDir.x, lightDir.y, lightDir.z);
        shader.SetVec3("uLightColor", lightCol.x, lightCol.y, lightCol.z);
        shader.SetFloat("uAmbientStrength", 0.25f);
        shader.SetFloat("uSpecStrength", 0.15f);
        shader.SetFloat("uShininess", 16.0f);

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
        glBufferData(GL_ARRAY_BUFFER, instances.size() * sizeof(glm::mat4), instances.data(), GL_STATIC_DRAW);

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
    }
};

//  App 
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
        glfwSetWindowUserPointer(window, this);

        glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) {
            glViewport(0, 0, w, h);
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

        // Build scene assets
        terrain.seaLevel = 2.5f;
        terrain.Build(200, 0.2f);

        water.y = terrain.seaLevel;
        water.Build(120, 0.35f);

        sky.Build();

        trees.BuildMesh();
        trees.PlaceOnTerrain(terrain);

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

            camera.ProcessKeyboard(window, dt);
            tod.Update(dt);

            Render(now);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    void Shutdown()
    {
        trees.Destroy();
        terrain.Destroy();
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

    Camera camera;
    TimeOfDaySystem tod;

    Terrain terrain;
    Water water;
    Skybox sky;
    TreeSystem trees;

    std::unique_ptr<Shader> terrainShader, skyShader, waterShader, treeShader;

    void Render(float timeSeconds)
    {
        glm::vec3 lightDir = tod.LightDir();
        glm::vec3 lightCol = tod.LightColor();

        glClearColor(0.02f, 0.03f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = camera.ViewMatrix();
        glm::mat4 proj = glm::perspective(glm::radians(60.f), (float)width / (float)height, 0.1f, 500.f);
        glm::mat4 model(1.0f);

        // Sky first
        sky.Draw(*skyShader, view, proj, lightDir, tod.t01);

        // Terrain
        terrain.Draw(*terrainShader, model, view, proj, camera, lightDir, lightCol);

        // Trees
        trees.Draw(*treeShader, view, proj, camera, lightDir, lightCol);

        // Water
        water.Draw(*waterShader, model, view, proj, camera, lightDir, lightCol, timeSeconds);
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
