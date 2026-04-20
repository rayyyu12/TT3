#include "sound_manager.h"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <SDL2/SDL.h>

static float get_time_seconds() {
    return SDL_GetTicks() / 1000.0f;
}

int SoundManager::to_mix_volume(float vol) {
    return static_cast<int>(std::clamp(vol, 0.0f, 1.0f) * MIX_MAX_VOLUME);
}

Mix_Chunk* SoundManager::get_sound(const std::string& key) const {
    auto it = sounds.find(key);
    if (it != sounds.end()) return it->second;
    return nullptr;
}

float SoundManager::get_sound_duration(const std::string& key) const {
    auto it = sound_durations.find(key);
    if (it != sound_durations.end()) return it->second;
    return 0.0f;
}

// --- Construction / Destruction ---

SoundManager::SoundManager(const std::string& profile)
    : sound_profile(profile)
{
    load_sounds();
}

SoundManager::~SoundManager() {
    free_all_sounds();
}

void SoundManager::free_all_sounds() {
    // Rev stage sounds point to the same Mix_Chunk* as the sounds map,
    // so clear rev_stages first (without freeing) to avoid double-free.
    rev_stages.clear();

    for (auto& [key, chunk] : sounds) {
        if (chunk) {
            Mix_FreeChunk(chunk);
        }
    }
    sounds.clear();
    sound_durations.clear();
}

// --- Sound Loading ---

Mix_Chunk* SoundManager::load_sound(const std::string& filename, float* out_duration) {
    std::string path = sound_profile + "/" + filename;

    if (!std::filesystem::exists(path)) {
        return nullptr;
    }

    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) {
        std::cout << "Warning: Could not load '" << filename << "': " << Mix_GetError() << "\n";
        return nullptr;
    }

    if (out_duration) {
        // Calculate duration: bytes / (frequency * bytes_per_sample * channels)
        int bytes_per_sample = 2; // 16-bit audio
        float total_samples = static_cast<float>(chunk->alen) / (bytes_per_sample * MIXER_CHANNELS_STEREO);
        *out_duration = total_samples / MIXER_FREQUENCY;
    }

    return chunk;
}

void SoundManager::load_sounds() {
    free_all_sounds();

    if (sound_profile == "supra") {
        crossfade_duration_ms = 200;
    } else {
        crossfade_duration_ms = 500;
    }

    std::cout << "Loading sounds from '" << sound_profile << "' folder...\n";

    if (sound_profile == "m4") {
        load_m4_sounds();
    } else if (sound_profile == "supra") {
        load_supra_sounds();
    } else {
        std::cout << "Warning: No sound loading method for profile '" << sound_profile << "'\n";
    }
}

void SoundManager::load_m4_sounds() {
    sounds["idle"] = load_sound("engine_idle_loop.wav");
    sounds["starter"] = load_sound("engine_starter.wav");

    struct StageInfo { const char* filename; int rpm_peak; };
    StageInfo stages[] = {
        {"engine_rev_stage1.wav", 3000},
        {"engine_rev_stage2.wav", 5000},
        {"engine_rev_stage3.wav", 7000},
        {"engine_rev_stage4.wav", 8500}
    };

    for (int i = 0; i < 4; i++) {
        float dur = 0.0f;
        Mix_Chunk* snd = load_sound(stages[i].filename, &dur);
        if (snd) {
            RevStage stage;
            stage.key = "rev_stage" + std::to_string(i + 1);
            stage.sound = snd;
            stage.rpm_peak = stages[i].rpm_peak;
            stage.duration = dur;
            rev_stages.push_back(stage);

            sounds[stage.key] = snd;
        }
    }

    sounds["turbo_bov"] = load_sound("turbo_spool_and_bov.wav");
    sounds["rev_limiter"] = load_sound("engine_high_rev_with_limiter.wav");
    sounds["accel_gears"] = load_sound("acceleration_gears_1_to_4.wav");
    sounds["cruising"] = load_sound("engine_cruising_loop.wav");
    sounds["decel_downshifts"] = load_sound("deceleration_downshifts_to_idle.wav");
    sounds["launch_control_engage"] = load_sound("launch_control_engage.wav");
    sounds["launch_control_hold_loop"] = load_sound("launch_control_hold_loop.wav");
}

void SoundManager::load_supra_sounds() {
    float dur = 0.0f;
    sounds["startup"] = load_sound("supra_startup.wav", &dur);
    sounds["startup_duration"] = nullptr; // duration stored separately
    startup_duration_supra = dur;

    sounds["idle"] = load_sound("supra_idle_loop.wav");

    const char* clip_names[] = {
        "light_pull_1", "light_pull_2",
        "light_cruise_1", "light_cruise_2", "light_cruise_3",
        "aggressive_push_1", "aggressive_push_2", "aggressive_push_3",
        "aggressive_push_4", "aggressive_push_5", "aggressive_push_6",
        "violent_pull_1", "violent_pull_2", "violent_pull_3",
        "highway_cruise_loop"
    };

    for (const char* name : clip_names) {
        float clip_dur = 0.0f;
        sounds[name] = load_sound(std::string(name) + ".wav", &clip_dur);
        sound_durations[name] = clip_dur;
    }

    for (int i = 1; i <= 3; i++) {
        std::string key = "rev_" + std::to_string(i);
        float rev_dur = 0.0f;
        Mix_Chunk* snd = load_sound("supra_rev_" + std::to_string(i) + ".wav", &rev_dur);
        if (snd) {
            RevStage stage;
            stage.key = key;
            stage.sound = snd;
            stage.intensity = i;
            stage.duration = rev_dur;
            rev_stages.push_back(stage);

            sounds[key] = snd;
        }
    }
}

