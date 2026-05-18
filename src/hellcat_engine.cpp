#include "hellcat_engine.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <SDL2/SDL.h>

static float now_sec() { return SDL_GetTicks() / 1000.0f; }

HellcatEngineSimulation::HellcatEngineSimulation(HellcatSoundManager& sound_manager)
    : sm(sound_manager)
{
    simulated_rpm = HELLCAT_IDLE_RPM;
}

void HellcatEngineSimulation::reset() {
    state = EngineState::ENGINE_OFF;
    simulated_rpm = HELLCAT_IDLE_RPM;
    raw_throttle = 0.0f;
    smoothed_throttle = 0.0f;
    previous_throttle = 0.0f;
    engine_load = 0.0f;
    simulated_gear = 1;
    last_shift_time = 0.0f;
    in_simple_rev_gesture = false;
    rev_lockout_until = 0.0f;
}

void HellcatEngineSimulation::update(float dt, float new_throttle) {
    previous_throttle = raw_throttle;
    raw_throttle = new_throttle;
    smoothed_throttle = HELLCAT_EMA_ALPHA * raw_throttle
                      + (1.0f - HELLCAT_EMA_ALPHA) * smoothed_throttle;
    engine_load = raw_throttle - previous_throttle;
    float current_time = now_sec();
    sm.update(dt);

    if (state == EngineState::ENGINE_OFF) {
        if (raw_throttle > THROTTLE_DEADZONE_LOW + 0.05f) {
            state = EngineState::STARTING;
            state_start_time = current_time;
            sm.play_startup_sound();
            raw_throttle = 0.0f;
            smoothed_throttle = 0.0f;
            simulated_rpm = HELLCAT_IDLE_RPM;
        }
    }
    else if (state == EngineState::STARTING) {
        if (!sm.is_startup_busy()) {
            state = EngineState::IDLE;
            state_start_time = current_time;
            sm.play_foundation_layer(0.0f, HELLCAT_IDLE_RPM);
            sm.set_idle_target_volume(HELLCAT_NORMAL_IDLE_VOLUME);
        }
    }
    else if (state == EngineState::IDLE) {
        handle_idle_state(current_time, dt);
    }
    else if (state == EngineState::DRIVING) {
        handle_driving_state(current_time, dt);
    }

    if (state != EngineState::ENGINE_OFF && state != EngineState::STARTING) {
        if (std::fabs(engine_load) < 0.3f)
            sm.play_character_layer(engine_load, simulated_rpm, smoothed_throttle);
        sm.play_foundation_layer(smoothed_throttle, simulated_rpm);
    }
}

void HellcatEngineSimulation::handle_idle_state(float current_time, float dt) {
    sm.set_idle_target_volume(HELLCAT_NORMAL_IDLE_VOLUME);
    check_simple_rev_gestures(current_time);

    if (!in_simple_rev_gesture && smoothed_throttle > HELLCAT_THROTTLE_IDLE_THRESHOLD) {
        if (raw_throttle > HELLCAT_THROTTLE_IDLE_THRESHOLD) {
            state = EngineState::DRIVING;
            state_start_time = current_time;
        }
    }

    if (simulated_rpm > HELLCAT_IDLE_RPM)
        simulated_rpm = std::max(HELLCAT_IDLE_RPM, simulated_rpm - HELLCAT_RPM_DECAY_COAST * dt);
}

