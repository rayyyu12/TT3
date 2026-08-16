#pragma once

#include "config.h"
#include <string>
#include <map>
#include <vector>
#include <random>
#include <SDL2/SDL_mixer.h>

// Pending one-shot overlay to fire after a delay (backfire, deferred pop, etc.)
struct PendingOverlay {
    std::string sound_key;
    int         channel;
    float       play_at_seconds;
    float       volume;
};

class SvjSoundManager {
public:
    SvjSoundManager();
    ~SvjSoundManager();

    SvjSoundManager(const SvjSoundManager&) = delete;
    SvjSoundManager& operator=(const SvjSoundManager&) = delete;

    void update(float dt);
    void update_idle_fade(float dt);
    void set_idle_target_volume(float target, bool instant = false);

    // ---------- Startup / Idle ----------
    bool play_startup_sound();
    bool is_startup_busy() const;
    bool play_idle_loop();
    void halt_idle_loop();
    void fade_out_idle(int fade_ms);
    void play_idle_fade_in();  // current at 0, ramps via update_idle_fade to idle_target

    // ---------- Gear-clip A/B ping-pong ----------
    void  play_gear_clip(int gear, float volume);
    void  play_gear_clip_fade_in(int gear, float volume, int fade_ms);
    void  crossfade_to_gear_clip(int gear, int crossfade_ms);
    void  halt_gear_channels();
    bool  is_gear_clip_finished() const;
    float current_gear_clip_position_sec() const;
    float current_gear_clip_duration_sec() const;
    void  set_gear_clip_volume(float vol);

    // ---------- Redline / Cruise / Decel ----------
    void play_redline_loop();
    void halt_redline_loop();
    bool is_redline_playing() const;
    void fade_out_redline_loop(int fade_ms);
    // Smoothly crossfade current gear clip into looping redline (gear fades out).
    void begin_crossfade_gear_to_redline(int fade_ms);
    void fade_out_gear_channels(int fade_ms);

    void play_cruise_loop();
    void halt_cruise_loop();

    void play_decel_clip();
    void restart_decel_clip();
    void halt_decel_clip();
    void fade_out_decel_clip(int fade_ms);
    bool is_decel_clip_finished() const;
    float current_decel_clip_position_sec() const;

    // ---------- Rev mode (NEUTRAL state) ----------
    // Crossfades between LowRev/MedRev/HighRev based on throttle. When throttle
    // is sustained above SVJ_REV_HIGH_MAX for SVJ_NEUTRAL_REDLINE_HOLD_MS, the
    // redline loop kicks in (and rev channels are halted).
    void play_rev_for_throttle(float throttle, float dt);
    void halt_rev_sounds();
    int  get_current_rev_zone() const { return current_rev_zone; }

    // ---------- Launch control ----------
    void play_launch_loop();
    void fade_out_launch(int fade_ms);
    void halt_launch_loop();

    // ---------- SFX overlays ----------
    // Returns false if the no-pop roll landed and no pop was played.
    bool play_random_pop();
    bool play_random_downshift_overlay();
    bool play_backfire(int delay_ms = 0);
    bool play_clunk();

    // ---------- Global ----------
    void stop_all_sounds();
    void fade_out_all_sounds(int fade_ms);

    // Public state (for diagnostics / logging)
    float idle_target_volume   = SVJ_NORMAL_IDLE_VOLUME;
    float idle_current_volume  = SVJ_NORMAL_IDLE_VOLUME;
    bool  idle_is_fading       = false;
    int   active_gear_channel  = SVJ_CH_GEAR_A;
    int   current_rev_zone     = 0;  // 0=none, 1=low, 2=med, 3=high, 4=redline-hold

private:
    void load_sounds();
    void free_all_sounds();
    Mix_Chunk* get_sound(const std::string& key) const;
    float      get_sound_duration(const std::string& key) const;
    static int to_mix_volume(float vol);
    float      get_time_seconds() const;

