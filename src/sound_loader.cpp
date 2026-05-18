#include "sound_loader.h"
#include "config.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <queue>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <SDL2/SDL.h>

namespace fs = std::filesystem;

static constexpr char CACHE_MAGIC[4] = {'S', 'V', 'C', '2'};
static constexpr const char* CACHE_FILENAME = "sound_cache.bin";

OptimizedSoundLoader::OptimizedSoundLoader(const std::string& path)
    : sound_path(path) {}

std::string OptimizedSoundLoader::strip_wav_extension(const std::string& filename) {
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".wav")
        return filename.substr(0, filename.size() - 4);
    return filename;
}

std::string OptimizedSoundLoader::get_cache_path() const {
    return sound_path + "/" + CACHE_FILENAME;
}

// ---------------------------------------------------------------------------
// Cache validation: cache must exist and be newer than every requested WAV
// ---------------------------------------------------------------------------

bool OptimizedSoundLoader::is_cache_valid(const std::vector<std::string>& filenames) const {
    std::string cache_path = get_cache_path();
    if (!fs::exists(cache_path)) return false;

    try {
        auto cache_time = fs::last_write_time(cache_path);
        for (const auto& f : filenames) {
            std::string wav_path = sound_path + "/" + f;
            if (!fs::exists(wav_path)) continue;
            if (fs::last_write_time(wav_path) > cache_time)
                return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Decode a WAV from an in-memory buffer using SDL's official API
// ---------------------------------------------------------------------------

LoadedSound OptimizedSoundLoader::decode_wav_from_memory(const uint8_t* data, uint32_t size) {
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw) return {};

    Mix_Chunk* chunk = Mix_LoadWAV_RW(rw, 1);
    if (!chunk) return {};

    constexpr int bytes_per_sample = 2;
    float total_samples = static_cast<float>(chunk->alen)
                        / (bytes_per_sample * MIXER_CHANNELS_STEREO);
    float duration = total_samples / static_cast<float>(MIXER_FREQUENCY);

    return { chunk, duration };
}

// ---------------------------------------------------------------------------
// Load from cache: one sequential read, then decode each entry from memory
// ---------------------------------------------------------------------------

std::map<std::string, LoadedSound> OptimizedSoundLoader::load_from_cache() {
    using clock = std::chrono::steady_clock;
    auto t_start = clock::now();

    std::map<std::string, LoadedSound> result;
    std::string cache_path = get_cache_path();

    std::ifstream in(cache_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return {};

    auto file_size = in.tellg();
    in.seekg(0);

    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    in.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!in.good()) return {};
    in.close();

    float read_time = std::chrono::duration<float>(clock::now() - t_start).count();

    const uint8_t* ptr = buffer.data();
    const uint8_t* end = ptr + buffer.size();

    auto read_bytes = [&](void* dst, size_t n) -> bool {
        if (ptr + n > end) return false;
        std::memcpy(dst, ptr, n);
        ptr += n;
        return true;
    };

    char magic[4];
    if (!read_bytes(magic, 4) || std::memcmp(magic, CACHE_MAGIC, 4) != 0) {
        std::printf("Cache version mismatch, regenerating.\n");
        return {};
    }

    uint32_t entry_count = 0;
    if (!read_bytes(&entry_count, sizeof(entry_count))) return {};

    int decoded = 0;
    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t key_len = 0;
        if (!read_bytes(&key_len, sizeof(key_len))) break;
        if (key_len > 1024) break;

        std::string key(key_len, '\0');
        if (!read_bytes(key.data(), key_len)) break;

        float duration = 0.0f;
        if (!read_bytes(&duration, sizeof(duration))) break;

        uint32_t wav_len = 0;
        if (!read_bytes(&wav_len, sizeof(wav_len))) break;
        if (ptr + wav_len > end) break;

        LoadedSound snd = decode_wav_from_memory(ptr, wav_len);
        ptr += wav_len;

        if (snd.chunk) {
            snd.duration = duration;
            result[key] = snd;
            ++decoded;
        }
    }

    float total_time = std::chrono::duration<float>(clock::now() - t_start).count();
    float cache_mb = static_cast<float>(file_size) / (1024.0f * 1024.0f);
    std::printf("[CACHE] Loaded %d sounds from cache (%.1f MB read in %.3fs, "
                "%.3fs total with decode)\n",
                decoded, cache_mb, read_time, total_time);

    return result;
}

// ---------------------------------------------------------------------------
// Write cache: pack all raw WAV bytes + metadata into a single binary file
// ---------------------------------------------------------------------------

void OptimizedSoundLoader::write_cache(
        const std::vector<RawFile>& raw_files,
        const std::map<std::string, float>& durations) {

    std::string cache_path = get_cache_path();
    std::ofstream out(cache_path, std::ios::binary);
    if (!out.is_open()) {
        std::printf("Warning: Could not write sound cache to '%s'\n",
                    cache_path.c_str());
        return;
    }

    out.write(CACHE_MAGIC, 4);

    uint32_t entry_count = 0;
    for (const auto& rf : raw_files) {
        if (!rf.bytes.empty()) ++entry_count;
    }
    out.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));

    for (const auto& rf : raw_files) {
        if (rf.bytes.empty()) continue;

        uint32_t key_len = static_cast<uint32_t>(rf.key.size());
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(rf.key.data(), key_len);

        float dur = 0.0f;
        auto it = durations.find(rf.key);
        if (it != durations.end()) dur = it->second;
        out.write(reinterpret_cast<const char*>(&dur), sizeof(dur));

        uint32_t wav_len = static_cast<uint32_t>(rf.bytes.size());
        out.write(reinterpret_cast<const char*>(&wav_len), sizeof(wav_len));
        out.write(reinterpret_cast<const char*>(rf.bytes.data()), wav_len);
    }

    float cache_mb = static_cast<float>(out.tellp()) / (1024.0f * 1024.0f);
    std::printf("[CACHE] Written %.1f MB to '%s' (%u entries)\n",
                cache_mb, cache_path.c_str(), entry_count);
}

