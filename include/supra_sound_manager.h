#pragma once

#include "config.h"
#include <string>
#include <map>
#include <vector>
#include <random>
#include <SDL2/SDL_mixer.h>

struct SupraRevStage {
    std::string key;
    Mix_Chunk* sound = nullptr;
    int rpm_peak = 0;
    float duration = 0.0f;
};

struct SupraRevResult {
    std::string key;
    int rpm_peak = 0;
    float duration = 0.0f;
};

class SupraSoundManager {
public:
    SupraSoundManager();
    ~SupraSoundManager();

    SupraSoundManager(const SupraSoundManager&) = delete;
    SupraSoundManager& operator=(const SupraSoundManager&) = delete;

    // Called every frame
    void update_idle_fade(float dt);
    void update_driving_crossfade();

    // Idle
    void play_idle();
    void stop_idle();
    void set_idle_target_volume(float target, bool instant = false);

    // Driving sounds -- throttle-range clip selection with A/B crossfade
    std::string get_throttle_range(float throttle) const;
    bool play_driving_sound(float throttle_pct,
                            const std::string& force_type = "",
                            bool crossfade = false);
    bool is_driving_sound_busy() const;
    void stop_driving_sounds(int fade_ms = 0);

    // Staged revs (RPM-aware, same logic as M4)
    SupraRevResult* play_staged_rev(float current_rpm, float gesture_peak_throttle);

    // Simple rev (fallback for backwards compat: picks rev_1/2/3 by throttle)
    bool play_rev_sound(float peak_throttle);
    bool is_rev_sound_busy() const;

    // Startup one-shot
    bool play_startup_sound();

    // Global controls
    void stop_all_sounds();
    void fade_out_all_sounds(int fade_ms);

    // Reverse lookup for logging
    std::string get_sound_name_from_obj(Mix_Chunk* sound_obj) const;

    // Public state -- read by SupraEngineSimulation
    float idle_target_volume  = SUPRA_NORMAL_IDLE_VOLUME;
    float idle_current_volume = SUPRA_NORMAL_IDLE_VOLUME;
    bool  idle_is_fading      = false;
    bool  transitioning_driving_sound = false;
    float last_clip_start_time = 0.0f;

private:
    void load_sounds();
    void free_all_sounds();
    Mix_Chunk* get_sound(const std::string& key) const;
    static int to_mix_volume(float vol);
    float get_time_seconds() const;

    // Pick a random clip from a category, skipping nulls
    std::string pick_random_clip(const std::vector<std::string>& clips);

    std::map<std::string, Mix_Chunk*> sounds;
    std::vector<SupraRevStage> rev_stages;

    // Categorized clip lists (keys into the sounds map)
    std::vector<std::string> light_pull_sounds;
    std::vector<std::string> light_cruise_sounds;
    std::vector<std::string> aggressive_push_sounds;
    std::vector<std::string> violent_pull_sounds;

    int active_driving_channel  = SUPRA_CH_DRIVING_A;
    float transition_start_time = 0.0f;

    std::mt19937 rng;

    SupraRevResult last_rev_result;
};
