#include "supra_engine.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <SDL2/SDL.h>

static float now_sec() { return SDL_GetTicks() / 1000.0f; }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SupraEngineSimulation::SupraEngineSimulation(SupraSoundManager& sound_manager)
    : sm(sound_manager)
{
    simulated_rpm = SUPRA_RPM_IDLE;
}

void SupraEngineSimulation::reset() {
    state = EngineState::ENGINE_OFF;
    simulated_rpm = SUPRA_RPM_IDLE;
    raw_throttle = 0.0f;
    ema_throttle = 0.0f;
    throttle_history.clear();
    in_potential_rev_gesture = false;
    rev_gesture_lockout_until_time = 0.0f;
    has_audio_queue = false;
    current_playing_sound_range.clear();
    last_rev_sound_finish_time = 0.0f;
}

// ---------------------------------------------------------------------------
// Convenience wrappers
// ---------------------------------------------------------------------------

bool SupraEngineSimulation::play_pull_sound_for_range(const std::string& /*range_name*/) {
    return sm.play_driving_sound(ema_throttle, "pull");
}

bool SupraEngineSimulation::play_cruise_sound_for_range(const std::string& /*range_name*/,
                                                         const std::string& cruise_type) {
    return sm.play_driving_sound(ema_throttle, cruise_type);
}

// ---------------------------------------------------------------------------
// Main update
//
// Python reference: Main_REV3.py lines 2052-2111
// State machine: ENGINE_OFF -> STARTING -> IDLE <-> PRE_ACCEL
//                                               -> ACCELERATING <-> CRUISING
//                                               -> DECELERATING -> IDLE
// ---------------------------------------------------------------------------

