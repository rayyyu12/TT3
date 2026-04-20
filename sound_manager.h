#pragma once

#include <string>
#include <map>
#include <vector>
#include <SDL2/SDL_mixer.h>

#include "config.h"

struct RevStage {
    std::string key;
    Mix_Chunk* sound = nullptr;
    int rpm_peak = 0;
    int intensity = 0;
    float duration = 0.0f;
};

struct RevResult {
    float duration = 0.0f;
    int rpm_peak = 0;
};

class SoundManager {
public:
    SoundManager(const std::string& profile = "m4");
    ~SoundManager();

    // Prevent copying -- each SoundManager owns its Mix_Chunk resources
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    void load_sounds();
    void switch_profile(const std::string& new_profile);

    bool play_startup();
    void play_idle();
    void set_idle_target_volume(float target, bool instant = false);
    void update_idle_fade(float dt);

    void play_long_sequence(const std::string& sound_key, int loops = 0, bool transition_from_other = false);
    void update_long_sequence_crossfade();
    void stop_long_sequence(int fade_ms = 0);
    bool is_long_sequence_busy() const;

    bool play_staged_rev(float gesture_peak_throttle, RevResult& out_result);

    void stop_all_sounds(int fade_ms = 0);

    void update();

    bool is_startup_busy() const;
    bool is_staged_rev_busy() const;

    std::string sound_profile;

    bool waiting_for_launch_hold_loop = false;
    bool launch_control_sounds_active = false;
    bool just_switched_to_lc_hold = false;

    Mix_Chunk* get_sound(const std::string& key) const;
    float get_sound_duration(const std::string& key) const;

private:
    void free_all_sounds();
    Mix_Chunk* load_sound(const std::string& filename, float* out_duration = nullptr);

    void load_m4_sounds();
    void load_supra_sounds();

    // Converts a 0.0-1.0 float volume to SDL_mixer's 0-128 int range
    static int to_mix_volume(float vol);

    std::map<std::string, Mix_Chunk*> sounds;
    std::map<std::string, float> sound_durations;
    std::vector<RevStage> rev_stages;

    float startup_duration_supra = 2.0f;

    int active_long_channel = CH_LONG_SEQUENCE_A;
    bool transitioning_long_sound = false;
    float transition_start_time = 0.0f;
    int crossfade_duration_ms = 500;

    float idle_target_volume = 0.7f;
    float idle_current_volume = 0.7f;
    bool idle_is_fading = false;
};
