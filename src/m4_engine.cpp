#include "m4_engine.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <SDL2/SDL.h>

static float now_sec() { return SDL_GetTicks() / 1000.0f; }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

M4EngineSimulation::M4EngineSimulation(M4SoundManager& sound_manager)
    : sm(sound_manager)
{
    simulated_rpm = M4_RPM_IDLE;
}

void M4EngineSimulation::reset() {
    state = EngineState::ENGINE_OFF;
    simulated_rpm = M4_RPM_IDLE;
    current_throttle = 0.0f;
    throttle_history.clear();
    peak_throttle_in_gesture = 0.0f;
    in_potential_gesture = false;
    gesture_lockout_until_time = 0.0f;
    time_at_100_throttle = 0.0f;
    time_in_idle = 0.0f;
    played_full_accel_sequence_recently = false;
    time_in_launch_control_range = 0.0f;
    last_rev_sound_finish_time = 0.0f;
}

// ---------------------------------------------------------------------------
// Main update -- called once per frame
//
// Python reference: Main_REV3.py lines 1798-1983
// State machine: ENGINE_OFF -> STARTING -> IDLING <-> PLAYFUL_REV
//                          |-> LAUNCH_HOLD -> ACCELERATING -> CRUISING
//                                                          -> DECELERATING -> IDLING
// ---------------------------------------------------------------------------

