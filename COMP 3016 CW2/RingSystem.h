#pragma once
#include <vector>
#include <random>
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>
#include <glm/glm/gtc/constants.hpp>

#include <GL/glew.h>

class Shader;
class Camera;

// Minimal vertex for ring mesh (pos + normal)
struct RingVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
};

struct RingMesh
{
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0;

    void Destroy()
    {
        if (ebo) glDeleteBuffers(1, &ebo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = ebo = 0;
        indexCount = 0;
    }
};

class RingSystem
{
public:
    struct Ring
    {
        glm::vec3 posWS{ 0.0f };
        float yaw = 0.0f;        // rotate around Y
        float pitch = 0.0f;      // rotate around X 
        float scale = 1.0f;
        bool collected = false;
    };

    void InitMesh(float majorR = 2.0f, float minorR = 0.35f, int segMajor = 48, int segMinor = 18);
    void Destroy();

    void Reset();

    // Spawns rings around islands using terrain sampling callbacks (so RingSystem stays OOP/decoupled)
    // sampleHeight(localX, localZ) should return local terrain height for that island
    // sampleNormal(localX, localZ) should return local terrain normal for that island
    template<typename HeightFn, typename NormalFn>
    void SpawnForIsland(int islandIndex,
        const glm::vec2& islandCenterXZ,
        float islandHalfSize,
        int count,
        int seed,
        HeightFn sampleHeight,
        NormalFn sampleNormal);

    // Returns how many rings were collected this frame
    int UpdateCollect(const glm::vec3& playerPosWS);

    void Draw(Shader& shader,
        const glm::mat4& view,
        const glm::mat4& proj,
        const Camera& cam,
        const glm::vec3& sunDir,
        const glm::vec3& sunCol,
        bool fogEnabled,
        const glm::vec3& fogColor,
        float fogDensity,
        float nightFactor);

    // Scoring
    int GetScore() const { return score; }
    int GetCollected() const { return collectedCount; }
    int GetTotal() const { return totalCount; }

    // Tuning
    void SetCollectRadius(float r) { collectRadius = r; }
    void SetPointsPerRing(int p) { pointsPerRing = p; }

private:
    RingMesh mesh;
    std::vector<Ring> rings;

    float collectRadius = 2.25f;
    int pointsPerRing = 10;

    int score = 0;
    int collectedCount = 0;
    int totalCount = 0;

    glm::mat4 RingModelMatrix(const Ring& r) const;
};

// Template implementation in header
template<typename HeightFn, typename NormalFn>
void RingSystem::SpawnForIsland(int islandIndex,
    const glm::vec2& islandCenterXZ,
    float islandHalfSize,
    int count,
    int seed,
    HeightFn sampleHeight,
    NormalFn sampleNormal)
{
    std::mt19937 rng(seed + islandIndex * 1337);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_real_distribution<float> yawR(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> scaleR(0.9f, 1.35f);
    std::uniform_real_distribution<float> pitchR(glm::radians(65.0f), glm::radians(90.0f));


    // Place rings in a nice band around the island (avoid center and coastline)
    float rMin = islandHalfSize * 0.18f;
    float rMax = islandHalfSize * 0.62f;

    for (int i = 0; i < count; i++)
    {
        // random polar
        float a = u01(rng) * 6.2831853f;
        float r = glm::mix(rMin, rMax, u01(rng));

        float lx = cos(a) * r;
        float lz = sin(a) * r;

        // Keep them on flatter ground
        glm::vec3 n = sampleNormal(lx, lz);
        if (n.y < 0.88f) { i--; continue; }

        float y = sampleHeight(lx, lz);
        if (y < 0.0f) { i--; continue; }

        Ring ring;
        ring.posWS = glm::vec3(islandCenterXZ.x + lx, y + 5.0f + u01(rng) * 4.0f, islandCenterXZ.y + lz);
        ring.yaw = a + glm::half_pi<float>();     
        ring.pitch = glm::radians(85.0f);
        ring.scale = scaleR(rng);
        ring.collected = false;

        rings.push_back(ring);
    }

    totalCount = (int)rings.size();
}
