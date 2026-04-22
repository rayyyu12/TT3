#pragma once

#include <string>
#include <vector>
#include <map>
#include <SDL2/SDL_mixer.h>

struct LoadedSound {
    Mix_Chunk* chunk = nullptr;
    float duration = 0.0f;
};

// Parallel WAV loader. Each sound manager constructs one, feeds it a list of
// filenames, and gets back a map of key -> LoadedSound.  The caller owns the
// returned Mix_Chunk pointers and is responsible for freeing them.
class OptimizedSoundLoader {
public:
    explicit OptimizedSoundLoader(const std::string& sound_path);

    // Load all files in parallel using up to max_workers threads.
    // Keys in the returned map are filenames with ".wav" stripped.
    std::map<std::string, LoadedSound> load_sounds_parallel(
        const std::vector<std::string>& filenames,
        int max_workers = 4);

private:
    // Load a single WAV. Called from worker threads.
    LoadedSound load_one(const std::string& filename, float* out_load_time);

    std::string sound_path;
};