// ---------------------------------------------------------------------------
// Main entry point: try cache first, fall back to parallel file I/O + decode
// ---------------------------------------------------------------------------

std::map<std::string, LoadedSound> OptimizedSoundLoader::load_sounds_parallel(
        const std::vector<std::string>& filenames, int max_workers) {

    using clock = std::chrono::steady_clock;
    auto t_total_start = clock::now();

    // --- Fast path: load from cache ---
    if (is_cache_valid(filenames)) {
        std::printf("[%s] Valid sound cache found, loading from cache...\n",
                    sound_path.c_str());
        auto cached = load_from_cache();
        if (!cached.empty()) {
            float total = std::chrono::duration<float>(clock::now() - t_total_start).count();
            std::printf("[%s] Cache load complete: %zu sounds in %.2fs\n",
                        sound_path.c_str(), cached.size(), total);
            return cached;
        }
        std::printf("[%s] Cache load failed, falling back to file loading.\n",
                    sound_path.c_str());
    }

    // --- Slow path: read file bytes in parallel, decode on main thread ---
    std::printf("[%s] Loading %zu sounds (%d I/O threads)...\n",
                sound_path.c_str(), filenames.size(), max_workers);

    // Phase 1: Read raw WAV file bytes in parallel (thread-safe file I/O)
    std::mutex queue_mtx;
    std::queue<size_t> work_queue;
    for (size_t i = 0; i < filenames.size(); ++i) work_queue.push(i);

    std::vector<RawFile> raw_files(filenames.size());
    for (size_t i = 0; i < filenames.size(); ++i) {
        raw_files[i].filename = filenames[i];
        raw_files[i].key = strip_wav_extension(filenames[i]);
    }

    std::mutex print_mtx;

    auto worker = [&]() {
        while (true) {
            size_t idx;
            {
                std::lock_guard<std::mutex> lk(queue_mtx);
                if (work_queue.empty()) return;
                idx = work_queue.front();
                work_queue.pop();
            }

            std::string path = sound_path + "/" + raw_files[idx].filename;
            try {
                if (!fs::exists(path)) {
                    std::lock_guard<std::mutex> lk(print_mtx);
                    std::printf("  x %s (not found)\n",
                                raw_files[idx].filename.c_str());
                    continue;
                }

                auto fsize = fs::file_size(path);
                std::vector<uint8_t> buf(static_cast<size_t>(fsize));
                std::ifstream in(path, std::ios::binary);
                in.read(reinterpret_cast<char*>(buf.data()),
                        static_cast<std::streamsize>(fsize));

                if (in.good()) {
                    raw_files[idx].bytes = std::move(buf);
                    std::lock_guard<std::mutex> lk(print_mtx);
                    float mb = static_cast<float>(fsize) / (1024.0f * 1024.0f);
                    std::printf("  [READ] %s (%.1f MB)\n",
                                raw_files[idx].filename.c_str(), mb);
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(print_mtx);
                std::printf("  x %s (error: %s)\n",
                            raw_files[idx].filename.c_str(), e.what());
            }
        }
    };

    int num_threads = std::min(max_workers, static_cast<int>(filenames.size()));
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    float io_time = std::chrono::duration<float>(clock::now() - t_total_start).count();
    std::printf("[%s] File I/O complete in %.2fs, decoding...\n",
                sound_path.c_str(), io_time);

    // Phase 2: Decode WAVs on the main thread (single-threaded, SDL-safe)
    std::map<std::string, LoadedSound> output;
    std::map<std::string, float> durations;
    int successful = 0;

    for (const auto& rf : raw_files) {
        if (rf.bytes.empty()) {
            std::printf("  x %s (no data)\n", rf.filename.c_str());
            continue;
        }

        LoadedSound snd = decode_wav_from_memory(rf.bytes.data(),
                                                  static_cast<uint32_t>(rf.bytes.size()));
        if (snd.chunk) {
            output[rf.key] = snd;
            durations[rf.key] = snd.duration;
            ++successful;
        } else {
            std::printf("  x %s (decode failed: %s)\n",
                        rf.filename.c_str(), Mix_GetError());
        }
    }

    float total_time = std::chrono::duration<float>(clock::now() - t_total_start).count();
    std::printf("[%s] Loaded %d/%zu sounds in %.2fs (I/O: %.2fs)\n",
                sound_path.c_str(), successful, filenames.size(),
                total_time, io_time);

    // Phase 3: Write cache for next startup
    if (successful > 0) {
        write_cache(raw_files, durations);
    }

    return output;
}