void SupraEngineSimulation::update(float dt, float new_throttle) {
    float previous_raw = raw_throttle;
    raw_throttle = new_throttle;

    // EMA smoothing
    ema_throttle = SUPRA_EMA_ALPHA * raw_throttle + (1.0f - SUPRA_EMA_ALPHA) * ema_throttle;

    float current_time = now_sec();
    throttle_history.push({ current_time, raw_throttle });

    sm.update_idle_fade(dt);
    sm.update_driving_crossfade();

    // RPM decay
    bool is_rev_playing = sm.is_rev_sound_busy();

    if (!is_rev_playing &&
        current_time > last_rev_sound_finish_time + SUPRA_RPM_DECAY_COOLDOWN_AFTER_REV) {
        if (simulated_rpm > SUPRA_RPM_IDLE)
            simulated_rpm = std::max(SUPRA_RPM_IDLE, simulated_rpm - SUPRA_RPM_DECAY_RATE_PER_SEC * dt);
    }
    if (!is_rev_playing &&
        current_time > last_rev_sound_finish_time + SUPRA_RPM_RESET_TO_IDLE_THRESHOLD_TIME) {
        simulated_rpm = SUPRA_RPM_IDLE;
    }

    // -----------------------------------------------------------------------
    // ENGINE_OFF
    // -----------------------------------------------------------------------
    if (state == EngineState::ENGINE_OFF) {
        if (raw_throttle > THROTTLE_DEADZONE_LOW + 0.05f) {
            std::printf("\nSupra Engine Starting...\n");
            state = EngineState::STARTING;
            state_start_time = current_time;
            sm.play_startup_sound();
            raw_throttle = 0.0f;
            ema_throttle = 0.0f;
            simulated_rpm = SUPRA_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
    }
    // -----------------------------------------------------------------------
    // STARTING
    // -----------------------------------------------------------------------
    else if (state == EngineState::STARTING) {
        if (!sm.is_rev_sound_busy()) {
            std::printf("\nSupra Engine Idling.\n");
            state = EngineState::IDLE;
            state_start_time = current_time;
            sm.set_idle_target_volume(SUPRA_NORMAL_IDLE_VOLUME);
            sm.play_idle();
            simulated_rpm = SUPRA_RPM_IDLE;
            last_rev_sound_finish_time = current_time;
        }
    }
    // -----------------------------------------------------------------------
    // IDLE
    // -----------------------------------------------------------------------
    else if (state == EngineState::IDLE) {
        handle_idle_state(current_time, previous_raw);
    }
    // -----------------------------------------------------------------------
    // PRE_ACCEL
    // -----------------------------------------------------------------------
    else if (state == EngineState::PRE_ACCEL) {
        handle_pre_accel_state(current_time);
    }
    // -----------------------------------------------------------------------
    // ACCELERATING
    // -----------------------------------------------------------------------
    else if (state == EngineState::ACCELERATING) {
        handle_accelerating_state(current_time);
    }
    // -----------------------------------------------------------------------
    // CRUISING
    // -----------------------------------------------------------------------
    else if (state == EngineState::CRUISING) {
        handle_cruising_state(current_time);
    }
    // -----------------------------------------------------------------------
    // DECELERATING
    // -----------------------------------------------------------------------
    else if (state == EngineState::DECELERATING) {
        handle_decelerating_state(current_time);
    }
}

// ---------------------------------------------------------------------------
// IDLE handler
//
// Python reference: Main_REV3.py lines 2113-2146
// Priority order:
//   1. Maintain idle sound
//   2. Check for rev gestures
//   3. If no gesture in progress and EMA throttle is sustained, -> PRE_ACCEL
// ---------------------------------------------------------------------------

void SupraEngineSimulation::handle_idle_state(float current_time,
                                               float previous_raw_throttle) {
    if (!sm.is_rev_sound_busy()) {
        sm.set_idle_target_volume(SUPRA_NORMAL_IDLE_VOLUME);
        if (!Mix_Playing(SUPRA_CH_IDLE))
            sm.play_idle();
    }

    // Always check rev gestures first
    check_rev_gestures(current_time, previous_raw_throttle);

    if (in_potential_rev_gesture) return;

    // Brief exclusion after a rev just finished
    if (current_time < (rev_gesture_lockout_until_time - SUPRA_REV_RETRIGGER_LOCKOUT + 0.3f))
        return;

    std::string ema_range = sm.get_throttle_range(ema_throttle);

    if (ema_range != "idle") {
        // Avoid transitioning on EMA lag from a completed rev gesture
        if (raw_throttle <= THROTTLE_DEADZONE_LOW * 1.2f && ema_throttle > 0.08f)
            return;

        std::printf("\nSupra IDLE -> PRE_ACCEL (EMA range: %s, sustained throttle)\n",
                    ema_range.c_str());
        state = EngineState::PRE_ACCEL;
        state_start_time = current_time;
        sm.set_idle_target_volume(0.0f);
    }
}

// ---------------------------------------------------------------------------
// PRE_ACCEL handler
//
// Python reference: Main_REV3.py lines 2148-2168
// Waits SUPRA_PRE_ACCEL_DELAY to gauge intent, then commits.
// ---------------------------------------------------------------------------

void SupraEngineSimulation::handle_pre_accel_state(float current_time) {
    float time_in_state = current_time - state_start_time;

    if (time_in_state >= SUPRA_PRE_ACCEL_DELAY) {
        std::string ema_range = sm.get_throttle_range(ema_throttle);

        if (ema_range == "idle") {
            state = EngineState::IDLE;
            sm.set_idle_target_volume(SUPRA_NORMAL_IDLE_VOLUME);
            return;
        }

        std::printf("Supra PRE_ACCEL -> ACCELERATING (Intent: %s, EMA: %.3f)\n",
                    ema_range.c_str(), ema_throttle);
        state = EngineState::ACCELERATING;
        state_start_time = current_time;

        if (play_pull_sound_for_range(ema_range))
            current_playing_sound_range = ema_range;
    }
}

// ---------------------------------------------------------------------------
// ACCELERATING handler
//
// Python reference: Main_REV3.py lines 2169-2199
// Plays pull sounds. If the range changes mid-clip, queues the new range.
// After the clip ends and no new pull is needed, transitions to CRUISING.
// ---------------------------------------------------------------------------

void SupraEngineSimulation::handle_accelerating_state(float current_time) {
    std::string ema_range = sm.get_throttle_range(ema_throttle);

    if (ema_range == "idle") {
        std::printf("\nSupra ACCELERATING -> DECELERATING (waiting for audio to finish)\n");
        state = EngineState::DECELERATING;
        state_start_time = current_time;
        has_audio_queue = false;
        return;
    }

    if (sm.is_driving_sound_busy()) {
        // Range changed while clip is playing -- queue it
        if (ema_range != current_playing_sound_range) {
            has_audio_queue = true;
            audio_queue = { ema_range, "pull" };
        }
    } else {
        // Clip finished
        if (has_audio_queue) {
            std::string queued_range = audio_queue.range;
            if (queued_range == sm.get_throttle_range(ema_throttle)) {
                if (play_pull_sound_for_range(queued_range))
                    current_playing_sound_range = queued_range;
            }
            has_audio_queue = false;
        } else {
            float time_since_pull_ended = current_time - state_start_time;
            if (time_since_pull_ended >= SUPRA_CRUISE_TRANSITION_DELAY) {
                state = EngineState::CRUISING;
                std::string cruise_type = (ema_range == "highway") ? "highway_cruise" : "cruise";
                if (play_cruise_sound_for_range(ema_range, cruise_type))
                    current_playing_sound_range = ema_range;
            } else {
                if (play_pull_sound_for_range(ema_range))
                    current_playing_sound_range = ema_range;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// CRUISING handler
//
// Python reference: Main_REV3.py lines 2201-2221
// ---------------------------------------------------------------------------

void SupraEngineSimulation::handle_cruising_state(float current_time) {
    std::string ema_range = sm.get_throttle_range(ema_throttle);

    if (ema_range == "idle") {
        std::printf("\nSupra CRUISING -> DECELERATING (waiting for audio to finish)\n");
        state = EngineState::DECELERATING;
        state_start_time = current_time;
        return;
    }

    if (ema_range != current_playing_sound_range) {
        std::printf("\nSupra CRUISING -> ACCELERATING (Range changed)\n");
        state = EngineState::ACCELERATING;
        state_start_time = current_time;
        if (sm.play_driving_sound(ema_throttle, "pull", true))
            current_playing_sound_range = ema_range;
        return;
    }

    if (!sm.is_driving_sound_busy()) {
        std::string cruise_type = (ema_range == "highway") ? "highway_cruise" : "cruise";
        play_cruise_sound_for_range(ema_range, cruise_type);
    }
}

// ---------------------------------------------------------------------------
// DECELERATING handler
//
// Python reference: Main_REV3.py lines 2223-2248
// Waits for the current driving clip to finish before returning to IDLE.
// If the user re-applies throttle, goes back to ACCELERATING.
// ---------------------------------------------------------------------------

void SupraEngineSimulation::handle_decelerating_state(float current_time) {
    std::string ema_range = sm.get_throttle_range(ema_throttle);

    if (ema_range != "idle") {
        std::printf("\nSupra DECELERATING -> ACCELERATING (throttle applied again: %s)\n",
                    ema_range.c_str());
        state = EngineState::ACCELERATING;
        state_start_time = current_time;
        if (play_pull_sound_for_range(ema_range))
            current_playing_sound_range = ema_range;
        return;
    }

    if (sm.is_driving_sound_busy()) return;

    // Audio finished -- safe to go back to idle
    std::printf("\nSupra DECELERATING -> IDLE (audio finished)\n");
    state = EngineState::IDLE;
    sm.set_idle_target_volume(SUPRA_NORMAL_IDLE_VOLUME);
    current_playing_sound_range.clear();
    if (!Mix_Playing(SUPRA_CH_IDLE))
        sm.play_idle();
}

// ---------------------------------------------------------------------------
// Rev gesture detection
//
// Python reference: Main_REV3.py lines 2263-2316
// Similar to M4 but tuned for Supra:
//   - Drop threshold is 0.6 * peak (vs M4's 0.7)
//   - Idle return threshold is 1.8 * deadzone (vs M4's 1.5)
//   - Minimum peak is deadzone + 0.04 (vs M4's 0.02)
//   - Lockout is SUPRA_REV_RETRIGGER_LOCKOUT (0.5s vs M4's 0.3s)
// ---------------------------------------------------------------------------

void SupraEngineSimulation::check_rev_gestures(float current_time,
                                                float old_throttle) {
    if (state != EngineState::IDLE) {
        in_potential_rev_gesture = false;
        return;
    }

    // Start detection
    if (!in_potential_rev_gesture && current_time >= rev_gesture_lockout_until_time) {
        bool rising_from_idle = (throttle_history.size() < 2 ||
                                 throttle_history[throttle_history.size() - 2].second <= THROTTLE_DEADZONE_LOW);
        if (raw_throttle > THROTTLE_DEADZONE_LOW && rising_from_idle) {
            std::printf("Supra rev gesture START: throttle %.3f\n", raw_throttle);
            in_potential_rev_gesture = true;
            rev_gesture_start_time = current_time;
            peak_throttle_in_rev_gesture = raw_throttle;
        }
    }

    if (!in_potential_rev_gesture) return;

    peak_throttle_in_rev_gesture = std::max(peak_throttle_in_rev_gesture, raw_throttle);

    // Timeout
    if (current_time - rev_gesture_start_time > SUPRA_REV_GESTURE_WINDOW_TIME) {
        std::printf("Supra rev gesture TIMEOUT (peak: %.3f)\n", peak_throttle_in_rev_gesture);
        in_potential_rev_gesture = false;
        return;
    }

    bool is_falling = (raw_throttle < peak_throttle_in_rev_gesture * 0.6f) &&
                      (raw_throttle < old_throttle) &&
                      (raw_throttle <= THROTTLE_DEADZONE_LOW * 1.8f);

    float min_rev_threshold = THROTTLE_DEADZONE_LOW + 0.04f;

    if (is_falling && peak_throttle_in_rev_gesture > min_rev_threshold) {
        float gesture_peak = peak_throttle_in_rev_gesture;

        std::printf("Supra rev gesture COMPLETE: peak %.3f, current %.3f\n",
                    gesture_peak, raw_throttle);
        in_potential_rev_gesture = false;
        rev_gesture_lockout_until_time = current_time + SUPRA_REV_RETRIGGER_LOCKOUT;

        SupraRevResult* rev_info = sm.play_staged_rev(simulated_rpm, gesture_peak);
        if (rev_info) {
            simulated_rpm = static_cast<float>(rev_info->rpm_peak);
            last_rev_sound_finish_time = current_time + rev_info->duration;
            sm.set_idle_target_volume(SUPRA_LOW_IDLE_VOLUME_DURING_REV);
            std::printf("Supra rev played: %s (RPM: %d)\n",
                        rev_info->key.c_str(), rev_info->rpm_peak);
        }
    } else if (is_falling) {
        std::printf("Supra rev gesture CANCELLED: peak %.3f < threshold %.3f\n",
                    peak_throttle_in_rev_gesture, min_rev_threshold);
        in_potential_rev_gesture = false;
    }
}
