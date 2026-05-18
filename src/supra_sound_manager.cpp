#include "supra_sound_manager.h"
#include "sound_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <SDL2/SDL.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int SupraSoundManager::to_mix_volume(float vol) {
    return static_cast<int>(std::clamp(vol, 0.0f, 1.0f) * MIX_MAX_VOLUME);
}

float SupraSoundManager::get_time_seconds() const {
    return SDL_GetTicks() / 1000.0f;
}

Mix_Chunk* SupraSoundManager::get_sound(const std::string& key) const {
    auto it = sounds.find(key);
    return (it != sounds.end()) ? it->second : nullptr;
}

std::string SupraSoundManager::pick_random_clip(const std::vector<std::string>& clips) {
    std::vector<std::string> available;
    for (const auto& c : clips) {
        if (get_sound(c)) available.push_back(c);
    }
    if (available.empty()) return "";
    std::uniform_int_distribution<int> dist(0, static_cast<int>(available.size()) - 1);
    return available[dist(rng)];
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

SupraSoundManager::SupraSoundManager()
    : rng(std::random_device{}())
{
    load_sounds();
}

SupraSoundManager::~SupraSoundManager() {
    free_all_sounds();
}

void SupraSoundManager::free_all_sounds() {
    rev_stages.clear();
    for (auto& [key, chunk] : sounds) {
        if (chunk) Mix_FreeChunk(chunk);
    }
    sounds.clear();
}

// ---------------------------------------------------------------------------
// Sound Loading
// ---------------------------------------------------------------------------

void SupraSoundManager::load_sounds() {
    std::printf("Supra Sound Manager: Loading sounds with parallel optimization...\n");

    std::vector<std::string> files = {
        "supra_idle_loop.wav",
        "supra_startup.wav",
        "light_pull_1.wav", "light_pull_2.wav",
        "light_cruise_1.wav", "light_cruise_2.wav", "light_cruise_3.wav",
        "aggressive_push_1.wav", "aggressive_push_2.wav", "aggressive_push_3.wav",
        "aggressive_push_4.wav", "aggressive_push_5.wav", "aggressive_push_6.wav",
        "violent_pull_1.wav", "violent_pull_2.wav", "violent_pull_3.wav",
        "highway_cruise_loop.wav",
        "supra_rev_stage1.wav", "supra_rev_stage2.wav",
        "supra_rev_stage3.wav", "supra_rev_stage4.wav",
        "supra_rev_1.wav", "supra_rev_2.wav", "supra_rev_3.wav"
    };

    OptimizedSoundLoader loader(SUPRA_SOUND_FILES_PATH);
    auto loaded = loader.load_sounds_parallel(files, 6);

    auto grab = [&](const std::string& loaded_key) -> Mix_Chunk* {
        auto it = loaded.find(loaded_key);
        return (it != loaded.end()) ? it->second.chunk : nullptr;
    };

    // Essential sounds
    sounds["idle"]    = grab("supra_idle_loop");
    sounds["startup"] = grab("supra_startup");

    // Check for missing essentials
    const char* essentials[] = { "supra_idle_loop", "supra_startup" };
    for (const char* e : essentials) {
        if (!loaded.count(e) || !loaded[e].chunk)
            std::printf("SUPRA CRITICAL ERROR: Missing essential sound file: %s.wav\n", e);
    }

    // Light pulls and cruising
    sounds["light_pull_1"]  = grab("light_pull_1");
    sounds["light_pull_2"]  = grab("light_pull_2");
    sounds["light_cruise_1"] = grab("light_cruise_1");
    sounds["light_cruise_2"] = grab("light_cruise_2");
    sounds["light_cruise_3"] = grab("light_cruise_3");

    // Aggressive pushes
    for (int i = 1; i <= 6; ++i)
        sounds["aggressive_push_" + std::to_string(i)] = grab("aggressive_push_" + std::to_string(i));

    // Violent pulls
    for (int i = 1; i <= 3; ++i)
        sounds["violent_pull_" + std::to_string(i)] = grab("violent_pull_" + std::to_string(i));

    // Highway cruise
    sounds["highway_cruise_loop"] = grab("highway_cruise_loop");

    // Original rev sounds (fallback)
    for (int i = 1; i <= 3; ++i)
        sounds["rev_" + std::to_string(i)] = grab("supra_rev_" + std::to_string(i));

    // Categorized clip lists
    light_pull_sounds      = { "light_pull_1", "light_pull_2" };
    light_cruise_sounds    = { "light_cruise_1", "light_cruise_2", "light_cruise_3" };
    aggressive_push_sounds = { "aggressive_push_1", "aggressive_push_2", "aggressive_push_3",
                               "aggressive_push_4", "aggressive_push_5", "aggressive_push_6" };
    violent_pull_sounds    = { "violent_pull_1", "violent_pull_2", "violent_pull_3" };

    // Staged rev sounds
    const char* stage_keys[] = {
        "supra_rev_stage1", "supra_rev_stage2",
        "supra_rev_stage3", "supra_rev_stage4"
    };
    constexpr int rpm_peaks[] = { 3500, 5500, 7500, 9000 };

    for (int i = 0; i < 4; ++i) {
        auto it = loaded.find(stage_keys[i]);
        if (it != loaded.end() && it->second.chunk) {
            SupraRevStage stage;
            stage.key      = std::string("supra_rev_stage") + std::to_string(i + 1);
            stage.sound    = it->second.chunk;
            stage.rpm_peak = rpm_peaks[i];
            stage.duration = it->second.duration;
            rev_stages.push_back(stage);
            sounds[stage.key] = it->second.chunk;
        }
    }

    int successful = 0;
    for (auto& [k, v] : sounds) { if (v) ++successful; }
    std::printf("SUPRA: Optimized loading complete. %d sounds loaded + %d rev stages ready.\n",
                successful, static_cast<int>(rev_stages.size()));
}

// ---------------------------------------------------------------------------
// Reverse Lookup
// ---------------------------------------------------------------------------

std::string SupraSoundManager::get_sound_name_from_obj(Mix_Chunk* sound_obj) const {
    if (!sound_obj) return "None";
    for (auto& [name, chunk] : sounds) {
        if (chunk == sound_obj) return name;
    }
    for (auto& stage : rev_stages) {
        if (stage.sound == sound_obj) return stage.key;
    }
    return "UnknownSoundObject";
}

// ---------------------------------------------------------------------------
// Idle
// ---------------------------------------------------------------------------

void SupraSoundManager::play_idle() {
    Mix_Chunk* idle_snd = get_sound("idle");
    if (!idle_snd) return;

    if (!Mix_Playing(SUPRA_CH_IDLE) || Mix_GetChunk(SUPRA_CH_IDLE) != idle_snd) {
        Mix_PlayChannel(SUPRA_CH_IDLE, idle_snd, -1);
    }
    idle_current_volume = idle_target_volume;
    Mix_Volume(SUPRA_CH_IDLE, to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
    idle_is_fading = std::abs(idle_current_volume - idle_target_volume) > 0.01f;
}

void SupraSoundManager::stop_idle() {
    Mix_HaltChannel(SUPRA_CH_IDLE);
    idle_is_fading = false;
}

void SupraSoundManager::set_idle_target_volume(float target, bool instant) {
    target = std::clamp(target, 0.0f, 1.0f);
    if (std::abs(idle_target_volume - target) > 0.01f || instant) {
        idle_target_volume = target;
        if (instant) {
            idle_current_volume = target;
            if (Mix_Playing(SUPRA_CH_IDLE))
                Mix_Volume(SUPRA_CH_IDLE, to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
            idle_is_fading = false;
        } else {
            if (std::abs(idle_current_volume - idle_target_volume) > 0.01f)
                idle_is_fading = true;
        }
    }
}

void SupraSoundManager::update_idle_fade(float dt) {
    if (!idle_is_fading || !Mix_Playing(SUPRA_CH_IDLE)) return;

    if (std::abs(idle_current_volume - idle_target_volume) < 0.01f) {
        idle_current_volume = idle_target_volume;
        idle_is_fading = false;
    } else if (idle_current_volume < idle_target_volume) {
        idle_current_volume = std::min(idle_current_volume + SUPRA_IDLE_TRANSITION_SPEED * dt,
                                       idle_target_volume);
    } else {
        idle_current_volume = std::max(idle_current_volume - SUPRA_IDLE_TRANSITION_SPEED * dt,
                                       idle_target_volume);
    }

    Mix_Volume(SUPRA_CH_IDLE, to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
}

// ---------------------------------------------------------------------------
// Throttle Range -- determines which clip category to pull from
// ---------------------------------------------------------------------------

std::string SupraSoundManager::get_throttle_range(float throttle) const {
    if (throttle >= SUPRA_HIGHWAY_CRUISE_THRESHOLD) return "highway";
    if (throttle >= 0.61f) return "violent";
    if (throttle >= 0.31f) return "aggressive";
    if (throttle >= 0.10f) return "light";
    return "idle";
}

// ---------------------------------------------------------------------------
// Driving Sounds -- throttle-range clip selection with optional A/B crossfade
//   force_type: "pull", "push", "cruise", "highway_cruise", or "" (auto)
//   crossfade:  if true, use crossfade instead of hard cut
// ---------------------------------------------------------------------------

bool SupraSoundManager::play_driving_sound(float throttle_pct,
                                            const std::string& force_type,
                                            bool crossfade) {
    float now = get_time_seconds();

    if (!crossfade) {
        if (now - last_clip_start_time < SUPRA_CLIP_OVERLAP_PREVENTION_TIME)
            return false;
        if (Mix_Playing(SUPRA_CH_DRIVING_A) || Mix_Playing(SUPRA_CH_DRIVING_B))
            return false;
    }

    std::string throttle_range = get_throttle_range(throttle_pct);
    Mix_Chunk* selected_sound = nullptr;
    std::string sound_name;
    std::string sound_type = force_type.empty() ? "pull" : force_type;

    if (sound_type == "highway_cruise") {
        Mix_Chunk* hw = get_sound("highway_cruise_loop");
        if (hw) { selected_sound = hw; sound_name = "highway_cruise_loop"; }

    } else if (sound_type == "cruise") {
        if (throttle_range == "highway") {
            Mix_Chunk* hw = get_sound("highway_cruise_loop");
            if (hw) { selected_sound = hw; sound_name = "highway_cruise_loop"; sound_type = "highway_cruise"; }
        } else {
            sound_name = pick_random_clip(light_cruise_sounds);
            if (!sound_name.empty()) selected_sound = get_sound(sound_name);
        }

    } else {
        // pull / push -- pick from the range-appropriate category
        std::string chosen;
        if (throttle_range == "light") {
            chosen = pick_random_clip(light_pull_sounds);
        } else if (throttle_range == "aggressive") {
            chosen = pick_random_clip(aggressive_push_sounds);
        } else if (throttle_range == "violent" || throttle_range == "highway") {
            chosen = pick_random_clip(violent_pull_sounds);
        }
        if (!chosen.empty()) {
            sound_name = chosen;
            selected_sound = get_sound(chosen);
        }
    }

    if (!selected_sound) return false;

    // Determine target channel (alternate between A and B)
    int target_ch = (active_driving_channel == SUPRA_CH_DRIVING_A)
                        ? SUPRA_CH_DRIVING_B : SUPRA_CH_DRIVING_A;

    int loops = (sound_type == "cruise" || sound_type == "highway_cruise") ? -1 : 0;

    if (crossfade) {
        int fade_out_ch = active_driving_channel;
        int fade_in_ch  = target_ch;

        Mix_FadeOutChannel(fade_out_ch, SUPRA_CROSSFADE_DURATION_MS);
        Mix_Volume(fade_in_ch, 0);
        Mix_PlayChannel(fade_in_ch, selected_sound, loops);

        active_driving_channel = fade_in_ch;
        transitioning_driving_sound = true;
        transition_start_time = now;

        std::printf("Supra crossfading to: %s (%s) at %.2f\n",
                    sound_name.c_str(), sound_type.c_str(), throttle_pct);
    } else {
        Mix_Volume(target_ch, to_mix_volume(MASTER_ENGINE_VOL));
        Mix_PlayChannel(target_ch, selected_sound, loops);
        active_driving_channel = target_ch;

        std::printf("Supra playing: %s (%s) at %.2f\n",
                    sound_name.c_str(), sound_type.c_str(), throttle_pct);
    }

    last_clip_start_time = now;
    return true;
}

// ---------------------------------------------------------------------------
// Driving Crossfade Update -- ramps the new channel from 0 to full volume
// ---------------------------------------------------------------------------

void SupraSoundManager::update_driving_crossfade() {
    if (!transitioning_driving_sound) return;

    float elapsed_ms = (get_time_seconds() - transition_start_time) * 1000.0f;
    float progress   = std::min(1.0f, elapsed_ms / static_cast<float>(SUPRA_CROSSFADE_DURATION_MS));

    if (Mix_Playing(active_driving_channel))
        Mix_Volume(active_driving_channel, to_mix_volume(progress * MASTER_ENGINE_VOL));

    if (progress >= 1.0f) {
        transitioning_driving_sound = false;
        if (Mix_Playing(active_driving_channel))
            Mix_Volume(active_driving_channel, to_mix_volume(MASTER_ENGINE_VOL));
    }
}

bool SupraSoundManager::is_driving_sound_busy() const {
    return Mix_Playing(SUPRA_CH_DRIVING_A) ||
           Mix_Playing(SUPRA_CH_DRIVING_B) ||
           transitioning_driving_sound;
}

void SupraSoundManager::stop_driving_sounds(int fade_ms) {
    if (fade_ms > 0) {
        Mix_FadeOutChannel(SUPRA_CH_DRIVING_A, fade_ms);
        Mix_FadeOutChannel(SUPRA_CH_DRIVING_B, fade_ms);
    } else {
        Mix_HaltChannel(SUPRA_CH_DRIVING_A);
        Mix_HaltChannel(SUPRA_CH_DRIVING_B);
    }
    transitioning_driving_sound = false;
}

// ---------------------------------------------------------------------------
// Staged Revs (RPM-aware) -- same selection logic as M4 but with Supra
// RPM peaks and constants.
// ---------------------------------------------------------------------------

SupraRevResult* SupraSoundManager::play_staged_rev(float current_rpm,
                                                    float gesture_peak_throttle) {
    if (Mix_Playing(SUPRA_CH_REV_SFX)) return nullptr;
    if (rev_stages.empty()) return nullptr;

    if (gesture_peak_throttle <= 0.1f) {
        if (current_rpm > SUPRA_RPM_IDLE + 500.0f) return nullptr;
    }

    const SupraRevStage* selected = nullptr;
    int n = static_cast<int>(rev_stages.size());

    if (current_rpm < rev_stages[0].rpm_peak * 0.8f) {
        if (gesture_peak_throttle < 0.4f)
            selected = &rev_stages[0];
        else if (gesture_peak_throttle < 0.75f)
            selected = (n > 1) ? &rev_stages[1] : &rev_stages[0];
        else
            selected = (n > 2) ? &rev_stages[2] : &rev_stages[std::min(1, n - 1)];
    } else if (n > 1 && current_rpm < rev_stages[1].rpm_peak * 0.8f) {
        if (gesture_peak_throttle < 0.5f)
            selected = (n > 1) ? &rev_stages[1] : &rev_stages[0];
        else
            selected = (n > 2) ? &rev_stages[2] : &rev_stages[std::min(1, n - 1)];
    } else if (n > 2 && current_rpm < rev_stages[2].rpm_peak * 0.9f) {
        if (gesture_peak_throttle < 0.6f)
            selected = (n > 2) ? &rev_stages[2] : &rev_stages[n - 1];
        else
            selected = (n > 3) ? &rev_stages[3] : &rev_stages[n - 1];
    } else {
        selected = (n > 3) ? &rev_stages[3] : &rev_stages[n - 1];
    }

    if (!selected || !selected->sound) return nullptr;

    // Promote upward if selected peak is below current RPM
    if (selected->rpm_peak < static_cast<int>(current_rpm) &&
        selected != &rev_stages.back()) {
        for (auto& stage : rev_stages) {
            if (stage.rpm_peak >= static_cast<int>(current_rpm)) {
                selected = &stage;
                break;
            }
        }
        if (selected->rpm_peak < static_cast<int>(current_rpm))
            selected = &rev_stages.back();
    }

    std::printf("Supra Playing rev: %s (Peak: %d) | CurrentRPM: %.0f | Gesture: %.2f\n",
                selected->key.c_str(), selected->rpm_peak,
                current_rpm, gesture_peak_throttle);

    Mix_Volume(SUPRA_CH_REV_SFX,
               to_mix_volume(SUPRA_STAGED_REV_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SUPRA_CH_REV_SFX, selected->sound, 0);

    last_rev_result.key      = selected->key;
    last_rev_result.rpm_peak = selected->rpm_peak;
    last_rev_result.duration = selected->duration;
    return &last_rev_result;
}

// ---------------------------------------------------------------------------
// Simple Rev (fallback) -- picks rev_1/2/3 based on throttle intensity
// ---------------------------------------------------------------------------

bool SupraSoundManager::play_rev_sound(float peak_throttle) {
    if (Mix_Playing(SUPRA_CH_REV_SFX)) return false;

    std::string sound_name;
    if (peak_throttle < 0.4f)       sound_name = "rev_1";
    else if (peak_throttle < 0.7f)  sound_name = "rev_2";
    else                             sound_name = "rev_3";

    Mix_Chunk* snd = get_sound(sound_name);
    if (!snd) return false;

    std::printf("Supra playing rev: %s (peak throttle: %.2f)\n",
                sound_name.c_str(), peak_throttle);
    Mix_Volume(SUPRA_CH_REV_SFX, to_mix_volume(MASTER_ENGINE_VOL));
    Mix_PlayChannel(SUPRA_CH_REV_SFX, snd, 0);
    return true;
}

bool SupraSoundManager::is_rev_sound_busy() const {
    return Mix_Playing(SUPRA_CH_REV_SFX) != 0;
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

bool SupraSoundManager::play_startup_sound() {
    Mix_Chunk* snd = get_sound("startup");
    if (!snd) return false;

    Mix_HaltChannel(SUPRA_CH_REV_SFX);
    Mix_Volume(SUPRA_CH_REV_SFX,
               to_mix_volume(SUPRA_NORMAL_IDLE_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SUPRA_CH_REV_SFX, snd, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Global Controls
// ---------------------------------------------------------------------------

void SupraSoundManager::stop_all_sounds() {
    Mix_HaltChannel(SUPRA_CH_IDLE);
    Mix_HaltChannel(SUPRA_CH_DRIVING_A);
    Mix_HaltChannel(SUPRA_CH_DRIVING_B);
    Mix_HaltChannel(SUPRA_CH_REV_SFX);
    idle_is_fading = false;
    transitioning_driving_sound = false;
}

void SupraSoundManager::fade_out_all_sounds(int fade_ms) {
    if (Mix_Playing(SUPRA_CH_IDLE))      Mix_FadeOutChannel(SUPRA_CH_IDLE, fade_ms);
    if (Mix_Playing(SUPRA_CH_DRIVING_A)) Mix_FadeOutChannel(SUPRA_CH_DRIVING_A, fade_ms);
    if (Mix_Playing(SUPRA_CH_DRIVING_B)) Mix_FadeOutChannel(SUPRA_CH_DRIVING_B, fade_ms);
    if (Mix_Playing(SUPRA_CH_REV_SFX))  Mix_FadeOutChannel(SUPRA_CH_REV_SFX, fade_ms);
}
