#include "AudioSystem.h"
#include <iostream>
#include <filesystem>

static bool FileExists(const char* path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool AudioSystem::Init()
{
    engine = createIrrKlangDevice();
    if (!engine)
    {
        std::cerr << "Failed to start irrKlang.\n";
        return false;
    }

    // Ambient loops
    const char* oceanPath = "assets/sfx/ocean.wav";
    const char* stormPath = "assets/sfx/storm_wind.wav";

    if (!FileExists(oceanPath)) std::cerr << "[WARN] Missing audio file: " << oceanPath << "\n";
    if (!FileExists(stormPath)) std::cerr << "[WARN] Missing audio file: " << stormPath << "\n";

    oceanLoop = engine->play2D(oceanPath, true, false, true);
    if (oceanLoop) oceanLoop->setVolume(0.55f);

    stormLoop = engine->play2D(stormPath, true, false, true);
    if (stormLoop) stormLoop->setVolume(0.0f);

    stormMix = 0.0f;
    return true;
}

void AudioSystem::Shutdown()
{
    for (auto& kv : lighthouseHums)
    {
        if (kv.second) { kv.second->stop(); kv.second->drop(); }
    }
    lighthouseHums.clear();

    if (oceanLoop) { oceanLoop->stop(); oceanLoop->drop(); oceanLoop = nullptr; }
    if (stormLoop) { stormLoop->stop(); stormLoop->drop(); stormLoop = nullptr; }

    if (engine) { engine->drop(); engine = nullptr; }
}

void AudioSystem::UpdateListener(const glm::vec3& pos, const glm::vec3& front, const glm::vec3& up)
{
    if (!engine) return;

    irrklang::vec3df p(pos.x, pos.y, pos.z);
    irrklang::vec3df look(front.x, front.y, front.z);
    irrklang::vec3df upv(up.x, up.y, up.z);
    irrklang::vec3df vel(0, 0, 0);

    engine->setListenerPosition(p, look, vel, upv);
}

void AudioSystem::UpdateStormMix(float dt, bool stormMode)
{
    if (!engine) return;

    float target = stormMode ? 1.0f : 0.0f;
    stormMix += (target - stormMix) * glm::clamp(dt * 1.5f, 0.0f, 1.0f);

    if (oceanLoop) oceanLoop->setVolume(0.55f * (1.0f - 0.35f * stormMix));
    if (stormLoop) stormLoop->setVolume(0.75f * stormMix);
}

void AudioSystem::PlayOneShot(const char* path, float volume)
{
    if (!engine) return;
    ISound* s = engine->play2D(path, false, false, true);
    if (s)
    {
        s->setVolume(volume);
        s->drop(); // IMPORTANT: one-shot, free it
    }
}

ISound* AudioSystem::PlayLoop(const char* path, float volume)
{
    if (!engine) return nullptr;
    ISound* s = engine->play2D(path, true, false, true);
    if (s) s->setVolume(volume);
    return s; // caller owns it (and must stop/drop)
}
