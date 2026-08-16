#include "svj_sound_manager.h"
#include "sound_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <SDL2/SDL.h>

namespace {

constexpr float kHalfPi = 1.57079632679f;

/** Length in seconds from Mix_Chunk bytes (matches OptimizedSoundLoader decode). */
inline float duration_from_chunk_bytes(Mix_Chunk* chunk) {
    if (!chunk || chunk->alen <= 0) return 0.0f;
    constexpr int bytes_per_sample = 2;
    const float frames = static_cast<float>(chunk->alen)
        / static_cast<float>(bytes_per_sample * MIXER_CHANNELS_STEREO);
    return frames / static_cast<float>(MIXER_FREQUENCY);
}

// Sound key constants (match base filenames, no extension)
constexpr const char* kKeyStartup       = "Startup";
constexpr const char* kKeyIdle          = "Idle";
constexpr const char* kKeyRedline       = "Redline";
constexpr const char* kKeyCruise        = "CruisingUnlooped";
constexpr const char* kKeyDecel         = "Deaccerleration1";
constexpr const char* kKeyLaunch        = "LaunchControl";
constexpr const char* kKeyBackfire      = "Backfire";

constexpr const char* kGearKeys[6] = {
    "1stG", "2ndG", "3rdG", "4thG", "5thG", "6thG"
};
constexpr const char* kPopKeys[4] = {
    "Pop1", "Pop2", "Pop3", "Pop4"
};
constexpr const char* kDownshiftKeys[3] = {
    "Downshift1", "Downshift2", "Downshift3"
};
constexpr const char* kLowRevKeys[2]  = { "LowRev1",    "LowRev2" };
constexpr const char* kMedRevKeys[2]  = { "MediumRev1", "MediumRev2" };
constexpr const char* kHighRevKeys[2] = { "HighRev1",   "HighRev2" };

// Placeholder clunk = Downshift3 (per plan)
constexpr const char* kKeyClunk = "Downshift3";

}  // namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int SvjSoundManager::to_mix_volume(float vol) {
    return static_cast<int>(std::clamp(vol, 0.0f, 1.0f) * MIX_MAX_VOLUME);
}

float SvjSoundManager::get_time_seconds() const {
    return SDL_GetTicks() / 1000.0f;
}

Mix_Chunk* SvjSoundManager::get_sound(const std::string& key) const {
    auto it = sounds.find(key);
    return (it != sounds.end()) ? it->second : nullptr;
}

float SvjSoundManager::get_sound_duration(const std::string& key) const {
    auto it = sound_durations.find(key);
    return (it != sound_durations.end()) ? it->second : 0.0f;
}

void SvjSoundManager::equal_power_crossfade(float progress,
                                            float& fade_out,
                                            float& fade_in) const {
    fade_out = std::cos(progress * kHalfPi);
    fade_in  = std::sin(progress * kHalfPi);
}

int SvjSoundManager::inactive_gear_channel() const {
    return (active_gear_channel == SVJ_CH_GEAR_A) ? SVJ_CH_GEAR_B
                                                  : SVJ_CH_GEAR_A;
}

void SvjSoundManager::swap_gear_channels() {
    active_gear_channel = inactive_gear_channel();
}