// --- Playback ---

bool SoundManager::play_startup() {
    // Try "startup" first (supra), fall back to "starter" (m4)
    Mix_Chunk* startup_sound = nullptr;
    auto it = sounds.find("startup");
    if (it != sounds.end() && it->second) {
        startup_sound = it->second;
    } else {
        it = sounds.find("starter");
        if (it != sounds.end()) startup_sound = it->second;
    }

    if (startup_sound && !Mix_Playing(CH_STARTUP)) {
        Mix_Volume(CH_STARTUP, to_mix_volume(MASTER_VOLUME));
        Mix_PlayChannel(CH_STARTUP, startup_sound, 0);
        return true;
    }
    return false;
}

void SoundManager::play_idle() {
    Mix_Chunk* idle_sound = get_sound("idle");
    if (!idle_sound) return;

    // If idle channel isn't playing, or is playing a different sound, restart it
    if (!Mix_Playing(CH_IDLE) || Mix_GetChunk(CH_IDLE) != idle_sound) {
        Mix_PlayChannel(CH_IDLE, idle_sound, -1); // -1 = loop forever
    }
    Mix_Volume(CH_IDLE, to_mix_volume(idle_current_volume * MASTER_VOLUME));
}

void SoundManager::set_idle_target_volume(float target, bool instant) {
    idle_target_volume = std::clamp(target, 0.0f, 1.0f);

    if (instant) {
        idle_current_volume = idle_target_volume;
        if (Mix_Playing(CH_IDLE)) {
            Mix_Volume(CH_IDLE, to_mix_volume(idle_current_volume * MASTER_VOLUME));
        }
        idle_is_fading = false;
    } else {
        if (std::abs(idle_current_volume - idle_target_volume) > 0.01f) {
            idle_is_fading = true;
        }
    }
}

void SoundManager::update_idle_fade(float dt) {
    if (!idle_is_fading || !Mix_Playing(CH_IDLE)) return;

    float fade_speed = (sound_profile == "m4") ? 2.5f : 4.0f;

    if (std::abs(idle_current_volume - idle_target_volume) < 0.01f) {
        idle_current_volume = idle_target_volume;
        idle_is_fading = false;
    } else if (idle_current_volume < idle_target_volume) {
        idle_current_volume = std::min(idle_current_volume + fade_speed * dt, idle_target_volume);
    } else {
        idle_current_volume = std::max(idle_current_volume - fade_speed * dt, idle_target_volume);
    }

    Mix_Volume(CH_IDLE, to_mix_volume(idle_current_volume * MASTER_VOLUME));
}

// --- Long Sequence (crossfading between two channels) ---

void SoundManager::play_long_sequence(const std::string& sound_key, int loops, bool transition_from_other) {
    Mix_Chunk* sound_to_play = get_sound(sound_key);
    if (!sound_to_play) return;

    int target_vol = to_mix_volume(1.0f * MASTER_VOLUME);

    if (!transition_from_other) {
        int other_channel = (active_long_channel == CH_LONG_SEQUENCE_A) ? CH_LONG_SEQUENCE_B : CH_LONG_SEQUENCE_A;
        Mix_HaltChannel(other_channel);
        Mix_Volume(active_long_channel, target_vol);
        Mix_PlayChannel(active_long_channel, sound_to_play, loops);
        transitioning_long_sound = false;
    } else {
        int fade_out_channel = active_long_channel;
        int fade_in_channel = (active_long_channel == CH_LONG_SEQUENCE_A) ? CH_LONG_SEQUENCE_B : CH_LONG_SEQUENCE_A;

        Mix_FadeOutChannel(fade_out_channel, crossfade_duration_ms);
        Mix_Volume(fade_in_channel, 0);
        Mix_PlayChannel(fade_in_channel, sound_to_play, loops);

        active_long_channel = fade_in_channel;
        transitioning_long_sound = true;
        transition_start_time = get_time_seconds();
    }
}

void SoundManager::update_long_sequence_crossfade() {
    if (!transitioning_long_sound) return;

    float elapsed_ms = (get_time_seconds() - transition_start_time) * 1000.0f;
    float progress = std::min(1.0f, elapsed_ms / crossfade_duration_ms);

    int target_vol = to_mix_volume(progress * MASTER_VOLUME);

    if (Mix_Playing(active_long_channel)) {
        Mix_Volume(active_long_channel, target_vol);
    }
    if (progress >= 1.0f) {
        transitioning_long_sound = false;
    }
}

