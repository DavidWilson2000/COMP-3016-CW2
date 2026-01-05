
#pragma once
#include <vector>
#include <random>
#include <glm/glm/glm.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "RingSystem.h"

// Forward declarations (so we don’t include massive headers here)
class Terrain;
class Water;
class TreeSystem;
struct WorldConfig;
struct Island;
struct GLModel;


enum class IslandBiome : int;

// WORLD owns islands + rebuild logic (generation).
class World
{
public:
    World() = default;

    // Build everything from scratch with the current config + seed
    void Rebuild(int seed,
        WorldConfig& cfg,
        RingSystem& rings,
        bool treeModelLoaded,
        const GLModel& treeModel,
        const glm::vec3& treePivotMS,
        bool housesLoaded,
        const std::vector<GLModel>& houseModels,
        bool lighthouseLoaded);

    // Queries
    Island* NearestIsland(float x, float z);
    std::vector<Island>& GetIslands() { return islands; }
    const std::vector<Island>& GetIslands() const { return islands; }

private:
    std::vector<Island> islands;

    // helpers (moved from App)
    static IslandBiome PickIslandBiome(std::mt19937& rng);
    bool FindLighthouseSpot(const Terrain& t, glm::vec3& outLocalPos) const;
};