int SvjSoundManager::inactive_rev_channel() const {
    return (active_rev_channel == SVJ_CH_REV_A) ? SVJ_CH_REV_B
                                                : SVJ_CH_REV_A;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

SvjSoundManager::SvjSoundManager()
    : rng(std::random_device{}())
{
    load_sounds();
}

SvjSoundManager::~SvjSoundManager() {
    free_all_sounds();
}

void SvjSoundManager::free_all_sounds() {
    for (auto& [key, chunk] : sounds) {
        if (chunk) Mix_FreeChunk(chunk);
    }
    sounds.clear();
    sound_durations.clear();
}

// ---------------------------------------------------------------------------
// Sound Loading
// ---------------------------------------------------------------------------

void SvjSoundManager::load_sounds() {
    std::printf("SVJ Sound Manager: Loading sounds with parallel optimization...\n");

    std::vector<std::string> files = {
        "Startup.wav", "Idle.wav", "Redline.wav", "CruisingUnlooped.wav",
        "Deaccerleration1.wav", "LaunchControl.wav", "Backfire.wav",
        "1stG.wav", "2ndG.wav", "3rdG.wav", "4thG.wav", "5thG.wav", "6thG.wav",
        "Pop1.wav", "Pop2.wav", "Pop3.wav", "Pop4.wav",
        "Downshift1.wav", "Downshift2.wav", "Downshift3.wav",
        "LowRev1.wav", "LowRev2.wav",
        "MediumRev1.wav", "MediumRev2.wav",
        "HighRev1.wav", "HighRev2.wav"
    };

    OptimizedSoundLoader loader(SVJ_SOUND_FILES_PATH);
    auto loaded = loader.load_sounds_parallel(files, 4);

    for (auto& [key, ls] : loaded) {
        sounds[key] = ls.chunk;
        sound_durations[key] = ls.duration;
    }

    int successful = 0;
    for (auto& [k, v] : sounds) { if (v) ++successful; }
    std::printf("SVJ: Optimized loading complete. %d sounds ready.\n", successful);
}

// ---------------------------------------------------------------------------
// Idle volume control
// ---------------------------------------------------------------------------

void SvjSoundManager::set_idle_target_volume(float target, bool instant) {
    target = std::clamp(target, 0.0f, 1.0f);
    if (std::abs(idle_target_volume - target) > 0.01f || instant) {
        idle_target_volume = target;
        if (instant) {
            idle_current_volume = target;
            if (Mix_Playing(SVJ_CH_IDLE))
                Mix_Volume(SVJ_CH_IDLE,
                           to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
            idle_is_fading = false;
        } else if (std::abs(idle_current_volume - idle_target_volume) > 0.01f) {
            idle_is_fading = true;
        }
    }
}

void SvjSoundManager::update_idle_fade(float dt) {
    if (!idle_is_fading || !Mix_Playing(SVJ_CH_IDLE)) return;

    if (std::abs(idle_current_volume - idle_target_volume) < 0.01f) {
        idle_current_volume = idle_target_volume;
        idle_is_fading = false;
    } else if (idle_current_volume < idle_target_volume) {
        idle_current_volume = std::min(
            idle_current_volume + SVJ_IDLE_TRANSITION_SPEED * dt,
            idle_target_volume);
    } else {
        idle_current_volume = std::max(
            idle_current_volume - SVJ_IDLE_TRANSITION_SPEED * dt,
            idle_target_volume);
    }

    Mix_Volume(SVJ_CH_IDLE,
               to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
}

// ---------------------------------------------------------------------------
// Startup / Idle
// ---------------------------------------------------------------------------

bool SvjSoundManager::play_startup_sound() {
    Mix_Chunk* snd = get_sound(kKeyStartup);
    if (!snd) return false;

    Mix_HaltChannel(SVJ_CH_STARTUP);
    Mix_Volume(SVJ_CH_STARTUP,
               to_mix_volume(SVJ_NORMAL_IDLE_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SVJ_CH_STARTUP, snd, 0);
    return true;
}

bool SvjSoundManager::is_startup_busy() const {
    return Mix_Playing(SVJ_CH_STARTUP) != 0;
}

bool SvjSoundManager::play_idle_loop() {
    Mix_Chunk* snd = get_sound(kKeyIdle);
    if (!snd) return false;

    if (!Mix_Playing(SVJ_CH_IDLE) || Mix_GetChunk(SVJ_CH_IDLE) != snd) {
        Mix_Volume(SVJ_CH_IDLE,
                   to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
        Mix_PlayChannel(SVJ_CH_IDLE, snd, -1);
    }
    return true;
}

void SvjSoundManager::halt_idle_loop() {
    Mix_HaltChannel(SVJ_CH_IDLE);
}

void SvjSoundManager::fade_out_idle(int fade_ms) {
    if (Mix_Playing(SVJ_CH_IDLE))
        Mix_FadeOutChannel(SVJ_CH_IDLE, fade_ms);
    idle_is_fading = false;
}

void SvjSoundManager::play_idle_fade_in() {
    Mix_Chunk* snd = get_sound(kKeyIdle);
    if (!snd) return;

    idle_current_volume = 0.0f;
    idle_is_fading = true;
    if (!Mix_Playing(SVJ_CH_IDLE) || Mix_GetChunk(SVJ_CH_IDLE) != snd) {
        Mix_Volume(SVJ_CH_IDLE, 0);
        Mix_PlayChannel(SVJ_CH_IDLE, snd, -1);
    } else {
        Mix_Volume(SVJ_CH_IDLE, 0);
    }
}

// ---------------------------------------------------------------------------
// Gear-clip A/B ping-pong
// ---------------------------------------------------------------------------

void SvjSoundManager::play_gear_clip(int gear, float volume) {
    if (gear < 1 || gear > SVJ_NUM_GEARS) return;
    Mix_Chunk* snd = get_sound(kGearKeys[gear - 1]);
    if (!snd) return;

    gear_fade_in_active = false;
    accel_redline_crossfading = false;
    gear_crossfading = false;

    // Halt the inactive channel (defensive), play on the active channel
    Mix_HaltChannel(inactive_gear_channel());
    Mix_HaltChannel(active_gear_channel);

    gear_clip_target_volume = std::clamp(volume, 0.0f, 1.0f);
    Mix_Volume(active_gear_channel,
               to_mix_volume(gear_clip_target_volume * MASTER_ENGINE_VOL));
    Mix_PlayChannel(active_gear_channel, snd, 0);

    active_gear = gear;
    gear_clip_play_start_sec = get_time_seconds();
    gear_clip_current_duration = get_sound_duration(kGearKeys[gear - 1]);
}

void SvjSoundManager::play_gear_clip_fade_in(int gear, float volume, int fade_ms) {
    if (gear < 1 || gear > SVJ_NUM_GEARS) return;
    Mix_Chunk* snd = get_sound(kGearKeys[gear - 1]);
    if (!snd) return;

    gear_crossfading = false;
    accel_redline_crossfading = false;

    Mix_HaltChannel(inactive_gear_channel());
    Mix_HaltChannel(active_gear_channel);

    gear_clip_target_volume = 0.0f;
    gear_fade_in_target_vol = std::clamp(volume, 0.0f, 1.0f);
    Mix_Volume(active_gear_channel, 0);
    Mix_PlayChannel(active_gear_channel, snd, 0);

    active_gear = gear;
    gear_clip_play_start_sec = get_time_seconds();
    gear_clip_current_duration = get_sound_duration(kGearKeys[gear - 1]);

    gear_fade_in_active = true;
    gear_fade_in_start_sec = get_time_seconds();
    gear_fade_in_duration_sec = std::max(0.01f, fade_ms / 1000.0f);
}

void SvjSoundManager::update_gear_fade_in(float dt) {
    (void)dt;
    if (!gear_fade_in_active || gear_crossfading || accel_redline_crossfading) return;

    float elapsed = get_time_seconds() - gear_fade_in_start_sec;
    float progress = std::clamp(elapsed / gear_fade_in_duration_sec, 0.0f, 1.0f);
    float fade_in = std::pow(std::sin(progress * kHalfPi), 0.62f);
    float v = gear_fade_in_target_vol * fade_in;
    Mix_Volume(active_gear_channel,
               to_mix_volume(v * MASTER_ENGINE_VOL));

    if (progress >= 1.0f) {
        gear_fade_in_active = false;
        gear_clip_target_volume = gear_fade_in_target_vol;
    }
}

void SvjSoundManager::crossfade_to_gear_clip(int gear, int crossfade_ms) {
    if (gear < 1 || gear > SVJ_NUM_GEARS) return;
    Mix_Chunk* snd = get_sound(kGearKeys[gear - 1]);
    if (!snd) return;

    gear_fade_in_active = false;
    accel_redline_crossfading = false;

    int outgoing = active_gear_channel;
    int incoming = inactive_gear_channel();

    // Start incoming silent and ramp up; outgoing keeps its current vol and
    // ramps down. We chase volumes in update_gear_crossfade().
    Mix_Volume(incoming, 0);
    Mix_HaltChannel(incoming);
    Mix_PlayChannel(incoming, snd, 0);

    gear_crossfading = true;
    gear_crossfade_start_sec = get_time_seconds();
    gear_crossfade_duration_sec = std::max(0.01f, crossfade_ms / 1000.0f);
    gear_crossfade_outgoing_channel = outgoing;
    gear_crossfade_incoming_channel = incoming;
    gear_crossfade_outgoing_start_vol = gear_clip_target_volume;
    gear_crossfade_incoming_target_vol = SVJ_GEAR_CLIP_FULL_VOLUME;

    active_gear = gear;
    active_gear_channel = incoming;
    gear_clip_play_start_sec = get_time_seconds();
    gear_clip_current_duration = get_sound_duration(kGearKeys[gear - 1]);
    gear_clip_target_volume = SVJ_GEAR_CLIP_FULL_VOLUME;
}

void SvjSoundManager::update_gear_crossfade(float dt) {
    (void)dt;
    if (!gear_crossfading) return;

    float elapsed = get_time_seconds() - gear_crossfade_start_sec;
    float progress = std::clamp(elapsed / gear_crossfade_duration_sec, 0.0f, 1.0f);
    float fade_out, fade_in;
    equal_power_crossfade(progress, fade_out, fade_in);

    Mix_Volume(gear_crossfade_outgoing_channel,
               to_mix_volume(gear_crossfade_outgoing_start_vol * fade_out
                             * MASTER_ENGINE_VOL));
    Mix_Volume(gear_crossfade_incoming_channel,
               to_mix_volume(gear_crossfade_incoming_target_vol * fade_in
                             * MASTER_ENGINE_VOL));

    if (progress >= 1.0f) {
        Mix_HaltChannel(gear_crossfade_outgoing_channel);
        gear_crossfading = false;
    }
}

void SvjSoundManager::begin_crossfade_gear_to_redline(int fade_ms) {
    gear_fade_in_active = false;

    if (gear_crossfading) {
        Mix_HaltChannel(gear_crossfade_outgoing_channel);
        gear_crossfading = false;
    }

    if (active_gear == 0 || !Mix_Playing(active_gear_channel)) {
        active_gear = 0;
        start_redline_fade_in(SVJ_REDLINE_AFTER_CLIP_FADE_IN_MS);
        return;
    }

    Mix_Chunk* rline = get_sound(kKeyRedline);
    if (!rline) {
        Mix_HaltChannel(active_gear_channel);
        active_gear = 0;
        return;
    }

    accel_redline_gear_start_vol = gear_clip_target_volume;
    Mix_HaltChannel(SVJ_CH_REDLINE);
    Mix_Volume(SVJ_CH_REDLINE, 0);
    Mix_PlayChannel(SVJ_CH_REDLINE, rline, -1);

    accel_redline_crossfading = true;
    accel_redline_start_sec = get_time_seconds();
    accel_redline_duration_sec = std::max(0.01f, fade_ms / 1000.0f);
}

void SvjSoundManager::update_accel_redline_crossfade(float dt) {
    (void)dt;
    if (!accel_redline_crossfading) return;

    float elapsed = get_time_seconds() - accel_redline_start_sec;
    float progress = std::clamp(elapsed / accel_redline_duration_sec, 0.0f, 1.0f);
    float fade_out, fade_in;
    equal_power_crossfade(progress, fade_out, fade_in);

    float gvol = accel_redline_gear_start_vol * fade_out;
    float rvol = SVJ_REDLINE_VOLUME * fade_in;
    Mix_Volume(active_gear_channel, to_mix_volume(gvol * MASTER_ENGINE_VOL));
    Mix_Volume(SVJ_CH_REDLINE, to_mix_volume(rvol * MASTER_ENGINE_VOL));

    if (progress >= 1.0f) {
        Mix_HaltChannel(active_gear_channel);
        Mix_Volume(SVJ_CH_REDLINE,
                   to_mix_volume(SVJ_REDLINE_VOLUME * MASTER_ENGINE_VOL));
        accel_redline_crossfading = false;
        active_gear = 0;
    }
}

void SvjSoundManager::halt_gear_channels() {
    if (accel_redline_crossfading) {
        accel_redline_crossfading = false;
        Mix_HaltChannel(SVJ_CH_REDLINE);
    }
    gear_fade_in_active = false;
    Mix_HaltChannel(SVJ_CH_GEAR_A);
    Mix_HaltChannel(SVJ_CH_GEAR_B);
    gear_crossfading = false;
    active_gear = 0;
}

bool SvjSoundManager::is_gear_clip_finished() const {
    if (active_gear == 0) return false;
    return Mix_Playing(active_gear_channel) == 0;
}

float SvjSoundManager::current_gear_clip_position_sec() const {
    if (active_gear == 0) return 0.0f;
    return get_time_seconds() - gear_clip_play_start_sec;
}

float SvjSoundManager::current_gear_clip_duration_sec() const {
    return gear_clip_current_duration;
}

void SvjSoundManager::set_gear_clip_volume(float vol) {
    float clamped = std::clamp(vol, 0.0f, 1.0f);
    gear_fade_in_target_vol = clamped;
    if (active_gear == 0 || gear_crossfading || accel_redline_crossfading) return;
    if (gear_fade_in_active) return;
    gear_clip_target_volume = clamped;
    Mix_Volume(active_gear_channel,
               to_mix_volume(gear_clip_target_volume * MASTER_ENGINE_VOL));
}

// ---------------------------------------------------------------------------
// Redline / Cruise / Decel
// ---------------------------------------------------------------------------

void SvjSoundManager::play_redline_loop() {
    Mix_Chunk* snd = get_sound(kKeyRedline);
    if (!snd) return;

    redline_fade_in_active = false;

    if (!Mix_Playing(SVJ_CH_REDLINE) || Mix_GetChunk(SVJ_CH_REDLINE) != snd) {
        Mix_HaltChannel(SVJ_CH_REDLINE);
        Mix_Volume(SVJ_CH_REDLINE,
                   to_mix_volume(SVJ_REDLINE_VOLUME * MASTER_ENGINE_VOL));
        Mix_PlayChannel(SVJ_CH_REDLINE, snd, -1);
    }
}

void SvjSoundManager::halt_redline_loop() {
    redline_fade_in_active = false;
    Mix_HaltChannel(SVJ_CH_REDLINE);
}

void SvjSoundManager::fade_out_redline_loop(int fade_ms) {
    redline_fade_in_active = false;
    if (fade_ms <= 0) {
        halt_redline_loop();
        return;
    }
    if (Mix_Playing(SVJ_CH_REDLINE))
        Mix_FadeOutChannel(SVJ_CH_REDLINE, fade_ms);
}

void SvjSoundManager::fade_out_gear_channels(int fade_ms) {
    gear_fade_in_active = false;
    gear_crossfading = false;
    accel_redline_crossfading = false;

    if (fade_ms <= 0) {
        halt_gear_channels();
        return;
    }
    bool any = false;
    if (Mix_Playing(SVJ_CH_GEAR_A)) {
        Mix_FadeOutChannel(SVJ_CH_GEAR_A, fade_ms);
        any = true;
    }
    if (Mix_Playing(SVJ_CH_GEAR_B)) {
        Mix_FadeOutChannel(SVJ_CH_GEAR_B, fade_ms);
        any = true;
    }
    if (!any)
        halt_gear_channels();
    else
        active_gear = 0;
}

void SvjSoundManager::start_redline_fade_in(int fade_ms) {
    Mix_Chunk* snd = get_sound(kKeyRedline);
    if (!snd) return;

    accel_redline_crossfading = false;
    redline_fade_in_active = true;
    redline_fade_in_start_sec = get_time_seconds();
    redline_fade_in_duration_sec = std::max(0.01f, fade_ms / 1000.0f);

    Mix_HaltChannel(SVJ_CH_REDLINE);
    Mix_Volume(SVJ_CH_REDLINE, 0);
    Mix_PlayChannel(SVJ_CH_REDLINE, snd, -1);
}

void SvjSoundManager::update_redline_fade_in(float dt) {
    (void)dt;
    if (!redline_fade_in_active) return;

    float elapsed = get_time_seconds() - redline_fade_in_start_sec;
    float p = std::clamp(elapsed / redline_fade_in_duration_sec, 0.0f, 1.0f);
    float v = SVJ_REDLINE_VOLUME * std::sin(p * kHalfPi);
    Mix_Volume(SVJ_CH_REDLINE, to_mix_volume(v * MASTER_ENGINE_VOL));

    if (p >= 1.0f)
        redline_fade_in_active = false;
}

bool SvjSoundManager::is_redline_playing() const {
    return Mix_Playing(SVJ_CH_REDLINE) != 0;
}

void SvjSoundManager::play_cruise_loop() {
    Mix_Chunk* snd = get_sound(kKeyCruise);
    if (!snd) return;

    if (!Mix_Playing(SVJ_CH_CRUISE) || Mix_GetChunk(SVJ_CH_CRUISE) != snd) {
        Mix_HaltChannel(SVJ_CH_CRUISE);
        Mix_Volume(SVJ_CH_CRUISE,
                   to_mix_volume(SVJ_CRUISE_VOLUME * MASTER_ENGINE_VOL));
        Mix_PlayChannel(SVJ_CH_CRUISE, snd, -1);
    }
}

void SvjSoundManager::halt_cruise_loop() {
    Mix_HaltChannel(SVJ_CH_CRUISE);
}

void SvjSoundManager::play_decel_clip() {
    Mix_Chunk* snd = get_sound(kKeyDecel);
    if (!snd) return;

    Mix_HaltChannel(SVJ_CH_DECEL);
    Mix_Volume(SVJ_CH_DECEL,
               to_mix_volume(SVJ_DECEL_VOLUME * MASTER_ENGINE_VOL));
    if (SVJ_DECEL_FADE_IN_MS > 0) {
        Mix_FadeInChannel(SVJ_CH_DECEL, snd, 0, SVJ_DECEL_FADE_IN_MS);
    } else {
        Mix_PlayChannel(SVJ_CH_DECEL, snd, 0);
    }

    decel_clip_play_start_sec = get_time_seconds();
    decel_clip_duration = get_sound_duration(kKeyDecel);
}

void SvjSoundManager::restart_decel_clip() {
    play_decel_clip();
}

void SvjSoundManager::halt_decel_clip() {
    Mix_HaltChannel(SVJ_CH_DECEL);
}

void SvjSoundManager::fade_out_decel_clip(int fade_ms) {
    if (Mix_Playing(SVJ_CH_DECEL) == 0) return;
    if (fade_ms <= 0) {
        Mix_HaltChannel(SVJ_CH_DECEL);
        return;
    }
    Mix_FadeOutChannel(SVJ_CH_DECEL, fade_ms);
}

bool SvjSoundManager::is_decel_clip_finished() const {
    return Mix_Playing(SVJ_CH_DECEL) == 0;
}

float SvjSoundManager::current_decel_clip_position_sec() const {
    if (Mix_Playing(SVJ_CH_DECEL) == 0) return 0.0f;
    return get_time_seconds() - decel_clip_play_start_sec;
}

// ---------------------------------------------------------------------------
// Rev mode (NEUTRAL)
// ---------------------------------------------------------------------------

int SvjSoundManager::zone_for_throttle(float throttle) const {
    if (throttle < SVJ_THROTTLE_IDLE_THRESHOLD) return 0;
    if (throttle <= SVJ_REV_LOW_MAX)            return 1;
    if (throttle <= SVJ_REV_MED_MAX)            return 2;
    if (throttle <= SVJ_REV_HIGH_MAX)           return 3;
    return 4;
}

std::string SvjSoundManager::pick_rev_variant(int zone) {
    std::uniform_int_distribution<int> dist(0, 1);
    int i = dist(rng);
    switch (zone) {
        case 1: return kLowRevKeys[i];
        case 2: return kMedRevKeys[i];
        case 3: return kHighRevKeys[i];
        default: return {};
    }
}

void SvjSoundManager::play_rev_for_throttle(float throttle, float dt) {
    int desired_zone = zone_for_throttle(throttle);

    // Track sustained-high-throttle to engage redline hold
    float now = get_time_seconds();
    if (desired_zone == 4) {
        if (high_throttle_start_sec < 0.0f)
            high_throttle_start_sec = now;
        float held_ms = (now - high_throttle_start_sec) * 1000.0f;
        if (held_ms >= SVJ_NEUTRAL_REDLINE_HOLD_MS) {
            if (!redline_hold_active) {
                redline_hold_active = true;
                // Halt rev variant channels, switch to redline loop
                Mix_HaltChannel(SVJ_CH_REV_A);
                Mix_HaltChannel(SVJ_CH_REV_B);
                rev_crossfading = false;
                current_rev_variant.clear();
                play_redline_loop();
            }
            current_rev_zone = 4;
            return;
        }
        // Not yet held long enough: keep playing HighRev variants
        desired_zone = 3;
    } else {
        high_throttle_start_sec = -1.0f;
        if (redline_hold_active) {
            redline_hold_active = false;
            halt_redline_loop();
        }
    }

    if (desired_zone == 0) {
        // Throttle below deadzone: stop revs
        halt_rev_sounds();
        current_rev_zone = 0;
        return;
    }

    if (desired_zone != current_rev_zone || current_rev_variant.empty()) {
        std::string variant = pick_rev_variant(desired_zone);
        Mix_Chunk* snd = get_sound(variant);
        if (!snd) {
            current_rev_zone = desired_zone;
            return;
        }

        if (current_rev_variant.empty()
            || Mix_Playing(active_rev_channel) == 0) {
            // No active rev: just play directly on the active channel
            Mix_HaltChannel(active_rev_channel);
            Mix_Volume(active_rev_channel,
                       to_mix_volume(SVJ_REV_VOLUME * MASTER_ENGINE_VOL));
            Mix_PlayChannel(active_rev_channel, snd, -1);
            current_rev_variant = variant;
            rev_crossfading = false;
        } else {
            // Active rev: crossfade to the other channel
            int incoming = inactive_rev_channel();
            Mix_HaltChannel(incoming);
            Mix_Volume(incoming, 0);
            Mix_PlayChannel(incoming, snd, -1);

            rev_crossfading = true;
            rev_crossfade_start_sec = now;
            rev_crossfade_outgoing_channel = active_rev_channel;
            rev_crossfade_incoming_channel = incoming;
            active_rev_channel = incoming;
            current_rev_variant = variant;
        }
        current_rev_zone = desired_zone;
    }

    update_rev_crossfade(dt);
}

void SvjSoundManager::update_rev_crossfade(float dt) {
    (void)dt;
    if (!rev_crossfading) return;

    float now = get_time_seconds();
    float elapsed = now - rev_crossfade_start_sec;
    float duration = SVJ_REV_ZONE_CROSSFADE_MS / 1000.0f;
    float progress = std::clamp(elapsed / duration, 0.0f, 1.0f);

    float fade_out, fade_in;
    equal_power_crossfade(progress, fade_out, fade_in);

    Mix_Volume(rev_crossfade_outgoing_channel,
               to_mix_volume(SVJ_REV_VOLUME * fade_out * MASTER_ENGINE_VOL));
    Mix_Volume(rev_crossfade_incoming_channel,
               to_mix_volume(SVJ_REV_VOLUME * fade_in * MASTER_ENGINE_VOL));

    if (progress >= 1.0f) {
        Mix_HaltChannel(rev_crossfade_outgoing_channel);
        rev_crossfading = false;
    }
}

void SvjSoundManager::halt_rev_sounds() {
    Mix_HaltChannel(SVJ_CH_REV_A);
    Mix_HaltChannel(SVJ_CH_REV_B);
    halt_redline_loop();
    rev_crossfading = false;
    redline_hold_active = false;
    high_throttle_start_sec = -1.0f;
    current_rev_variant.clear();
    current_rev_zone = 0;
}

// ---------------------------------------------------------------------------
// Launch control
// ---------------------------------------------------------------------------

void SvjSoundManager::play_launch_loop() {
    Mix_Chunk* snd = get_sound(kKeyLaunch);
    if (!snd) return;

    Mix_HaltChannel(SVJ_CH_LAUNCH);
    Mix_Volume(SVJ_CH_LAUNCH,
               to_mix_volume(SVJ_LAUNCH_LOOP_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SVJ_CH_LAUNCH, snd, -1);
}

void SvjSoundManager::fade_out_launch(int fade_ms) {
    if (Mix_Playing(SVJ_CH_LAUNCH))
        Mix_FadeOutChannel(SVJ_CH_LAUNCH, fade_ms);
}

void SvjSoundManager::halt_launch_loop() {
    Mix_HaltChannel(SVJ_CH_LAUNCH);
}

// ---------------------------------------------------------------------------
// SFX overlays
// ---------------------------------------------------------------------------

bool SvjSoundManager::play_random_pop() {
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) < SVJ_UPSHIFT_NO_POP_PROBABILITY)
        return false;

    std::uniform_int_distribution<int> dist(0, 3);
    const char* key = kPopKeys[dist(rng)];
    Mix_Chunk* snd = get_sound(key);
    if (!snd) return false;

    clear_one_shot_tail_for_channel(SVJ_CH_SHIFT_OVERLAY);
    Mix_HaltChannel(SVJ_CH_SHIFT_OVERLAY);
    const float pop_vol = std::clamp(SVJ_OVERLAY_VOLUME * SVJ_POP_VOLUME_GAIN,
                                     0.0f, 1.0f);
    Mix_Volume(SVJ_CH_SHIFT_OVERLAY,
               to_mix_volume(pop_vol * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SVJ_CH_SHIFT_OVERLAY, snd, 0);
    return true;
}

bool SvjSoundManager::play_random_downshift_overlay() {
    std::uniform_int_distribution<int> dist(0, 2);
    const char* key = kDownshiftKeys[dist(rng)];
    Mix_Chunk* snd = get_sound(key);
    if (!snd) return false;

    clear_one_shot_tail_for_channel(SVJ_CH_SHIFT_OVERLAY);
    Mix_HaltChannel(SVJ_CH_SHIFT_OVERLAY);
    Mix_Volume(SVJ_CH_SHIFT_OVERLAY,
               to_mix_volume(SVJ_OVERLAY_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SVJ_CH_SHIFT_OVERLAY, snd, 0);
    arm_one_shot_tail(one_shot_tail_shift_overlay, SVJ_CH_SHIFT_OVERLAY, snd,
                      key, SVJ_OVERLAY_VOLUME, SVJ_DOWNSHIFT_OVERLAY_TAIL_FADE_MS);
    return true;
}

bool SvjSoundManager::play_backfire(int delay_ms) {
    Mix_Chunk* snd = get_sound(kKeyBackfire);
    if (!snd) return false;

    if (delay_ms <= 0) {
        clear_one_shot_tail_for_channel(SVJ_CH_BACKFIRE);
        Mix_HaltChannel(SVJ_CH_BACKFIRE);
        Mix_Volume(SVJ_CH_BACKFIRE,
                   to_mix_volume(SVJ_OVERLAY_VOLUME * MASTER_ENGINE_VOL));
        Mix_PlayChannel(SVJ_CH_BACKFIRE, snd, 0);
        arm_one_shot_tail(one_shot_tail_backfire, SVJ_CH_BACKFIRE, snd,
                          kKeyBackfire, SVJ_OVERLAY_VOLUME,
                          SVJ_BACKFIRE_TAIL_FADE_MS);
        return true;
    }

    PendingOverlay p;
    p.sound_key = kKeyBackfire;
    p.channel = SVJ_CH_BACKFIRE;
    p.play_at_seconds = get_time_seconds() + delay_ms / 1000.0f;
    p.volume = SVJ_OVERLAY_VOLUME;
    pending_overlays.push_back(std::move(p));
    return true;
}

bool SvjSoundManager::play_clunk() {
    Mix_Chunk* snd = get_sound(kKeyClunk);
    if (!snd) return false;

    clear_one_shot_tail_for_channel(SVJ_CH_SHIFT_OVERLAY);
    Mix_HaltChannel(SVJ_CH_SHIFT_OVERLAY);
    Mix_Volume(SVJ_CH_SHIFT_OVERLAY,
               to_mix_volume(SVJ_OVERLAY_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(SVJ_CH_SHIFT_OVERLAY, snd, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Pending-overlay processing
// ---------------------------------------------------------------------------

void SvjSoundManager::clear_one_shot_tail_for_channel(int channel) {
    if (one_shot_tail_backfire.channel == channel) {
        one_shot_tail_backfire.channel = -1;
        one_shot_tail_backfire.in_silent_tail = false;
    }
    if (one_shot_tail_shift_overlay.channel == channel) {
        one_shot_tail_shift_overlay.channel = -1;
        one_shot_tail_shift_overlay.in_silent_tail = false;
    }
}

void SvjSoundManager::arm_one_shot_tail(OneShotTail& slot, int channel,
                                       Mix_Chunk* chunk,
                                       const std::string& duration_key,
                                       float base_volume_no_master,
                                       int fade_ms_nominal) {
    slot.channel         = channel;
    slot.play_start_sec  = get_time_seconds();
    slot.base_volume     = std::clamp(base_volume_no_master, 0.0f, 1.0f);
    slot.in_silent_tail  = false;

    const float from_map   = get_sound_duration(duration_key);
    const float from_chunk = duration_from_chunk_bytes(chunk);

    float dur = 0.0f;
    if (from_chunk > 0.0f && from_map > 0.0f)
        dur = std::min(from_chunk, from_map);
    else
        dur = std::max(from_chunk, from_map);

    // Schedule fades against a slightly shorter notional length so we never sit at
    // full gain right up to the decoder stop (the main cause of "abrupt" tails).
    dur *= SVJ_ONE_SHOT_TAIL_END_SAFETY;
    slot.clip_duration_sec = dur;

    if (slot.clip_duration_sec <= 0.0f || channel < 0) {
        slot.channel = -1;
        return;
    }

    const float want_fade = std::max(0.01f, fade_ms_nominal / 1000.0f);
    const float max_fade  = std::max(0.04f, slot.clip_duration_sec * 0.92f);
    slot.fade_sec         = std::min(want_fade, max_fade);
}

void SvjSoundManager::update_one_shot_tails() {
    const float now = get_time_seconds();

    auto step = [&](OneShotTail& tail) {
        if (tail.channel < 0) return;
        if (Mix_Playing(tail.channel) == 0) {
            tail.channel = -1;
            tail.in_silent_tail = false;
            return;
        }

        if (tail.in_silent_tail) {
            Mix_Volume(tail.channel, 0);
            return;
        }

        const float elapsed = now - tail.play_start_sec;
        const float dur     = tail.clip_duration_sec;
        const float fade    = tail.fade_sec;
        if (dur <= 0.0f || fade <= 0.0f) {
            tail.channel = -1;
            return;
        }

        const float fade_start = std::max(0.0f, dur - fade);
        if (elapsed < fade_start)
            return;

        float t = (elapsed - fade_start) / fade;
        if (t >= 1.0f) {
            tail.in_silent_tail = true;
            Mix_Volume(tail.channel, 0);
            return;
        }

        // Half-cosine ramp: zero slope at start/end of tail (softer than linear Mix_FadeOut)
        const float shaped = 0.5f * (1.0f + std::cos(t * kHalfPi * 2.0f));
        const float vol = tail.base_volume * shaped;
        Mix_Volume(tail.channel,
                   to_mix_volume(vol * MASTER_ENGINE_VOL));
    };

    step(one_shot_tail_backfire);
    step(one_shot_tail_shift_overlay);
}

void SvjSoundManager::process_pending_overlays() {
    if (pending_overlays.empty()) return;
    float now = get_time_seconds();

    for (auto it = pending_overlays.begin(); it != pending_overlays.end();) {
        if (now >= it->play_at_seconds) {
            Mix_Chunk* snd = get_sound(it->sound_key);
            if (snd) {
                Mix_HaltChannel(it->channel);
                Mix_Volume(it->channel,
                           to_mix_volume(it->volume * MASTER_ENGINE_VOL));
                Mix_PlayChannel(it->channel, snd, 0);
                if (it->channel == SVJ_CH_BACKFIRE) {
                    clear_one_shot_tail_for_channel(SVJ_CH_BACKFIRE);
                    arm_one_shot_tail(one_shot_tail_backfire, SVJ_CH_BACKFIRE, snd,
                                      kKeyBackfire, it->volume,
                                      SVJ_BACKFIRE_TAIL_FADE_MS);
                }
            }
            it = pending_overlays.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// update() -- called every frame
// ---------------------------------------------------------------------------

void SvjSoundManager::update(float dt) {
    update_idle_fade(dt);
    update_gear_fade_in(dt);
    update_redline_fade_in(dt);
    update_accel_redline_crossfade(dt);
    update_gear_crossfade(dt);
    update_rev_crossfade(dt);
    process_pending_overlays();
    update_one_shot_tails();
}

// ---------------------------------------------------------------------------
// Global controls
// ---------------------------------------------------------------------------

void SvjSoundManager::stop_all_sounds() {
    for (int ch = SVJ_CH_IDLE; ch <= SVJ_CH_STARTUP; ++ch)
        Mix_HaltChannel(ch);

    pending_overlays.clear();
    gear_fade_in_active = false;
    accel_redline_crossfading = false;
    redline_fade_in_active = false;
    gear_crossfading = false;
    rev_crossfading = false;
    redline_hold_active = false;
    high_throttle_start_sec = -1.0f;
    current_rev_variant.clear();
    current_rev_zone = 0;
    active_gear = 0;
    idle_is_fading = false;
    one_shot_tail_backfire.channel          = -1;
    one_shot_tail_backfire.in_silent_tail   = false;
    one_shot_tail_shift_overlay.channel     = -1;
    one_shot_tail_shift_overlay.in_silent_tail = false;
}

void SvjSoundManager::fade_out_all_sounds(int fade_ms) {
    for (int ch = SVJ_CH_IDLE; ch <= SVJ_CH_STARTUP; ++ch) {
        if (Mix_Playing(ch))
            Mix_FadeOutChannel(ch, fade_ms);
    }
    pending_overlays.clear();
    one_shot_tail_backfire.channel           = -1;
    one_shot_tail_backfire.in_silent_tail    = false;
    one_shot_tail_shift_overlay.channel      = -1;
    one_shot_tail_shift_overlay.in_silent_tail = false;
}
