#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <SDL2/SDL_mixer.h>

struct LoadedSound {
    Mix_Chunk* chunk = nullptr;
    float duration = 0.0f;
};

class OptimizedSoundLoader {
public:
    explicit OptimizedSoundLoader(const std::string& sound_path);

    std::map<std::string, LoadedSound> load_sounds_parallel(
        const std::vector<std::string>& filenames,
        int max_workers = 4);

private:
    struct RawFile {
        std::string key;
        std::string filename;
        std::vector<uint8_t> bytes;
    };

    static std::string strip_wav_extension(const std::string& filename);
    std::string get_cache_path() const;
    bool is_cache_valid(const std::vector<std::string>& filenames) const;

    std::map<std::string, LoadedSound> load_from_cache();
    void write_cache(const std::vector<RawFile>& raw_files,
                     const std::map<std::string, float>& durations);

    LoadedSound decode_wav_from_memory(const uint8_t* data, uint32_t size);

    std::string sound_path;
};
