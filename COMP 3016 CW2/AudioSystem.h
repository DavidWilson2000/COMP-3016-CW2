#pragma once
#include <unordered_map>
#include <string>

#include <irrKlang/irrKlang.h>
#include <glm/glm/glm.hpp>

using namespace irrklang;

class AudioSystem
{
public:
    bool Init();
    void Shutdown();

    void UpdateListener(const glm::vec3& pos, const glm::vec3& front, const glm::vec3& up);

    // dt = delta time, stormMode tells it where to fade toward
    void UpdateStormMix(float dt, bool stormMode);

    // simple helpers
    void PlayOneShot(const char* path, float volume = 1.0f);
    ISound* PlayLoop(const char* path, float volume);

private:
    ISoundEngine* engine = nullptr;
    ISound* oceanLoop = nullptr;
    ISound* stormLoop = nullptr;

    float stormMix = 0.0f;

    // You currently have this map but don’t seem to use it much yet:
    std::unordered_map<int, ISound*> lighthouseHums;
};
