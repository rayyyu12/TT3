#include "sound_loader.h"
#include "config.h"

#include <iostream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <queue>
#include <algorithm>
#include <chrono>
#include <cstdio>

OptimizedSoundLoader::OptimizedSoundLoader(const std::string& path)
    : sound_path(path) {}

LoadedSound OptimizedSoundLoader::load_one(const std::string& filename, float* out_load_time) {
    namespace fs = std::filesystem;
    using clock = std::chrono::steady_clock;

    std::string path = sound_path + "/" + filename;
    auto t_start = clock::now();

    if (!fs::exists(path)) {
        float elapsed = std::chrono::duration<float>(clock::now() - t_start).count();
        if (out_load_time) *out_load_time = elapsed;
        std::printf("Error: Sound file not found: '%s' at '%s'\n",
                    filename.c_str(), path.c_str());
        return {};
    }

    // Log file size before loading
    try {
        auto size_bytes = fs::file_size(path);
        float size_mb = static_cast<float>(size_bytes) / (1024.0f * 1024.0f);
        std::printf("[LOADING] %s (%.1f MB)...\n", filename.c_str(), size_mb);
    } catch (...) {}

    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    float elapsed = std::chrono::duration<float>(clock::now() - t_start).count();
    if (out_load_time) *out_load_time = elapsed;

    if (!chunk) {
        std::printf("Error: Could not load '%s': %s\n",
                    filename.c_str(), Mix_GetError());
        return {};
    }

    // Duration = total bytes / (frequency * bytes_per_sample * channels)
    constexpr int bytes_per_sample = 2; // 16-bit audio
    float total_samples = static_cast<float>(chunk->alen) / (bytes_per_sample * MIXER_CHANNELS_STEREO);
    float duration = total_samples / static_cast<float>(MIXER_FREQUENCY);

    return { chunk, duration };
}

std::map<std::string, LoadedSound> OptimizedSoundLoader::load_sounds_parallel(const std::vector<std::string>& filenames, int max_workers) {
    using clock = std::chrono::steady_clock;
    auto t_total_start = clock::now();

    std::printf("Loading %zu sounds with %d threads...\n", filenames.size(), max_workers);

    // Shared state protected by mutex
    struct Result {
        std::string key;
        std::string filename;
        LoadedSound sound;
        float load_time = 0.0f;
    };

    std::mutex queue_mtx;
    std::queue<std::string> work_queue;
    for (const auto& f : filenames) work_queue.push(f);

    std::mutex results_mtx;
    std::vector<Result> results;

    // Worker: pull filenames from the queue until empty
    auto worker = [&]() {
        while (true) {
            std::string filename;
            {
                std::lock_guard<std::mutex> lk(queue_mtx);
                if (work_queue.empty()) return;
                filename = work_queue.front();
                work_queue.pop();
            }

            float load_time = 0.0f;
            LoadedSound loaded = load_one(filename, &load_time);

            std::string key = filename;
            if (key.size() > 4 && key.substr(key.size() - 4) == ".wav") {
                key = key.substr(0, key.size() - 4);
            }

            {
                std::lock_guard<std::mutex> lk(results_mtx);
                results.push_back({ key, filename, loaded, load_time });
            }
        }
    };

    // Launch worker threads
    int num_threads = std::min(max_workers, static_cast<int>(filenames.size()));
    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Build output map and print per-file results
    std::map<std::string, LoadedSound> output;
    int successful = 0;

    struct SlowFile {
        std::string key;
        float time;
    };

    std::vector<SlowFile> slow_files;

    for (const auto& r : results) {
        output[r.key] = r.sound;

        if (r.sound.chunk) successful++;

        // File size for the completion report
        float size_mb = 0.0f;
        try {
            std::string path = sound_path + "/" + r.filename;
            if (std::filesystem::exists(path)) {
                size_mb = static_cast<float>(std::filesystem::file_size(path)) / (1024.0f * 1024.0f);
            }
        } catch (...) {}

        const char* slow_flag = (r.load_time > 5.0f) ? " [SLOW]" : "";
        if (r.sound.chunk) {
            std::printf("  + %s (%.3fs, %.1fMB)%s\n", r.filename.c_str(), r.load_time, size_mb, slow_flag);
        
        } else {
            std::printf("  x %s (failed)\n", r.filename.c_str());
        }

        if (r.load_time > 3.0f) {
            slow_files.push_back({r.key, r.load_time});
        }
    }

    float total_time = std::chrono::duration<float>(clock::now() - t_total_start).count();
    std::printf("Parallel Loading Summary:\n");
    std::printf("   Loaded: %d/%zu sounds\n", successful, filenames.size());
    std::printf("   Total time: %.2fs\n", total_time);
    if (!filenames.empty()) {
        std::printf("   Average per file: %.3fs\n", total_time / static_cast<float>(filenames.size()));
    }

    if (!slow_files.empty()) {
        std::sort(slow_files.begin(), slow_files.end(), [](const SlowFile& a, const SlowFile& b) {
            return a.time > b.time;
        });
        std::printf("   Slowest files:\n");
        int shown = 0;
        for (const auto& sf : slow_files) {
            if (shown++ >= 3) break;
            std::printf("      %s.wav: %.1fs\n", sf.key.c_str(), sf.time);
        }
    }

    return output;
}


