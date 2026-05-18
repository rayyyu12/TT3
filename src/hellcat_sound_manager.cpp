#include "hellcat_sound_manager.h"
#include "sound_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <SDL2/SDL.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int HellcatSoundManager::to_mix_volume(float vol) {
    return static_cast<int>(std::clamp(vol, 0.0f, 1.0f) * MIX_MAX_VOLUME);
}

float HellcatSoundManager::get_time_seconds() const {
    return SDL_GetTicks() / 1000.0f;
}

Mix_Chunk* HellcatSoundManager::get_sound(const std::string& key) const {
    auto it = sounds.find(key);
    return (it != sounds.end()) ? it->second : nullptr;
}

float HellcatSoundManager::power_curve(float input_val, float power) const {
    return std::pow(std::clamp(input_val, 0.0f, 1.0f), power);
}

void HellcatSoundManager::equal_power_crossfade(float progress,
                                                  float& fade_out,
                                                  float& fade_in) const {
    constexpr float half_pi = 3.14159265f / 2.0f;
    fade_out = std::cos(progress * half_pi);
    fade_in  = std::sin(progress * half_pi);
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

HellcatSoundManager::HellcatSoundManager()
    : rng(std::random_device{}())
{
    load_sounds();
}

HellcatSoundManager::~HellcatSoundManager() {
    free_all_sounds();
}

void HellcatSoundManager::free_all_sounds() {
    for (auto& [key, chunk] : sounds) {
        if (chunk) Mix_FreeChunk(chunk);
    }
    sounds.clear();
}

// ---------------------------------------------------------------------------
// Sound Loading (uses OptimizedSoundLoader for parallel I/O)
// ---------------------------------------------------------------------------

void HellcatSoundManager::load_sounds() {
    std::printf("Hellcat Sound Manager: Loading sounds with parallel optimization...\n");

    std::vector<std::string> files = {
        "hellcat_idle_loop.wav",
        "hellcat_rumble_low_rpm_loop.wav",
        "hellcat_rumble_mid_rpm_loop.wav",
        "hellcat_whine_low_rpm_loop.wav",
        "hellcat_whine_high_rpm_loop.wav",
        "hellcat_exhaust_roar.wav",
        "hellcat_decel_burble.wav",
        "hellcat_startup_roar.wav",
        "hellcat_upshift_bark.wav",
        "hellcat_downshift_revmatch1.wav",
        "hellcat_downshift_revmatch2.wav",
        "hellcat_rev_1.wav",
        "hellcat_rev_2.wav",
        "hellcat_rev_3.wav"
    };

    OptimizedSoundLoader loader(HELLCAT_SOUND_FILES_PATH);
    auto loaded = loader.load_sounds_parallel(files, 4);

    auto grab = [&](const std::string& loaded_key) -> Mix_Chunk* {
        auto it = loaded.find(loaded_key);
        return (it != loaded.end()) ? it->second.chunk : nullptr;
    };

    sounds["idle"]         = grab("hellcat_idle_loop");
    sounds["rumble_low"]   = grab("hellcat_rumble_low_rpm_loop");
    sounds["rumble_mid"]   = grab("hellcat_rumble_mid_rpm_loop");
    sounds["whine_low"]    = grab("hellcat_whine_low_rpm_loop");
    sounds["whine_high"]   = grab("hellcat_whine_high_rpm_loop");
    sounds["exhaust_roar"] = grab("hellcat_exhaust_roar");
    sounds["decel_burble"] = grab("hellcat_decel_burble");
    sounds["startup"]      = grab("hellcat_startup_roar");
    sounds["upshift"]      = grab("hellcat_upshift_bark");
    sounds["downshift_1"]  = grab("hellcat_downshift_revmatch1");
    sounds["downshift_2"]  = grab("hellcat_downshift_revmatch2");
    sounds["rev_1"]        = grab("hellcat_rev_1");
    sounds["rev_2"]        = grab("hellcat_rev_2");
    sounds["rev_3"]        = grab("hellcat_rev_3");

    auto wl = loaded.find("hellcat_whine_low_rpm_loop");
    if (wl != loaded.end() && wl->second.duration > 0.0f)
        whine_low_duration = wl->second.duration;

    auto wh = loaded.find("hellcat_whine_high_rpm_loop");
    if (wh != loaded.end() && wh->second.duration > 0.0f)
        whine_high_duration = wh->second.duration;

    int successful = 0;
    for (auto& [k, v] : sounds) { if (v) ++successful; }
    std::printf("Hellcat: Optimized loading complete. %d sounds ready.\n", successful);
}

// ---------------------------------------------------------------------------
// Idle Volume Control
// ---------------------------------------------------------------------------

void HellcatSoundManager::set_idle_target_volume(float target, bool instant) {
    target = std::clamp(target, 0.0f, 1.0f);
    if (std::abs(idle_target_volume - target) > 0.01f || instant) {
        idle_target_volume = target;
        if (instant) {
            idle_current_volume = target;
            if (Mix_Playing(HELLCAT_CH_IDLE))
                Mix_Volume(HELLCAT_CH_IDLE,
                           to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
            idle_is_fading = false;
        } else {
            if (std::abs(idle_current_volume - idle_target_volume) > 0.01f)
                idle_is_fading = true;
        }
    }
}

void HellcatSoundManager::update_idle_fade(float dt) {
    if (!idle_is_fading || !Mix_Playing(HELLCAT_CH_IDLE)) return;

    if (std::abs(idle_current_volume - idle_target_volume) < 0.01f) {
        idle_current_volume = idle_target_volume;
        idle_is_fading = false;
    } else if (idle_current_volume < idle_target_volume) {
        idle_current_volume = std::min(
            idle_current_volume + SUPRA_IDLE_TRANSITION_SPEED * dt,
            idle_target_volume);
    } else {
        idle_current_volume = std::max(
            idle_current_volume - SUPRA_IDLE_TRANSITION_SPEED * dt,
            idle_target_volume);
    }

    Mix_Volume(HELLCAT_CH_IDLE,
               to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
}

// ---------------------------------------------------------------------------
// Audio Inertia -- volumes chase targets smoothly
// ---------------------------------------------------------------------------

void HellcatSoundManager::update_audio_inertia(float dt) {
    auto chase = [&](float& current, float target) {
        if (std::abs(current - target) > 0.01f) {
            if (current < target)
                current = std::min(current + HELLCAT_AUDIO_INERTIA_SPEED * dt,
                                   target);
            else
                current = std::max(current - HELLCAT_AUDIO_INERTIA_SPEED * dt,
                                   target);
        }
    };

    chase(rumble_low_current_vol,  rumble_low_target_vol);
    chase(rumble_mid_current_vol,  rumble_mid_target_vol);
    chase(whine_low_current_vol,   whine_low_target_vol);
    chase(whine_high_current_vol,  whine_high_target_vol);
}

// ---------------------------------------------------------------------------
// Foundation Layer
//   Idle + Rumble low/mid with equal-power crossfade + Supercharger whine
// ---------------------------------------------------------------------------

void HellcatSoundManager::play_foundation_layer(float smoothed_throttle,
                                                  float simulated_rpm) {
    // Idle sound
    Mix_Chunk* idle_snd = get_sound("idle");
    if (idle_snd) {
        if (!Mix_Playing(HELLCAT_CH_IDLE) ||
            Mix_GetChunk(HELLCAT_CH_IDLE) != idle_snd)
            Mix_PlayChannel(HELLCAT_CH_IDLE, idle_snd, -1);

        float idle_volume;
        if (smoothed_throttle <= HELLCAT_THROTTLE_IDLE_THRESHOLD)
            idle_volume = 1.0f;
        else
            idle_volume = std::max(0.0f,
                1.0f - (smoothed_throttle - HELLCAT_THROTTLE_IDLE_THRESHOLD) / 0.1f);

        Mix_Volume(HELLCAT_CH_IDLE,
                   to_mix_volume(idle_volume * idle_current_volume * MASTER_ENGINE_VOL));
    }

    // Rumble with audio inertia and equal-power crossfading
    Mix_Chunk* rumble_low = get_sound("rumble_low");
    Mix_Chunk* rumble_mid = get_sound("rumble_mid");

    if (smoothed_throttle > HELLCAT_THROTTLE_IDLE_THRESHOLD) {
        constexpr float crossfade_start = 0.4f;
        constexpr float crossfade_end   = 0.6f;

        if (smoothed_throttle < crossfade_start) {
            float low_progress =
                (smoothed_throttle - HELLCAT_THROTTLE_IDLE_THRESHOLD) /
                (crossfade_start - HELLCAT_THROTTLE_IDLE_THRESHOLD);
            rumble_low_target_vol = power_curve(low_progress, 1.8f);
            rumble_mid_target_vol = 0.0f;

        } else if (smoothed_throttle > crossfade_end) {
            float mid_progress =
                (smoothed_throttle - crossfade_end) / (1.0f - crossfade_end);
            rumble_low_target_vol = 0.0f;
            rumble_mid_target_vol = power_curve(mid_progress, 2.5f);

        } else {
            float fade_progress =
                (smoothed_throttle - crossfade_start) /
                (crossfade_end - crossfade_start);
            float fade_out, fade_in;
            equal_power_crossfade(fade_progress, fade_out, fade_in);

            float base_progress =
                (smoothed_throttle - HELLCAT_THROTTLE_IDLE_THRESHOLD) /
                (1.0f - HELLCAT_THROTTLE_IDLE_THRESHOLD);
            float base_vol = power_curve(base_progress, 2.0f);

            rumble_low_target_vol = base_vol * fade_out;
            rumble_mid_target_vol = base_vol * fade_in;
        }

        if (rumble_low && rumble_low_target_vol > 0.01f) {
            if (!Mix_Playing(HELLCAT_CH_RUMBLE_LOW))
                Mix_PlayChannel(HELLCAT_CH_RUMBLE_LOW, rumble_low, -1);
            Mix_Volume(HELLCAT_CH_RUMBLE_LOW,
                       to_mix_volume(rumble_low_current_vol * MASTER_ENGINE_VOL));
        } else {
            Mix_HaltChannel(HELLCAT_CH_RUMBLE_LOW);
        }

        if (rumble_mid && rumble_mid_target_vol > 0.01f) {
            if (!Mix_Playing(HELLCAT_CH_RUMBLE_MID))
                Mix_PlayChannel(HELLCAT_CH_RUMBLE_MID, rumble_mid, -1);
            Mix_Volume(HELLCAT_CH_RUMBLE_MID,
                       to_mix_volume(rumble_mid_current_vol * MASTER_ENGINE_VOL));
        } else {
            Mix_HaltChannel(HELLCAT_CH_RUMBLE_MID);
        }
    } else {
        rumble_low_target_vol = 0.0f;
        rumble_mid_target_vol = 0.0f;
        if (rumble_low_current_vol < 0.01f) Mix_HaltChannel(HELLCAT_CH_RUMBLE_LOW);
        if (rumble_mid_current_vol < 0.01f) Mix_HaltChannel(HELLCAT_CH_RUMBLE_MID);
    }

    update_whine_sounds_enhanced(smoothed_throttle, simulated_rpm);
}

// ---------------------------------------------------------------------------
// Supercharger Whine (A/B crossfade near end of clip)
// ---------------------------------------------------------------------------

void HellcatSoundManager::update_whine_sounds_enhanced(float smoothed_throttle,
                                                        float simulated_rpm) {
    Mix_Chunk* whine_low_snd  = get_sound("whine_low");
    Mix_Chunk* whine_high_snd = get_sound("whine_high");

    if (smoothed_throttle > HELLCAT_THROTTLE_IDLE_THRESHOLD) {
        float base_intensity = power_curve(smoothed_throttle, 1.6f);
        float rpm_factor = std::min(1.0f,
            (simulated_rpm - HELLCAT_IDLE_RPM) /
            (HELLCAT_REDLINE_RPM - HELLCAT_IDLE_RPM));
        float rpm_boost = 0.3f * power_curve(rpm_factor, 1.2f);

        if (whine_low_snd) {
            float low_throttle_factor =
                std::max(0.0f, 1.0f - (smoothed_throttle - 0.3f) / 0.4f);
            whine_low_target_vol =
                (base_intensity + rpm_boost) * low_throttle_factor * 0.8f;
            manage_whine_crossfade(true, whine_low_snd);
            Mix_Volume(whine_low_active_channel,
                       to_mix_volume(whine_low_current_vol * MASTER_ENGINE_VOL));
        }

        if (whine_high_snd && smoothed_throttle > 0.4f) {
            float high_progress = (smoothed_throttle - 0.4f) / 0.6f;
            float high_intensity = power_curve(high_progress, 2.0f);
            whine_high_target_vol = (high_intensity + rpm_boost) * 1.0f;
            manage_whine_crossfade(false, whine_high_snd);
            Mix_Volume(whine_high_active_channel,
                       to_mix_volume(whine_high_current_vol * MASTER_ENGINE_VOL));
        } else {
            whine_high_target_vol = 0.0f;
            if (whine_high_current_vol < 0.01f) {
                Mix_HaltChannel(HELLCAT_CH_WHINE_HIGH_A);
                Mix_HaltChannel(HELLCAT_CH_WHINE_HIGH_B);
            }
        }
    } else {
        whine_low_target_vol  = 0.0f;
        whine_high_target_vol = 0.0f;
        if (whine_low_current_vol < 0.01f) {
            Mix_HaltChannel(HELLCAT_CH_WHINE_LOW_A);
            Mix_HaltChannel(HELLCAT_CH_WHINE_LOW_B);
        }
        if (whine_high_current_vol < 0.01f) {
            Mix_HaltChannel(HELLCAT_CH_WHINE_HIGH_A);
            Mix_HaltChannel(HELLCAT_CH_WHINE_HIGH_B);
        }
    }
}

void HellcatSoundManager::manage_whine_crossfade(bool is_low,
                                                   Mix_Chunk* sound) {
    float now = get_time_seconds();

    int   active_ch    = is_low ? whine_low_active_channel
                                : whine_high_active_channel;
    int   inactive_ch  = is_low
        ? ((active_ch == HELLCAT_CH_WHINE_LOW_A)
               ? HELLCAT_CH_WHINE_LOW_B : HELLCAT_CH_WHINE_LOW_A)
        : ((active_ch == HELLCAT_CH_WHINE_HIGH_A)
               ? HELLCAT_CH_WHINE_HIGH_B : HELLCAT_CH_WHINE_HIGH_A);

    bool&  crossfading = is_low ? whine_low_crossfading
                                : whine_high_crossfading;
    float& cf_start    = is_low ? whine_low_crossfade_start_time
                                : whine_high_crossfade_start_time;
    float& play_start  = is_low ? whine_low_play_start
                                : whine_high_play_start;
    int&   active_ref  = is_low ? whine_low_active_channel
                                : whine_high_active_channel;
    float  snd_length  = is_low ? whine_low_duration
                                : whine_high_duration;

    if (!Mix_Playing(active_ch)) {
        Mix_PlayChannel(active_ch, sound, -1);
        play_start = now;
    }

    if (!crossfading && Mix_Playing(active_ch)) {
        float crossfade_trigger = snd_length - 0.5f;
        float play_time = now - play_start;
        if (play_time >= crossfade_trigger) {
            Mix_Volume(inactive_ch, 0);
            Mix_PlayChannel(inactive_ch, sound, -1);
            crossfading = true;
            cf_start = now;
        }
    }

    if (crossfading) {
        float elapsed_ms = (now - cf_start) * 1000.0f;
        float progress = std::min(1.0f,
            elapsed_ms / static_cast<float>(HELLCAT_CROSSFADE_DURATION));
        Mix_Volume(inactive_ch,
                   to_mix_volume(progress * MASTER_ENGINE_VOL));

        if (progress >= 1.0f) {
            Mix_HaltChannel(active_ch);
            active_ref = inactive_ch;
            play_start = cf_start;
            crossfading = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Character Layer (exhaust roar on accel, decel burble on throttle release)
// ---------------------------------------------------------------------------

void HellcatSoundManager::play_character_layer(float engine_load,
                                                float simulated_rpm,
                                                float smoothed_throttle) {
    if (engine_load > 0.05f) {
        Mix_Chunk* exhaust_roar = get_sound("exhaust_roar");
        if (exhaust_roar && !Mix_Playing(HELLCAT_CH_ACCEL_RESPONSE)) {
            float volume = std::min(1.0f, engine_load * 2.0f);
            Mix_Volume(HELLCAT_CH_ACCEL_RESPONSE,
                       to_mix_volume(volume * MASTER_ENGINE_VOL));
            Mix_PlayChannel(HELLCAT_CH_ACCEL_RESPONSE, exhaust_roar, 0);
            accel_response_fading = true;
            accel_response_fade_start = get_time_seconds();
        }
    }

    if (engine_load < -0.05f) {
        Mix_Chunk* decel_burble = get_sound("decel_burble");
        if (decel_burble && !Mix_Playing(HELLCAT_CH_DECEL_BURBLE)) {
            float volume = std::min(0.8f, std::abs(engine_load) * 1.5f);
            float rpm_factor = std::min(1.0f, simulated_rpm / 3000.0f);
            Mix_Volume(HELLCAT_CH_DECEL_BURBLE,
                       to_mix_volume(volume * rpm_factor * MASTER_ENGINE_VOL));
            Mix_PlayChannel(HELLCAT_CH_DECEL_BURBLE, decel_burble, -1);
        }
    } else {
        if (Mix_Playing(HELLCAT_CH_DECEL_BURBLE))
            Mix_FadeOutChannel(HELLCAT_CH_DECEL_BURBLE, 200);
    }
}

// ---------------------------------------------------------------------------
// SFX Layer -- Startup, Upshift, Downshift
// ---------------------------------------------------------------------------

bool HellcatSoundManager::play_startup_sound() {
    Mix_Chunk* snd = get_sound("startup");
    if (!snd) return false;

    Mix_HaltChannel(HELLCAT_CH_STARTUP);
    Mix_Volume(HELLCAT_CH_STARTUP,
               to_mix_volume(HELLCAT_NORMAL_IDLE_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(HELLCAT_CH_STARTUP, snd, 0);
    return true;
}

bool HellcatSoundManager::play_upshift_sound() {
    Mix_Chunk* snd = get_sound("upshift");
    if (!snd) return false;

    Mix_HaltChannel(HELLCAT_CH_SHIFT_SFX);
    Mix_Volume(HELLCAT_CH_SHIFT_SFX, to_mix_volume(MASTER_ENGINE_VOL));
    Mix_PlayChannel(HELLCAT_CH_SHIFT_SFX, snd, 0);
    return true;
}

bool HellcatSoundManager::play_downshift_sound() {
    const std::string options[] = { "downshift_1", "downshift_2" };
    std::uniform_int_distribution<int> dist(0, 1);
    const std::string& selected = options[dist(rng)];

    Mix_Chunk* snd = get_sound(selected);
    if (!snd) return false;

    Mix_HaltChannel(HELLCAT_CH_SHIFT_SFX);
    Mix_Volume(HELLCAT_CH_SHIFT_SFX, to_mix_volume(MASTER_ENGINE_VOL));
    Mix_PlayChannel(HELLCAT_CH_SHIFT_SFX, snd, 0);
    return true;
}

bool HellcatSoundManager::is_startup_busy() const {
    return Mix_Playing(HELLCAT_CH_STARTUP) != 0;
}

bool HellcatSoundManager::is_shift_busy() const {
    return Mix_Playing(HELLCAT_CH_SHIFT_SFX) != 0;
}

// ---------------------------------------------------------------------------
// Rev Queue (queue up to 3 revs, process when channel free)
// ---------------------------------------------------------------------------

bool HellcatSoundManager::play_simple_rev() {
    const std::string options[] = { "rev_1", "rev_2", "rev_3" };
    std::uniform_int_distribution<int> dist(0, 2);
    const std::string& selected = options[dist(rng)];

    if (!Mix_Playing(HELLCAT_CH_SHIFT_SFX))
        return play_rev_now(selected);

    if (static_cast<int>(rev_queue.size()) < max_rev_queue_size) {
        rev_queue.push_back(selected);
        return true;
    }
    return false;
}

bool HellcatSoundManager::play_rev_now(const std::string& sound_key) {
    Mix_Chunk* snd = get_sound(sound_key);
    if (!snd) return false;

    Mix_HaltChannel(HELLCAT_CH_SHIFT_SFX);
    Mix_Volume(HELLCAT_CH_SHIFT_SFX, to_mix_volume(MASTER_ENGINE_VOL));
    Mix_PlayChannel(HELLCAT_CH_SHIFT_SFX, snd, 0);
    return true;
}

void HellcatSoundManager::process_rev_queue() {
    if (!Mix_Playing(HELLCAT_CH_SHIFT_SFX) && !rev_queue.empty()) {
        std::string next = rev_queue.front();
        rev_queue.erase(rev_queue.begin());
        play_rev_now(next);
    }
}

bool HellcatSoundManager::is_rev_busy() const {
    return Mix_Playing(HELLCAT_CH_SHIFT_SFX) != 0;
}

void HellcatSoundManager::clear_rev_queue() {
    rev_queue.clear();
}

// ---------------------------------------------------------------------------
// update() -- called every frame
// ---------------------------------------------------------------------------

void HellcatSoundManager::update(float dt) {
    update_idle_fade(dt);
    process_rev_queue();
    update_audio_inertia(dt);

    if (accel_response_fading && Mix_Playing(HELLCAT_CH_ACCEL_RESPONSE)) {
        float now = get_time_seconds();
        float fade_duration = HELLCAT_FADE_OUT_DURATION / 1000.0f;
        float elapsed = now - accel_response_fade_start;

        if (elapsed >= fade_duration) {
            accel_response_fading = false;
        } else {
            float fade_progress = elapsed / fade_duration;
            if (fade_progress > 0.5f) {
                float fade_vol =
                    std::max(0.0f, 1.0f - ((fade_progress - 0.5f) * 2.0f));
                Mix_Volume(HELLCAT_CH_ACCEL_RESPONSE,
                           to_mix_volume(fade_vol * MASTER_ENGINE_VOL));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Global Controls
// ---------------------------------------------------------------------------

void HellcatSoundManager::stop_all_sounds() {
    Mix_HaltChannel(HELLCAT_CH_IDLE);
    Mix_HaltChannel(HELLCAT_CH_RUMBLE_LOW);
    Mix_HaltChannel(HELLCAT_CH_RUMBLE_MID);
    Mix_HaltChannel(HELLCAT_CH_WHINE_LOW_A);
    Mix_HaltChannel(HELLCAT_CH_WHINE_LOW_B);
    Mix_HaltChannel(HELLCAT_CH_WHINE_HIGH_A);
    Mix_HaltChannel(HELLCAT_CH_WHINE_HIGH_B);
    Mix_HaltChannel(HELLCAT_CH_ACCEL_RESPONSE);
    Mix_HaltChannel(HELLCAT_CH_DECEL_BURBLE);
    Mix_HaltChannel(HELLCAT_CH_STARTUP);
    Mix_HaltChannel(HELLCAT_CH_SHIFT_SFX);

    idle_is_fading         = false;
    whine_low_crossfading  = false;
    whine_high_crossfading = false;
    accel_response_fading  = false;
}

void HellcatSoundManager::fade_out_all_sounds(int fade_ms) {
    if (Mix_Playing(HELLCAT_CH_IDLE))           Mix_FadeOutChannel(HELLCAT_CH_IDLE, fade_ms);
    if (Mix_Playing(HELLCAT_CH_RUMBLE_LOW))     Mix_FadeOutChannel(HELLCAT_CH_RUMBLE_LOW, fade_ms);
    if (Mix_Playing(HELLCAT_CH_RUMBLE_MID))     Mix_FadeOutChannel(HELLCAT_CH_RUMBLE_MID, fade_ms);
    if (Mix_Playing(HELLCAT_CH_WHINE_LOW_A))    Mix_FadeOutChannel(HELLCAT_CH_WHINE_LOW_A, fade_ms);
    if (Mix_Playing(HELLCAT_CH_WHINE_LOW_B))    Mix_FadeOutChannel(HELLCAT_CH_WHINE_LOW_B, fade_ms);
    if (Mix_Playing(HELLCAT_CH_WHINE_HIGH_A))   Mix_FadeOutChannel(HELLCAT_CH_WHINE_HIGH_A, fade_ms);
    if (Mix_Playing(HELLCAT_CH_WHINE_HIGH_B))   Mix_FadeOutChannel(HELLCAT_CH_WHINE_HIGH_B, fade_ms);
    if (Mix_Playing(HELLCAT_CH_ACCEL_RESPONSE)) Mix_FadeOutChannel(HELLCAT_CH_ACCEL_RESPONSE, fade_ms);
    if (Mix_Playing(HELLCAT_CH_DECEL_BURBLE))   Mix_FadeOutChannel(HELLCAT_CH_DECEL_BURBLE, fade_ms);
    if (Mix_Playing(HELLCAT_CH_STARTUP))        Mix_FadeOutChannel(HELLCAT_CH_STARTUP, fade_ms);
    if (Mix_Playing(HELLCAT_CH_SHIFT_SFX))      Mix_FadeOutChannel(HELLCAT_CH_SHIFT_SFX, fade_ms);

    clear_rev_queue();
}