void SoundManager::stop_long_sequence(int fade_ms) {
    if (fade_ms > 0) {
        Mix_FadeOutChannel(CH_LONG_SEQUENCE_A, fade_ms);
        Mix_FadeOutChannel(CH_LONG_SEQUENCE_B, fade_ms);
    } else {
        Mix_HaltChannel(CH_LONG_SEQUENCE_A);
        Mix_HaltChannel(CH_LONG_SEQUENCE_B);
    }
    transitioning_long_sound = false;
}

bool SoundManager::is_long_sequence_busy() const {
    return Mix_Playing(CH_LONG_SEQUENCE_A) || Mix_Playing(CH_LONG_SEQUENCE_B) || transitioning_long_sound;
}

// --- Staged Rev ---

bool SoundManager::play_staged_rev(float gesture_peak_throttle, RevResult& out_result) {
    if (Mix_Playing(CH_STAGED_REV_SOUND)) {
        return false;
    }

    int rev_vol = to_mix_volume(0.9f * MASTER_VOLUME);
    const RevStage* selected = nullptr;

    if (sound_profile == "supra") {
        if (rev_stages.size() >= 3) {
            if (gesture_peak_throttle <= 0.4f) selected = &rev_stages[0];
            else if (gesture_peak_throttle <= 0.7f) selected = &rev_stages[1];
            else selected = &rev_stages[2];
        } else if (!rev_stages.empty()) {
            selected = &rev_stages[0];
        }

        if (selected && selected->sound) {
            Mix_Volume(CH_STAGED_REV_SOUND, rev_vol);
            Mix_PlayChannel(CH_STAGED_REV_SOUND, selected->sound, 0);
            out_result.duration = selected->duration;
            out_result.rpm_peak = 0;
            return true;
        }
    } else {
        if (!rev_stages.empty()) {
            if (gesture_peak_throttle <= 0.4f) {
                selected = &rev_stages[0];
            } else if (gesture_peak_throttle <= 0.75f) {
                selected = (rev_stages.size() > 1) ? &rev_stages[1] : &rev_stages[0];
            } else {
                selected = (rev_stages.size() > 2) ? &rev_stages[2] : &rev_stages.back();
            }
        }

        if (selected && selected->sound) {
            Mix_Volume(CH_STAGED_REV_SOUND, rev_vol);
            Mix_PlayChannel(CH_STAGED_REV_SOUND, selected->sound, 0);
            out_result.rpm_peak = selected->rpm_peak;
            out_result.duration = selected->duration;
            return true;
        }
    }

    return false;
}

// --- Global Controls ---

void SoundManager::stop_all_sounds(int fade_ms) {
    if (fade_ms > 0) {
        Mix_FadeOutChannel(-1, fade_ms); // -1 = all channels
    } else {
        Mix_HaltChannel(-1);
    }
}

bool SoundManager::is_startup_busy() const {
    return Mix_Playing(CH_STARTUP) != 0;
}

bool SoundManager::is_staged_rev_busy() const {
    return Mix_Playing(CH_STAGED_REV_SOUND) != 0;
}

void SoundManager::switch_profile(const std::string& new_profile) {
    std::cout << "\nSwitching from " << sound_profile << " to " << new_profile << "\n";
    sound_profile = new_profile;
    stop_all_sounds(100);
    load_sounds();
    play_startup();
}

// --- Launch Control Update ---

void SoundManager::update() {
    just_switched_to_lc_hold = false;

    Mix_Chunk* lc_engage = get_sound("launch_control_engage");
    Mix_Chunk* lc_hold = get_sound("launch_control_hold_loop");
    Mix_Chunk* current_sfx = Mix_GetChunk(CH_TURBO_LIMITER_SFX);

    if (waiting_for_launch_hold_loop) {
        if (!Mix_Playing(CH_TURBO_LIMITER_SFX) || current_sfx != lc_engage) {
            if (lc_hold) {
                Mix_Volume(CH_TURBO_LIMITER_SFX, to_mix_volume(MASTER_VOLUME));
                Mix_PlayChannel(CH_TURBO_LIMITER_SFX, lc_hold, -1);
                just_switched_to_lc_hold = true;
            } else {
                launch_control_sounds_active = false;
            }
            waiting_for_launch_hold_loop = false;
        }
    }

    if (launch_control_sounds_active && !just_switched_to_lc_hold) {
        Mix_Chunk* sfx_now = Mix_GetChunk(CH_TURBO_LIMITER_SFX);
        bool sfx_busy = Mix_Playing(CH_TURBO_LIMITER_SFX) != 0;

        if (!waiting_for_launch_hold_loop &&
            (!sfx_busy || (sfx_now != lc_engage && sfx_now != lc_hold))) {
            launch_control_sounds_active = false;
        }
    }
}
