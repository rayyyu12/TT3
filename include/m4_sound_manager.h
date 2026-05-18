#pragma once

#include "config.h"
#include <string>
#include <map>
#include <vector>
#include <SDL2/SDL_mixer.h>

struct M4RevStage {
    std::string key;
    Mix_Chunk* sound = nullptr;
    int rpm_peak = 0;
    float duration = 0.0f;
};

struct M4RevResult {
    std::string key;
    int rpm_peak = 0;
    float duration = 0.0f;
};

struct M4PendingOffsetSound {
    std::string sound_key;
    float scheduled_time;
    float offset_duration;
    int loops;
};

class M4SoundManager {
public:
    M4SoundManager();
    ~M4SoundManager();

    M4SoundManager(const M4SoundManager&) = delete;
    M4SoundManager& operator=(const M4SoundManager&) = delete;

    // Called every frame by the engine simulation
    void update();
    void update_idle_fade(float dt);
    void update_long_sequence_crossfade();

    // Idle
    void play_idle();
    void stop_idle();
    void set_idle_target_volume(float target, bool instant = false);

    // Staged revs (RPM-aware selection based on current RPM + gesture intensity)
    M4RevResult* play_staged_rev(float current_rpm, float gesture_peak_throttle);
    void stop_staged_rev_sound();

    // Turbo / limiter SFX channel (shared with starter and launch control)
    bool play_turbo_or_limiter_sfx(const std::string& sound_key);
    bool play_starter_sfx();
    void stop_turbo_limiter_sfx();
    bool is_turbo_limiter_sfx_busy() const;
    bool any_playful_sfx_active() const;

    // Launch control (engage -> wait for finish -> hold loop)
    bool play_launch_control_sequence();
    void stop_launch_control_sequence(int fade_ms = M4_FADE_OUT_MS / 2);
    bool is_launch_control_active() const;

    // Long sequence (dual-channel A/B crossfade with optional start offset)
    void play_long_sequence(const std::string& sound_key, int loops = 0,
                            bool transition_from_other = false,
                            float start_offset = 0.0f);
    void stop_long_sequence(int fade_ms = 0);
    bool is_long_sequence_busy() const;

    // Global controls
    void stop_all_sounds();
    void fade_out_all_sounds(int fade_ms);

    // Reverse lookup for logging
    std::string get_sound_name_from_obj(Mix_Chunk* sound_obj) const;

    // Public state -- read by M4EngineSimulation
    float idle_target_volume  = M4_NORMAL_IDLE_VOLUME;
    float idle_current_volume = M4_NORMAL_IDLE_VOLUME;
    bool  idle_is_fading      = false;

    bool waiting_for_launch_hold_loop = false;
    bool launch_control_sounds_active = false;
    bool just_switched_to_lc_hold     = false;
    bool transitioning_long_sound     = false;

private:
    void load_sounds();
    void free_all_sounds();

    Mix_Chunk* get_sound(const std::string& key) const;
    static int to_mix_volume(float vol);
    float get_time_seconds() const;

    std::map<std::string, Mix_Chunk*> sounds;
    std::vector<M4RevStage> rev_stages;
    std::vector<M4PendingOffsetSound> pending_offset_sounds;

    int active_long_channel     = M4_CH_LONG_SEQUENCE_A;
    float transition_start_time = 0.0f;

    // Returned by pointer from play_staged_rev so the caller can read the result
    M4RevResult last_rev_result;
};