void M4EngineSimulation::update(float dt, float new_throttle) {
    float previous_throttle = current_throttle;
    current_throttle = new_throttle;
    float current_time = now_sec();

    throttle_history.push({ current_time, current_throttle });

    // Per-frame sound manager housekeeping
    sm.update_long_sequence_crossfade();
    sm.update_idle_fade(dt);
    sm.update();

    // RPM decay when no rev sound is playing
    bool is_rev_playing = Mix_Playing(M4_CH_STAGED_REV_SOUND) != 0;

    if (!is_rev_playing &&
        current_time > last_rev_sound_finish_time + M4_RPM_DECAY_COOLDOWN_AFTER_REV) {
        if (simulated_rpm > M4_RPM_IDLE)
            simulated_rpm = std::max(M4_RPM_IDLE, simulated_rpm - M4_RPM_DECAY_RATE_PER_SEC * dt);
    }
    if (!is_rev_playing &&
        current_time > last_rev_sound_finish_time + M4_RPM_RESET_TO_IDLE_THRESHOLD_TIME) {
        simulated_rpm = M4_RPM_IDLE;
    }

    // -----------------------------------------------------------------------
    // ENGINE_OFF
    // -----------------------------------------------------------------------
    if (state == EngineState::ENGINE_OFF) {
        if (current_throttle > THROTTLE_DEADZONE_LOW + 0.05f) {
            std::printf("\nM4 Engine Starting Triggered...\n");
            state = EngineState::STARTING;
            sm.play_starter_sfx();
            current_throttle = 0.0f;
            throttle_history.push({ current_time, current_throttle });
            simulated_rpm = M4_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
    }
    // -----------------------------------------------------------------------
    // STARTING
    // -----------------------------------------------------------------------
    else if (state == EngineState::STARTING) {
        if (!sm.is_turbo_limiter_sfx_busy() && !sm.is_launch_control_active()) {
            std::printf("\nM4 Engine Idling.\n");
            state = EngineState::IDLING;
            sm.set_idle_target_volume(M4_NORMAL_IDLE_VOLUME);
            sm.play_idle();
            time_in_idle = 0.0f;
            simulated_rpm = M4_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
    }
    // -----------------------------------------------------------------------
    // IDLING / PLAYFUL_REV
    // -----------------------------------------------------------------------
    else if (state == EngineState::IDLING || state == EngineState::PLAYFUL_REV) {

        if (state == EngineState::IDLING) {
            time_in_idle += dt;
            if (time_in_idle > M4_FULL_ACCEL_RESET_IDLE_TIME)
                played_full_accel_sequence_recently = false;
            if (!sm.any_playful_sfx_active() && !sm.is_launch_control_active())
                sm.set_idle_target_volume(M4_NORMAL_IDLE_VOLUME);
        } else {
            sm.set_idle_target_volume(M4_LOW_IDLE_VOLUME_DURING_SFX);
            if (!sm.any_playful_sfx_active()) {
                state = EngineState::IDLING;
                time_in_idle = 0.0f;
                sm.set_idle_target_volume(M4_NORMAL_IDLE_VOLUME);
            }
        }

        // --- Launch control detection ---
        bool in_lc_range = (current_throttle > M4_LAUNCH_CONTROL_THROTTLE_MIN &&
                            current_throttle < M4_LAUNCH_CONTROL_THROTTLE_MAX);

        if (in_lc_range && !M4_LAUNCH_CONTROL_BRAKE_REQUIRED &&
            state != EngineState::LAUNCH_HOLD) {
            time_in_launch_control_range += dt;
            if (time_in_launch_control_range >= M4_LAUNCH_CONTROL_HOLD_DURATION &&
                !sm.is_launch_control_active()) {
                std::printf("\nM4 Launch Control Engaged!\n");
                state = EngineState::LAUNCH_HOLD;
                sm.stop_staged_rev_sound();
                sm.stop_turbo_limiter_sfx();
                if (sm.play_launch_control_sequence()) {
                    sm.set_idle_target_volume(M4_VERY_LOW_IDLE_VOLUME_DURING_LAUNCH, true);
                } else {
                    state = EngineState::IDLING;
                }
                time_in_launch_control_range = 0.0f;
                time_at_100_throttle = 0.0f;
                simulated_rpm = M4_RPM_IDLE;
                last_rev_sound_finish_time = current_time;
                return;
            }
        } else if (!in_lc_range && state != EngineState::LAUNCH_HOLD) {
            time_in_launch_control_range = 0.0f;
        }

        // --- Playful gesture detection ---
        if (state != EngineState::LAUNCH_HOLD)
            check_playful_gestures(current_time, previous_throttle);

        // --- Sustained full throttle -> acceleration ---
        if (current_throttle >= 0.98f) {
            time_at_100_throttle += dt;
            if (time_at_100_throttle >= M4_SUSTAINED_100_THROTTLE_TIME &&
                !played_full_accel_sequence_recently &&
                state != EngineState::ACCELERATING &&
                state != EngineState::LAUNCH_HOLD) {
                std::printf("\nM4 Full Acceleration!\n");
                state = EngineState::ACCELERATING;
                sm.set_idle_target_volume(0.0f, true);
                sm.stop_launch_control_sequence(50);
                sm.stop_staged_rev_sound();
                sm.stop_turbo_limiter_sfx();
                sm.play_long_sequence("accel_gears", 0, false, M4_ACCELERATION_SOUND_OFFSET);
                played_full_accel_sequence_recently = true;
                time_at_100_throttle = 0.0f;
                simulated_rpm = M4_RPM_IDLE;
                last_rev_sound_finish_time = current_time;
            }
        } else {
            time_at_100_throttle = 0.0f;
        }
    }
    // -----------------------------------------------------------------------
    // LAUNCH_HOLD
    // -----------------------------------------------------------------------
    else if (state == EngineState::LAUNCH_HOLD) {
        sm.set_idle_target_volume(M4_VERY_LOW_IDLE_VOLUME_DURING_LAUNCH, true);

        if (current_throttle >= 0.80f) {
            std::printf("\nM4 Launching! (throttle: %.2f)\n", current_throttle);
            state = EngineState::ACCELERATING;
            sm.stop_launch_control_sequence(0);
            sm.set_idle_target_volume(0.0f, true);
            sm.play_long_sequence("accel_gears", 0, false, M4_LAUNCH_ACCELERATION_SOUND_OFFSET);
            played_full_accel_sequence_recently = true;
            simulated_rpm = M4_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
        else if (current_throttle < M4_LAUNCH_CONTROL_THROTTLE_MIN ||
                 !sm.is_launch_control_active()) {
            if (sm.is_launch_control_active())
                std::printf("\nM4 Launch Control Disengaged. (throttle dropped to %.2f)\n", current_throttle);
            state = EngineState::IDLING;
            sm.stop_launch_control_sequence();
            sm.set_idle_target_volume(M4_NORMAL_IDLE_VOLUME);
            sm.play_idle();
            time_in_idle = 0.0f;
            simulated_rpm = M4_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
    }
    // -----------------------------------------------------------------------
    // ACCELERATING
    // -----------------------------------------------------------------------
    else if (state == EngineState::ACCELERATING) {
        sm.set_idle_target_volume(0.0f, true);

        if (current_throttle < 0.90f) {
            std::printf("\nM4 Decelerating...\n");
            state = EngineState::DECELERATING;
            sm.stop_long_sequence(100);
            sm.play_long_sequence("decel_downshifts", 0, false, 0.05f);
        }
        else if (!sm.is_long_sequence_busy() && !sm.transitioning_long_sound) {
            if (current_throttle >= 0.90f) {
                std::printf("\nM4 Cruising...\n");
                state = EngineState::CRUISING;
                sm.stop_long_sequence(100);
                sm.play_long_sequence("cruising", -1, false, 0.05f);
            } else {
                std::printf("\nM4 Decelerating (from accel end)...\n");
                state = EngineState::DECELERATING;
                sm.stop_long_sequence(100);
                sm.play_long_sequence("decel_downshifts", 0, false, 0.05f);
            }
        }
    }
    // -----------------------------------------------------------------------
    // CRUISING
    // -----------------------------------------------------------------------
    else if (state == EngineState::CRUISING) {
        sm.set_idle_target_volume(0.0f, true);

        if (current_throttle < 0.90f) {
            std::printf("\nM4 Decelerating (from cruise)...\n");
            state = EngineState::DECELERATING;
            sm.stop_long_sequence(100);
            sm.play_long_sequence("decel_downshifts", 0, false, 0.05f);
        }
    }
    // -----------------------------------------------------------------------
    // DECELERATING
    // -----------------------------------------------------------------------
    else if (state == EngineState::DECELERATING) {
        sm.set_idle_target_volume(0.0f, true);

        if (current_throttle >= 0.85f) {
            std::printf("\nM4 Back to Accelerating (from decel) - throttle: %.3f\n", current_throttle);
            state = EngineState::ACCELERATING;
            sm.stop_long_sequence(100);
            sm.play_long_sequence("accel_gears", 0, false,
                                  M4_ACCELERATION_SOUND_OFFSET + 0.05f);
            played_full_accel_sequence_recently = true;
        }
        else if (!sm.is_long_sequence_busy() && !sm.transitioning_long_sound) {
            std::printf("\nM4 Back to Idling (from decel end).\n");
            state = EngineState::IDLING;
            sm.set_idle_target_volume(M4_NORMAL_IDLE_VOLUME);
            sm.play_idle();
            time_in_idle = 0.0f;
            simulated_rpm = M4_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
    }
}

// ---------------------------------------------------------------------------
// Playful gesture detection
//
// Detects a quick throttle blip (rise then fall within the gesture window)
// and triggers the appropriate staged rev sound based on gesture intensity
// and current simulated RPM.
//
// Python reference: Main_REV3.py lines 1985-2022
// ---------------------------------------------------------------------------

void M4EngineSimulation::check_playful_gestures(float current_time,
                                                  float old_throttle) {
    if (sm.is_long_sequence_busy() ||
        state == EngineState::LAUNCH_HOLD ||
        sm.is_launch_control_active()) {
        in_potential_gesture = false;
        return;
    }

    // Start new gesture detection
    if (!in_potential_gesture && current_time >= gesture_lockout_until_time) {
        bool rising_from_idle = (throttle_history.size() < 2 ||
                                 throttle_history[throttle_history.size() - 2].second <= THROTTLE_DEADZONE_LOW);
        if (current_throttle > THROTTLE_DEADZONE_LOW && rising_from_idle) {
            in_potential_gesture = true;
            gesture_start_time = current_time;
            peak_throttle_in_gesture = current_throttle;
        }
    }

    if (!in_potential_gesture) return;

    peak_throttle_in_gesture = std::max(peak_throttle_in_gesture, current_throttle);

    // Timeout
    if (current_time - gesture_start_time > M4_GESTURE_WINDOW_TIME) {
        in_potential_gesture = false;
        return;
    }

    bool is_falling = (current_throttle < peak_throttle_in_gesture * 0.7f) &&
                      (current_throttle < old_throttle) &&
                      (current_throttle <= THROTTLE_DEADZONE_LOW * 1.5f);

    if (is_falling && peak_throttle_in_gesture > THROTTLE_DEADZONE_LOW + 0.02f) {
        float gesture_peak = peak_throttle_in_gesture;

        in_potential_gesture = false;
        gesture_lockout_until_time = current_time + M4_GESTURE_RETRIGGER_LOCKOUT;

        M4RevResult* rev_info = sm.play_staged_rev(simulated_rpm, gesture_peak);
        if (rev_info) {
            simulated_rpm = static_cast<float>(rev_info->rpm_peak);
            last_rev_sound_finish_time = current_time + rev_info->duration;
            sm.set_idle_target_volume(M4_LOW_IDLE_VOLUME_DURING_SFX);
            if (state == EngineState::IDLING || state == EngineState::PLAYFUL_REV)
                state = EngineState::PLAYFUL_REV;
            time_at_100_throttle = 0.0f;
        }
    }
}
