#pragma once

#include "config.h"
#include <string>
#include <map>
#include <vector>
#include <random>
#include <SDL2/SDL_mixer.h>

class HellcatSoundManager {
public:
    HellcatSoundManager();
    ~HellcatSoundManager();

    HellcatSoundManager(const HellcatSoundManager&) = delete;
    HellcatSoundManager& operator=(const HellcatSoundManager&) = delete;

    void update(float dt);
    void update_idle_fade(float dt);
    void set_idle_target_volume(float target, bool instant = false);

    void play_foundation_layer(float smoothed_throttle, float simulated_rpm);
    void play_character_layer(float engine_load, float simulated_rpm,
                              float smoothed_throttle);

    bool play_startup_sound();
    bool play_upshift_sound();
    bool play_downshift_sound();
    bool is_startup_busy() const;
    bool is_shift_busy() const;

    bool play_simple_rev();
    bool is_rev_busy() const;
    void clear_rev_queue();

    void stop_all_sounds();
    void fade_out_all_sounds(int fade_ms);

    // Public state -- read by HellcatEngineSimulation
    float idle_target_volume  = HELLCAT_NORMAL_IDLE_VOLUME;
    float idle_current_volume = HELLCAT_NORMAL_IDLE_VOLUME;
    bool  idle_is_fading      = false;

    float rumble_low_current_vol  = 0.0f;
    float rumble_low_target_vol   = 0.0f;
    float rumble_mid_current_vol  = 0.0f;
    float rumble_mid_target_vol   = 0.0f;
    float whine_low_current_vol   = 0.0f;
    float whine_low_target_vol    = 0.0f;
    float whine_high_current_vol  = 0.0f;
    float whine_high_target_vol   = 0.0f;

    bool whine_low_crossfading    = false;
    bool whine_high_crossfading   = false;
    bool accel_response_fading    = false;

private:
    void load_sounds();
    void free_all_sounds();

    Mix_Chunk* get_sound(const std::string& key) const;
    static int to_mix_volume(float vol);
    float get_time_seconds() const;

    float power_curve(float input_val, float power = 2.2f) const;
    void equal_power_crossfade(float progress, float& fade_out,
                               float& fade_in) const;
    void update_audio_inertia(float dt);
    void update_whine_sounds_enhanced(float smoothed_throttle,
                                      float simulated_rpm);
    void manage_whine_crossfade(bool is_low, Mix_Chunk* sound);
    bool play_rev_now(const std::string& sound_key);
    void process_rev_queue();

    std::map<std::string, Mix_Chunk*> sounds;

    // Whine crossfade state
    int   whine_low_active_channel         = HELLCAT_CH_WHINE_LOW_A;
    float whine_low_crossfade_start_time   = 0.0f;
    float whine_low_play_start             = 0.0f;
    float whine_low_duration               = 14.0f;

    int   whine_high_active_channel        = HELLCAT_CH_WHINE_HIGH_A;
    float whine_high_crossfade_start_time  = 0.0f;
    float whine_high_play_start            = 0.0f;
    float whine_high_duration              = 14.0f;

    float accel_response_fade_start = 0.0f;

    std::vector<std::string> rev_queue;
    int max_rev_queue_size = 3;

    std::mt19937 rng;
};