void HellcatEngineSimulation::handle_driving_state(float current_time, float dt) {
    // RPM physics
    if (smoothed_throttle > HELLCAT_THROTTLE_IDLE_THRESHOLD) {
        float gear_multiplier = HELLCAT_GEAR_ACCEL_MULTIPLIERS[simulated_gear - 1];
        float rpm_increase = HELLCAT_RPM_ACCEL_BASE * smoothed_throttle * gear_multiplier * dt;
        simulated_rpm += rpm_increase;
    } else {
        float gear_braking = HELLCAT_GEAR_ENGINE_BRAKING[simulated_gear - 1];
        float total_decay;
        if (engine_load < -0.05f)
            total_decay = HELLCAT_RPM_THROTTLE_LIFT_DECAY + gear_braking;
        else
            total_decay = HELLCAT_RPM_DECAY_COAST + gear_braking;
        simulated_rpm = std::max(HELLCAT_IDLE_RPM, simulated_rpm - total_decay * dt);
    }

    simulated_rpm = std::min(simulated_rpm, HELLCAT_REDLINE_RPM);

    float time_since_shift = current_time - last_shift_time;

    // Upshift
    if (simulated_rpm >= HELLCAT_REDLINE_RPM
        && simulated_gear < HELLCAT_NUM_GEARS
        && time_since_shift >= HELLCAT_MIN_SHIFT_INTERVAL) {
        if (sm.play_upshift_sound())
            sm.set_idle_target_volume(HELLCAT_LOW_IDLE_VOLUME_DURING_SHIFT);
        simulated_gear++;
        float new_rpm = 1800.0f + simulated_gear * 100.0f;
        simulated_rpm = new_rpm;
        last_shift_time = current_time;
    }

    // Downshift
    if (simulated_gear > 1 && time_since_shift >= HELLCAT_MIN_SHIFT_INTERVAL) {
        float downshift_threshold = HELLCAT_GEAR_DOWNSHIFT_THRESHOLDS[simulated_gear - 1];

        bool immediate = engine_load < -0.15f
                      && simulated_rpm < downshift_threshold * 1.4f;
        bool natural   = simulated_rpm < downshift_threshold
                      && smoothed_throttle < 0.4f;
        bool stopping  = simulated_rpm < 1400.0f
                      && smoothed_throttle < 0.15f;
        bool sustained = smoothed_throttle < 0.1f
                      && simulated_rpm < downshift_threshold * 1.3f
                      && time_since_shift >= HELLCAT_MIN_SHIFT_INTERVAL * 0.7f;

        if (immediate || natural || stopping || sustained) {
            if (sm.play_downshift_sound())
                sm.set_idle_target_volume(HELLCAT_LOW_IDLE_VOLUME_DURING_SHIFT);
            simulated_gear--;
            float rev_match = 400.0f + simulated_gear * 150.0f;
            simulated_rpm = std::min(HELLCAT_REDLINE_RPM * 0.75f, simulated_rpm + rev_match);
            last_shift_time = current_time;
        }
    }

    if (!sm.is_shift_busy())
        sm.set_idle_target_volume(HELLCAT_NORMAL_IDLE_VOLUME);

    // Return to idle
    if (smoothed_throttle <= HELLCAT_THROTTLE_IDLE_THRESHOLD && simulated_gear <= 1) {
        state = EngineState::IDLE;
        state_start_time = current_time;
        sm.set_idle_target_volume(HELLCAT_NORMAL_IDLE_VOLUME);
        sm.clear_rev_queue();
        in_simple_rev_gesture = false;
        rev_lockout_until = current_time + 1.0f;
    }
}

void HellcatEngineSimulation::check_simple_rev_gestures(float current_time) {
    if (state != EngineState::IDLE || current_time - state_start_time < 0.5f) {
        in_simple_rev_gesture = false;
        if (state != EngineState::IDLE)
            sm.clear_rev_queue();
        return;
    }

    if (!in_simple_rev_gesture && current_time >= rev_lockout_until) {
        float stable_idle_time = current_time - state_start_time;
        if (stable_idle_time < 1.0f)
            return;
        if (raw_throttle > HELLCAT_THROTTLE_IDLE_THRESHOLD * 2.0f
            && previous_throttle <= HELLCAT_THROTTLE_IDLE_THRESHOLD * 1.5f) {
            in_simple_rev_gesture = true;
            rev_gesture_start_time = current_time;
            rev_peak_throttle = raw_throttle;
        }
    }

    if (!in_simple_rev_gesture) return;

    rev_peak_throttle = std::max(rev_peak_throttle, raw_throttle);

    if (current_time - rev_gesture_start_time > 1.0f) {
        in_simple_rev_gesture = false;
        return;
    }

    bool is_dropping = raw_throttle < rev_peak_throttle * 0.5f
                    && raw_throttle <= HELLCAT_THROTTLE_IDLE_THRESHOLD * 2.0f;

    if (is_dropping && rev_peak_throttle > 0.08f) {
        in_simple_rev_gesture = false;
        rev_lockout_until = current_time + 0.5f;
        if (sm.play_simple_rev())
            sm.set_idle_target_volume(HELLCAT_LOW_IDLE_VOLUME_DURING_SHIFT);
    } else if (is_dropping) {
        in_simple_rev_gesture = false;
    }
}
