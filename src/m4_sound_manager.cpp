#include "m4_sound_manager.h"
#include "sound_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <SDL2/SDL.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

int M4SoundManager::to_mix_volume(float vol) {
    return static_cast<int>(std::clamp(vol, 0.0f, 1.0f) * MIX_MAX_VOLUME);
}

float M4SoundManager::get_time_seconds() const {
    return SDL_GetTicks() / 1000.0f;
}

Mix_Chunk* M4SoundManager::get_sound(const std::string& key) const {
    auto it = sounds.find(key);
    return (it != sounds.end()) ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

M4SoundManager::M4SoundManager() {
    load_sounds();

    if (Mix_AllocateChannels(-1) > M4_CH_STAGED_REV_SOUND) {
        std::printf("M4 initialized dedicated channel %d for staged rev sounds.\n",
                    M4_CH_STAGED_REV_SOUND);
    } else {
        std::printf("M4 Warning: Not enough mixer channels for staged rev channel (%d).\n",
                    M4_CH_STAGED_REV_SOUND);
    }
}

M4SoundManager::~M4SoundManager() {
    free_all_sounds();
}

void M4SoundManager::free_all_sounds() {
    rev_stages.clear();
    for (auto& [key, chunk] : sounds) {
        if (chunk) Mix_FreeChunk(chunk);
    }
    sounds.clear();
}

// ---------------------------------------------------------------------------
// Sound Loading (uses OptimizedSoundLoader for parallel I/O)
// ---------------------------------------------------------------------------

void M4SoundManager::load_sounds() {
    std::printf("M4 Sound Manager: Loading sounds with parallel optimization...\n");

    std::vector<std::string> files = {
        "engine_idle_loop.wav",
        "engine_rev_stage1.wav", "engine_rev_stage2.wav",
        "engine_rev_stage3.wav", "engine_rev_stage4.wav",
        "turbo_spool_and_bov.wav",
        "engine_high_rev_with_limiter.wav",
        "acceleration_gears_1_to_4.wav",
        "engine_cruising_loop.wav",
        "deceleration_downshifts_to_idle.wav",
        "engine_starter.wav",
        "launch_control_engage.wav",
        "launch_control_hold_loop.wav"
    };

    OptimizedSoundLoader loader(M4_SOUND_FILES_PATH);
    auto loaded = loader.load_sounds_parallel(files, 4);

    auto grab = [&](const std::string& loaded_key) -> Mix_Chunk* {
        auto it = loaded.find(loaded_key);
        return (it != loaded.end()) ? it->second.chunk : nullptr;
    };

    sounds["idle"]                     = grab("engine_idle_loop");
    sounds["turbo_bov"]                = grab("turbo_spool_and_bov");
    sounds["rev_limiter"]              = grab("engine_high_rev_with_limiter");
    sounds["accel_gears"]              = grab("acceleration_gears_1_to_4");
    sounds["cruising"]                 = grab("engine_cruising_loop");
    sounds["decel_downshifts"]         = grab("deceleration_downshifts_to_idle");
    sounds["starter"]                  = grab("engine_starter");
    sounds["launch_control_engage"]    = grab("launch_control_engage");
    sounds["launch_control_hold_loop"] = grab("launch_control_hold_loop");

    const char* stage_keys[] = {
        "engine_rev_stage1", "engine_rev_stage2",
        "engine_rev_stage3", "engine_rev_stage4"
    };
    constexpr int rpm_peaks[] = { 3000, 5000, 7000, 8500 };

    for (int i = 0; i < 4; ++i) {
        auto it = loaded.find(stage_keys[i]);
        if (it != loaded.end() && it->second.chunk) {
            M4RevStage stage;
            stage.key      = "rev_stage" + std::to_string(i + 1);
            stage.sound    = it->second.chunk;
            stage.rpm_peak = rpm_peaks[i];
            stage.duration = it->second.duration;
            rev_stages.push_back(stage);
            sounds[stage.key] = it->second.chunk;
        }
    }

    int successful = 0;
    for (auto& [k, v] : sounds) { if (v) ++successful; }
    std::printf("M4: Optimized loading complete. %d sounds ready.\n", successful);
}

// ---------------------------------------------------------------------------
// Reverse Lookup (for logging -- given a chunk pointer, return its name)
// ---------------------------------------------------------------------------

std::string M4SoundManager::get_sound_name_from_obj(Mix_Chunk* sound_obj) const {
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
// update() -- called every frame
//   1. Process pending offset sounds (delayed playback)
//   2. Manage launch control engage -> hold loop transition
//   3. Detect when launch control sounds stop unexpectedly
// ---------------------------------------------------------------------------

void M4SoundManager::update() {
    just_switched_to_lc_hold = false;

    // 1. Process pending offset sounds
    float now = get_time_seconds();
    for (int i = static_cast<int>(pending_offset_sounds.size()) - 1; i >= 0; --i) {
        auto& p = pending_offset_sounds[i];
        if (now >= p.scheduled_time + p.offset_duration) {
            Mix_Chunk* snd = get_sound(p.sound_key);
            if (snd) {
                Mix_Volume(active_long_channel, to_mix_volume(MASTER_ENGINE_VOL));
                Mix_PlayChannel(active_long_channel, snd, p.loops);
                std::printf("M4 Playing %s with %.1fs offset simulation\n",
                            p.sound_key.c_str(), p.offset_duration);
            }
            pending_offset_sounds.erase(pending_offset_sounds.begin() + i);
        }
    }

    // 2. Launch control: once engage sound finishes, start hold loop
    Mix_Chunk* lc_engage = get_sound("launch_control_engage");
    Mix_Chunk* lc_hold   = get_sound("launch_control_hold_loop");
    Mix_Chunk* current_sfx = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);

    if (waiting_for_launch_hold_loop) {
        if (!Mix_Playing(M4_CH_TURBO_LIMITER_SFX) || current_sfx != lc_engage) {
            if (lc_hold) {
                Mix_Volume(M4_CH_TURBO_LIMITER_SFX,
                           to_mix_volume(M4_LAUNCH_CONTROL_HOLD_VOL * MASTER_ENGINE_VOL));
                Mix_PlayChannel(M4_CH_TURBO_LIMITER_SFX, lc_hold, -1);
                just_switched_to_lc_hold = true;
            } else {
                launch_control_sounds_active = false;
            }
            waiting_for_launch_hold_loop = false;
        }
    }

    // 3. Detect if launch control sounds stopped unexpectedly
    if (launch_control_sounds_active && !just_switched_to_lc_hold) {
        Mix_Chunk* sfx_now = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);
        bool sfx_busy = Mix_Playing(M4_CH_TURBO_LIMITER_SFX) != 0;

        if (!waiting_for_launch_hold_loop &&
            (!sfx_busy || (sfx_now != lc_engage && sfx_now != lc_hold))) {
            launch_control_sounds_active = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Idle -- looping idle sound with smooth volume fading
// ---------------------------------------------------------------------------

void M4SoundManager::play_idle() {
    Mix_Chunk* idle_snd = get_sound("idle");
    if (!idle_snd) return;

    if (!Mix_Playing(M4_CH_IDLE) || Mix_GetChunk(M4_CH_IDLE) != idle_snd) {
        Mix_PlayChannel(M4_CH_IDLE, idle_snd, -1);
    }
    idle_current_volume = idle_target_volume;
    Mix_Volume(M4_CH_IDLE, to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
    idle_is_fading = std::abs(idle_current_volume - idle_target_volume) > 0.01f;
}

void M4SoundManager::stop_idle() {
    Mix_HaltChannel(M4_CH_IDLE);
    idle_is_fading = false;
}

void M4SoundManager::set_idle_target_volume(float target, bool instant) {
    target = std::clamp(target, 0.0f, 1.0f);
    if (std::abs(idle_target_volume - target) > 0.01f || instant) {
        idle_target_volume = target;
        if (instant) {
            idle_current_volume = target;
            if (Mix_Playing(M4_CH_IDLE))
                Mix_Volume(M4_CH_IDLE, to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
            idle_is_fading = false;
        } else {
            if (std::abs(idle_current_volume - idle_target_volume) > 0.01f)
                idle_is_fading = true;
        }
    }
}

void M4SoundManager::update_idle_fade(float dt) {
    if (!idle_is_fading || !Mix_Playing(M4_CH_IDLE)) return;

    if (std::abs(idle_current_volume - idle_target_volume) < 0.01f) {
        idle_current_volume = idle_target_volume;
        idle_is_fading = false;
    } else if (idle_current_volume < idle_target_volume) {
        idle_current_volume = std::min(idle_current_volume + M4_IDLE_TRANSITION_SPEED * dt,
                                       idle_target_volume);
    } else {
        idle_current_volume = std::max(idle_current_volume - M4_IDLE_TRANSITION_SPEED * dt,
                                       idle_target_volume);
    }

    Mix_Volume(M4_CH_IDLE, to_mix_volume(idle_current_volume * MASTER_ENGINE_VOL));
}

// ---------------------------------------------------------------------------
// Staged Revs -- RPM-aware selection
//   Uses both current_rpm and gesture_peak_throttle to pick the right stage.
//   If the selected stage's peak RPM is below current RPM, promotes upward.
// ---------------------------------------------------------------------------

M4RevResult* M4SoundManager::play_staged_rev(float current_rpm,
                                              float gesture_peak_throttle) {
    if (Mix_Playing(M4_CH_STAGED_REV_SOUND)) return nullptr;
    if (rev_stages.empty()) return nullptr;

    if (gesture_peak_throttle <= 0.1f) {
        if (current_rpm > M4_RPM_IDLE + 500.0f) return nullptr;
    }

    const M4RevStage* selected = nullptr;
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

    // Promote to higher stage if selected peak is below current RPM
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

    std::printf("M4 Playing rev: %s (Peak: %d) | CurrentRPM: %.0f | Gesture: %.2f\n",
                selected->key.c_str(), selected->rpm_peak,
                current_rpm, gesture_peak_throttle);

    Mix_Volume(M4_CH_STAGED_REV_SOUND,
               to_mix_volume(M4_STAGED_REV_VOLUME * MASTER_ENGINE_VOL));
    Mix_PlayChannel(M4_CH_STAGED_REV_SOUND, selected->sound, 0);

    last_rev_result.key      = selected->key;
    last_rev_result.rpm_peak = selected->rpm_peak;
    last_rev_result.duration = selected->duration;
    return &last_rev_result;
}

void M4SoundManager::stop_staged_rev_sound() {
    Mix_HaltChannel(M4_CH_STAGED_REV_SOUND);
}

// ---------------------------------------------------------------------------
// Turbo / Limiter SFX Channel
//   This channel is shared between turbo/limiter one-shots, the starter sound,
//   and launch control sounds. Many methods check what's currently playing
//   on this channel to avoid stomping on launch control audio.
// ---------------------------------------------------------------------------

bool M4SoundManager::play_turbo_or_limiter_sfx(const std::string& sound_key) {
    Mix_Chunk* snd = get_sound(sound_key);
    if (!snd) return false;

    if (is_launch_control_active())
        stop_launch_control_sequence(100);

    Mix_Volume(M4_CH_TURBO_LIMITER_SFX, to_mix_volume(MASTER_ENGINE_VOL));
    Mix_PlayChannel(M4_CH_TURBO_LIMITER_SFX, snd, 0);
    return true;
}

bool M4SoundManager::play_starter_sfx() {
    Mix_Chunk* snd = get_sound("starter");
    if (!snd) return false;

    Mix_Chunk* current_sfx = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);
    Mix_Chunk* lc_engage   = get_sound("launch_control_engage");
    Mix_Chunk* lc_hold     = get_sound("launch_control_hold_loop");

    if (!Mix_Playing(M4_CH_TURBO_LIMITER_SFX) ||
        (current_sfx != lc_engage && current_sfx != lc_hold)) {
        Mix_HaltChannel(M4_CH_TURBO_LIMITER_SFX);
        Mix_Volume(M4_CH_TURBO_LIMITER_SFX,
                   to_mix_volume(M4_NORMAL_IDLE_VOLUME * MASTER_ENGINE_VOL));
        Mix_PlayChannel(M4_CH_TURBO_LIMITER_SFX, snd, 0);
        return true;
    }
    return false;
}

void M4SoundManager::stop_turbo_limiter_sfx() {
    Mix_Chunk* current_sfx = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);
    Mix_Chunk* lc_engage   = get_sound("launch_control_engage");
    Mix_Chunk* lc_hold     = get_sound("launch_control_hold_loop");

    if (current_sfx != lc_engage && current_sfx != lc_hold)
        Mix_HaltChannel(M4_CH_TURBO_LIMITER_SFX);
}

bool M4SoundManager::is_turbo_limiter_sfx_busy() const {
    if (!Mix_Playing(M4_CH_TURBO_LIMITER_SFX)) return false;

    Mix_Chunk* current_sfx = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);
    Mix_Chunk* lc_engage   = get_sound("launch_control_engage");
    Mix_Chunk* lc_hold     = get_sound("launch_control_hold_loop");

    return (current_sfx != lc_engage && current_sfx != lc_hold);
}

bool M4SoundManager::any_playful_sfx_active() const {
    if (Mix_Playing(M4_CH_STAGED_REV_SOUND)) return true;

    if (Mix_Playing(M4_CH_TURBO_LIMITER_SFX)) {
        Mix_Chunk* current_sfx = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);
        Mix_Chunk* lc_engage   = get_sound("launch_control_engage");
        Mix_Chunk* lc_hold     = get_sound("launch_control_hold_loop");
        Mix_Chunk* starter     = get_sound("starter");

        if (current_sfx && current_sfx != lc_engage &&
            current_sfx != lc_hold && current_sfx != starter) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Launch Control
//   Sequence: play engage sound -> update() detects it finished ->
//             starts looping hold sound. Disengage stops/fades everything.
// ---------------------------------------------------------------------------

bool M4SoundManager::play_launch_control_sequence() {
    Mix_Chunk* engage = get_sound("launch_control_engage");
    Mix_Chunk* hold   = get_sound("launch_control_hold_loop");

    if (engage) {
        Mix_HaltChannel(M4_CH_TURBO_LIMITER_SFX);
        Mix_Volume(M4_CH_TURBO_LIMITER_SFX,
                   to_mix_volume(M4_LAUNCH_CONTROL_ENGAGE_VOL * MASTER_ENGINE_VOL));
        Mix_PlayChannel(M4_CH_TURBO_LIMITER_SFX, engage, 0);
        waiting_for_launch_hold_loop = true;
        launch_control_sounds_active = true;
        just_switched_to_lc_hold = false;
        return true;
    }

    if (hold) {
        std::printf("M4 Launch control engage sound missing, playing hold loop directly.\n");
        Mix_HaltChannel(M4_CH_TURBO_LIMITER_SFX);
        Mix_Volume(M4_CH_TURBO_LIMITER_SFX,
                   to_mix_volume(M4_LAUNCH_CONTROL_HOLD_VOL * MASTER_ENGINE_VOL));
        Mix_PlayChannel(M4_CH_TURBO_LIMITER_SFX, hold, -1);
        waiting_for_launch_hold_loop = false;
        launch_control_sounds_active = true;
        just_switched_to_lc_hold = true;
        return true;
    }

    launch_control_sounds_active = false;
    return false;
}

void M4SoundManager::stop_launch_control_sequence(int fade_ms) {
    Mix_Chunk* current_sfx = Mix_GetChunk(M4_CH_TURBO_LIMITER_SFX);
    Mix_Chunk* lc_engage   = get_sound("launch_control_engage");
    Mix_Chunk* lc_hold     = get_sound("launch_control_hold_loop");

    if (Mix_Playing(M4_CH_TURBO_LIMITER_SFX) &&
        (current_sfx == lc_engage || current_sfx == lc_hold)) {
        if (fade_ms > 0) Mix_FadeOutChannel(M4_CH_TURBO_LIMITER_SFX, fade_ms);
        else             Mix_HaltChannel(M4_CH_TURBO_LIMITER_SFX);
    }
    waiting_for_launch_hold_loop = false;
    launch_control_sounds_active = false;
    just_switched_to_lc_hold = false;
}

bool M4SoundManager::is_launch_control_active() const {
    return launch_control_sounds_active || waiting_for_launch_hold_loop;
}

// ---------------------------------------------------------------------------
// Long Sequence -- dual-channel crossfade with optional start offset
//   Uses channels A and B. For crossfade: fade out the active channel,
//   start the new sound at volume 0 on the other channel, then
//   update_long_sequence_crossfade() ramps it up each frame.
//   start_offset schedules the sound to play after a delay (simulates
//   skipping silence at the beginning of acceleration sounds).
// ---------------------------------------------------------------------------

void M4SoundManager::play_long_sequence(const std::string& sound_key, int loops,
                                         bool transition_from_other,
                                         float start_offset) {
    Mix_Chunk* snd = get_sound(sound_key);
    if (!snd) {
        std::printf("M4 Long sequence sound key '%s' not found.\n", sound_key.c_str());
        int other = (active_long_channel == M4_CH_LONG_SEQUENCE_A)
                        ? M4_CH_LONG_SEQUENCE_B : M4_CH_LONG_SEQUENCE_A;
        Mix_HaltChannel(other);
        Mix_HaltChannel(active_long_channel);
        transitioning_long_sound = false;
        return;
    }

    if (start_offset > 0.0f) {
        std::printf("M4 Scheduling %s with %.1fs offset\n",
                    sound_key.c_str(), start_offset);
        pending_offset_sounds.push_back({ sound_key, get_time_seconds(),
                                          start_offset, loops });
        return;
    }

    if (!transition_from_other) {
        int other = (active_long_channel == M4_CH_LONG_SEQUENCE_A)
                        ? M4_CH_LONG_SEQUENCE_B : M4_CH_LONG_SEQUENCE_A;
        Mix_HaltChannel(other);
        Mix_Volume(active_long_channel, to_mix_volume(MASTER_ENGINE_VOL));
        Mix_PlayChannel(active_long_channel, snd, loops);
        transitioning_long_sound = false;
    } else {
        int fade_out_ch = active_long_channel;
        int fade_in_ch  = (active_long_channel == M4_CH_LONG_SEQUENCE_A)
                              ? M4_CH_LONG_SEQUENCE_B : M4_CH_LONG_SEQUENCE_A;

        Mix_FadeOutChannel(fade_out_ch, M4_CROSSFADE_DURATION_MS);
        Mix_Volume(fade_in_ch, 0);
        Mix_PlayChannel(fade_in_ch, snd, loops);

        active_long_channel      = fade_in_ch;
        transitioning_long_sound = true;
        transition_start_time    = get_time_seconds();
    }
}

void M4SoundManager::update_long_sequence_crossfade() {
    if (!transitioning_long_sound) return;

    float elapsed_ms = (get_time_seconds() - transition_start_time) * 1000.0f;
    float progress   = std::min(1.0f, elapsed_ms / static_cast<float>(M4_CROSSFADE_DURATION_MS));

    if (Mix_Playing(active_long_channel))
        Mix_Volume(active_long_channel, to_mix_volume(progress * MASTER_ENGINE_VOL));

    if (progress >= 1.0f) {
        transitioning_long_sound = false;
        if (Mix_Playing(active_long_channel))
            Mix_Volume(active_long_channel, to_mix_volume(MASTER_ENGINE_VOL));
    }
}

void M4SoundManager::stop_long_sequence(int fade_ms) {
    if (fade_ms > 0) {
        Mix_FadeOutChannel(M4_CH_LONG_SEQUENCE_A, fade_ms);
        Mix_FadeOutChannel(M4_CH_LONG_SEQUENCE_B, fade_ms);
    } else {
        Mix_HaltChannel(M4_CH_LONG_SEQUENCE_A);
        Mix_HaltChannel(M4_CH_LONG_SEQUENCE_B);
    }
    transitioning_long_sound = false;
}

bool M4SoundManager::is_long_sequence_busy() const {
    return Mix_Playing(M4_CH_LONG_SEQUENCE_A) ||
           Mix_Playing(M4_CH_LONG_SEQUENCE_B) ||
           transitioning_long_sound;
}

// ---------------------------------------------------------------------------
// Global Controls
// ---------------------------------------------------------------------------

void M4SoundManager::stop_all_sounds() {
    Mix_HaltChannel(M4_CH_IDLE);
    Mix_HaltChannel(M4_CH_TURBO_LIMITER_SFX);
    Mix_HaltChannel(M4_CH_LONG_SEQUENCE_A);
    Mix_HaltChannel(M4_CH_LONG_SEQUENCE_B);
    Mix_HaltChannel(M4_CH_STAGED_REV_SOUND);
    transitioning_long_sound     = false;
    launch_control_sounds_active = false;
    waiting_for_launch_hold_loop = false;
    idle_is_fading               = false;
}

void M4SoundManager::fade_out_all_sounds(int fade_ms) {
    if (Mix_Playing(M4_CH_IDLE))              Mix_FadeOutChannel(M4_CH_IDLE, fade_ms);
    if (Mix_Playing(M4_CH_TURBO_LIMITER_SFX)) Mix_FadeOutChannel(M4_CH_TURBO_LIMITER_SFX, fade_ms);
    if (Mix_Playing(M4_CH_LONG_SEQUENCE_A))   Mix_FadeOutChannel(M4_CH_LONG_SEQUENCE_A, fade_ms);
    if (Mix_Playing(M4_CH_LONG_SEQUENCE_B))   Mix_FadeOutChannel(M4_CH_LONG_SEQUENCE_B, fade_ms);
    if (Mix_Playing(M4_CH_STAGED_REV_SOUND))  Mix_FadeOutChannel(M4_CH_STAGED_REV_SOUND, fade_ms);
}