    void equal_power_crossfade(float progress, float& fade_out,
                               float& fade_in) const;

    // Gear-clip helpers
    int inactive_gear_channel() const;
    void swap_gear_channels();
    void update_gear_crossfade(float dt);
    void update_gear_fade_in(float dt);
    void update_accel_redline_crossfade(float dt);
    void update_redline_fade_in(float dt);
    void start_redline_fade_in(int fade_ms);

    // Rev helpers
    int  zone_for_throttle(float throttle) const;
    std::string pick_rev_variant(int zone);
    int  inactive_rev_channel() const;
    void update_rev_crossfade(float dt);

    // Pending overlay queue (delayed backfires/pops)
    void process_pending_overlays();

    // Long smooth-outs for short one-shots (backfire, downshift blips)
    struct OneShotTail {
        int   channel       = -1;
        float play_start_sec = 0.0f;
        float clip_duration_sec = 0.0f;
        float base_volume   = 1.0f;   // before MASTER_ENGINE_VOL
        float fade_sec      = 0.0f;   // clamped from config vs clip length at arm time
        bool  in_silent_tail = false; // ramp finished; hold 0 gain until sample ends
    };
    void clear_one_shot_tail_for_channel(int channel);
    void arm_one_shot_tail(OneShotTail& slot, int channel, Mix_Chunk* chunk,
                           const std::string& duration_key,
                           float base_volume_no_master, int fade_ms_nominal);
    void update_one_shot_tails();

    std::map<std::string, Mix_Chunk*> sounds;
    std::map<std::string, float>      sound_durations;

    // Gear-clip ping-pong state
    int   active_gear = 0;            // 1..6, 0 = none
    float gear_clip_play_start_sec = 0.0f;
    float gear_clip_current_duration = 0.0f;
    float gear_clip_target_volume = SVJ_GEAR_CLIP_FULL_VOLUME;

    // Gear crossfade (decel->accel re-tap, or short-shift fallback)
    bool  gear_crossfading = false;
    float gear_crossfade_start_sec = 0.0f;
    float gear_crossfade_duration_sec = 0.0f;
    int   gear_crossfade_outgoing_channel = -1;
    int   gear_crossfade_incoming_channel = -1;
    float gear_crossfade_outgoing_start_vol = 1.0f;
    float gear_crossfade_incoming_target_vol = 1.0f;

    // After upshift / launch: ramp gear volume 0 -> target
    bool  gear_fade_in_active = false;
    float gear_fade_in_start_sec = 0.0f;
    float gear_fade_in_duration_sec = 0.0f;
    float gear_fade_in_target_vol  = 1.0f;

    // Accel clip -> redline limiter crossfade
    bool  accel_redline_crossfading = false;
    float accel_redline_start_sec = 0.0f;
    float accel_redline_duration_sec = 0.0f;
    float accel_redline_gear_start_vol = 1.0f;

    // Limiter loop fade-in when gear clip already stopped (no audio tail to crossfade)
    bool  redline_fade_in_active = false;
    float redline_fade_in_start_sec = 0.0f;
    float redline_fade_in_duration_sec = 0.0f;

    // Decel clip state
    float decel_clip_play_start_sec = 0.0f;
    float decel_clip_duration = 0.0f;

    // Rev zone state
    int   active_rev_channel = SVJ_CH_REV_A;
    std::string current_rev_variant;
    float high_throttle_start_sec = -1.0f;  // -1 = not started
    bool  redline_hold_active = false;
    // Rev crossfade (between zone variants)
    bool  rev_crossfading = false;
    float rev_crossfade_start_sec = 0.0f;
    int   rev_crossfade_outgoing_channel = -1;
    int   rev_crossfade_incoming_channel = -1;

    // Pending overlays
    std::vector<PendingOverlay> pending_overlays;

    OneShotTail one_shot_tail_backfire;
    OneShotTail one_shot_tail_shift_overlay;

    std::mt19937 rng;
};
