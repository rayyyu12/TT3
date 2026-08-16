#include "svj_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <SDL2/SDL.h>

namespace {
float now_sec() { return SDL_GetTicks() / 1000.0f; }
}  // namespace

SvjEngineSimulation::SvjEngineSimulation(SvjSoundManager& sound_manager)
    : sm(sound_manager),
      rng(std::random_device{}())
{
    simulated_rpm = 0.0f;
}

void SvjEngineSimulation::reset() {
    state = EngineState::ENGINE_OFF;
    raw_throttle = 0.0f;
    smoothed_throttle = 0.0f;
    simulated_gear = 1;
    gear_clip_elapsed = 0.0f;
    launch_release_t = 0.0f;
    launch_firing = false;
    current_rev_zone = 0;
    left_event_pending = false;
    right_event_pending = false;
    ignition_cut_pending = false;
    ignition_cut_due_sec = 0.0f;
    ignition_cut_target_gear = 1;
    simulated_rpm = 0.0f;
}

void SvjEngineSimulation::on_left_signal()  { left_event_pending  = true; }
void SvjEngineSimulation::on_right_signal() { right_event_pending = true; }

float SvjEngineSimulation::partial_throttle_volume(float throttle) const {
    if (throttle <= SVJ_THROTTLE_IDLE_THRESHOLD)
        return SVJ_PARTIAL_THROTTLE_FLOOR;
    float t = std::pow(std::clamp(throttle, 0.0f, 1.0f),
                       SVJ_PARTIAL_THROTTLE_POWER);
    return std::max(SVJ_PARTIAL_THROTTLE_FLOOR, t);
}

void SvjEngineSimulation::update(float dt, float new_throttle) {
    raw_throttle = new_throttle;
    smoothed_throttle = 0.3f * raw_throttle + 0.7f * smoothed_throttle;
    sm.update(dt);

    // Service pending ignition-cut (upshift) commit
    if (ignition_cut_pending && now_sec() >= ignition_cut_due_sec) {
        simulated_gear = ignition_cut_target_gear;
        sm.play_gear_clip_fade_in(simulated_gear,
                                  partial_throttle_volume(raw_throttle),
                                  SVJ_POST_SHIFT_GEAR_FADE_IN_MS);
        gear_clip_elapsed = 0.0f;
        ignition_cut_pending = false;
        state = EngineState::ACCELERATING;
    }

    // Service in-flight launch firing (LAUNCH_HOLD -> ACCELERATING)
    if (launch_firing) {
        launch_release_t += dt;
        if (launch_release_t >= SVJ_LAUNCH_RELEASE_DELAY) {
            simulated_gear = 1;
            sm.halt_launch_loop();
            sm.play_gear_clip_fade_in(simulated_gear,
                                      partial_throttle_volume(raw_throttle),
                                      SVJ_POST_SHIFT_GEAR_FADE_IN_MS);
            gear_clip_elapsed = 0.0f;
            launch_firing = false;
            launch_release_t = 0.0f;
            state = EngineState::ACCELERATING;
        }
    }

    switch (state) {
        case EngineState::ENGINE_OFF:   handle_engine_off(dt);   break;
        case EngineState::STARTING:     handle_starting(dt);     break;
        case EngineState::IDLE:         handle_idle(dt);         break;
        case EngineState::ACCELERATING: handle_accelerating(dt); break;
        case EngineState::REDLINE:      handle_redline(dt);      break;
        case EngineState::CRUISING:     handle_cruising(dt);     break;
        case EngineState::DECELERATING: handle_decelerating(dt); break;
        case EngineState::NEUTRAL:      handle_neutral(dt);      break;
        case EngineState::LAUNCH_HOLD:  handle_launch_hold(dt);  break;
        default:
            break;
    }

    // Always-consumed: signal events not handled by the active handler are dropped.
    left_event_pending = false;
    right_event_pending = false;

    current_rev_zone = sm.get_current_rev_zone();
}

// ---------------------------------------------------------------------------
// State entries
// ---------------------------------------------------------------------------

void SvjEngineSimulation::enter_idle() {
    sm.halt_gear_channels();
    sm.halt_redline_loop();
    sm.halt_cruise_loop();
    sm.halt_decel_clip();
    sm.halt_rev_sounds();
    sm.set_idle_target_volume(SVJ_NORMAL_IDLE_VOLUME);
    sm.play_idle_fade_in();
    state = EngineState::IDLE;
}

void SvjEngineSimulation::enter_accelerating(int crossfade_ms) {
    sm.fade_out_idle(SVJ_IDLE_LEAVE_FADE_MS);
    sm.halt_redline_loop();
    sm.halt_cruise_loop();
    if (crossfade_ms > 0)
        sm.fade_out_decel_clip(crossfade_ms);
    else
        sm.halt_decel_clip();

    if (crossfade_ms > 0) {
        sm.crossfade_to_gear_clip(simulated_gear, crossfade_ms);
    } else {
        sm.play_gear_clip_fade_in(simulated_gear,
                                  partial_throttle_volume(raw_throttle),
                                  SVJ_POST_SHIFT_GEAR_FADE_IN_MS);
    }
    gear_clip_elapsed = 0.0f;
    state = EngineState::ACCELERATING;
}

void SvjEngineSimulation::enter_redline() {
    sm.begin_crossfade_gear_to_redline(SVJ_ACCEL_TO_REDLINE_CROSSFADE_MS);
    state = EngineState::REDLINE;
}

void SvjEngineSimulation::enter_cruising() {
    sm.fade_out_idle(SVJ_IDLE_LEAVE_FADE_MS);
    sm.halt_gear_channels();
    sm.halt_redline_loop();
    sm.play_cruise_loop();
    state = EngineState::CRUISING;
}

void SvjEngineSimulation::enter_decelerating() {
    // Backfire roll for accel->decel only
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    if (chance(rng) < SVJ_DECEL_BACKFIRE_PROBABILITY)
        sm.play_backfire(SVJ_BACKFIRE_DELAY_MS);

    sm.fade_out_idle(SVJ_IDLE_LEAVE_FADE_MS);
    sm.halt_gear_channels();
    sm.halt_redline_loop();
    sm.halt_cruise_loop();
    sm.play_decel_clip();
    state = EngineState::DECELERATING;
}

void SvjEngineSimulation::enter_neutral() {
    sm.fade_out_idle(SVJ_IDLE_LEAVE_FADE_MS);
    sm.halt_gear_channels();
    sm.halt_redline_loop();
    sm.halt_cruise_loop();
    sm.halt_decel_clip();

    // NEUTRAL entry: GUARANTEED pop (re-roll past the 25% no-pop dice)
    for (int i = 0; i < 8; ++i)
        if (sm.play_random_pop()) break;

    state = EngineState::NEUTRAL;
    sm.set_idle_target_volume(SVJ_LOW_IDLE_VOLUME_DURING_SHIFT);
}

void SvjEngineSimulation::exit_neutral_with_clunk() {
    sm.halt_rev_sounds();
    sm.play_clunk();
    simulated_gear = 1;
    sm.set_idle_target_volume(SVJ_NORMAL_IDLE_VOLUME);
    state = EngineState::IDLE;
    sm.play_idle_fade_in();
}

void SvjEngineSimulation::enter_launch_hold() {
    sm.fade_out_idle(SVJ_IDLE_LEAVE_FADE_MS);
    sm.halt_gear_channels();
    sm.halt_redline_loop();
    sm.halt_cruise_loop();
    sm.halt_decel_clip();
    sm.halt_rev_sounds();
    sm.play_launch_loop();
    sm.set_idle_target_volume(SVJ_LOW_IDLE_VOLUME_DURING_SHIFT);
    simulated_gear = 1;
    launch_firing = false;
    launch_release_t = 0.0f;
    state = EngineState::LAUNCH_HOLD;
}

void SvjEngineSimulation::trigger_launch() {
    if (launch_firing) return;
    launch_firing = true;
    launch_release_t = 0.0f;
    sm.fade_out_launch(SVJ_LAUNCH_FADE_OUT_MS);
}

void SvjEngineSimulation::cancel_launch() {
    sm.halt_launch_loop();
    launch_firing = false;
    launch_release_t = 0.0f;
    sm.set_idle_target_volume(SVJ_NORMAL_IDLE_VOLUME);
    state = EngineState::IDLE;
    sm.play_idle_fade_in();
}

// ---------------------------------------------------------------------------
// Shift actions (moving states)
// ---------------------------------------------------------------------------

void SvjEngineSimulation::schedule_ignition_cut(int new_gear) {
    int fade_ms = 0;
    if (state == EngineState::REDLINE || sm.is_redline_playing()) {
        sm.fade_out_redline_loop(SVJ_SHIFT_REDLINE_FADE_OUT_MS);
        fade_ms = SVJ_SHIFT_REDLINE_FADE_OUT_MS;
    } else {
        sm.fade_out_gear_channels(SVJ_SHIFT_GEAR_FADE_OUT_MS);
        fade_ms = SVJ_SHIFT_GEAR_FADE_OUT_MS;
    }

    sm.play_random_pop();

    ignition_cut_pending = true;
    ignition_cut_due_sec = now_sec()
                         + fade_ms / 1000.0f
                         + SVJ_IGNITION_CUT_MS / 1000.0f;
    ignition_cut_target_gear = new_gear;
}

void SvjEngineSimulation::attempt_upshift() {
    if (state != EngineState::ACCELERATING && state != EngineState::REDLINE)
        return;
    if (simulated_gear >= SVJ_NUM_GEARS) return;
    if (ignition_cut_pending) return;

    int next_gear = simulated_gear + 1;
    if (next_gear == SVJ_NUM_GEARS && state == EngineState::REDLINE) {
        // From REDLINE into 6th: still ignition-cut into the 6th gear clip
        schedule_ignition_cut(next_gear);
        return;
    }
    schedule_ignition_cut(next_gear);
}

void SvjEngineSimulation::attempt_downshift() {
    if (state != EngineState::DECELERATING) return;
    if (simulated_gear <= 1) return;

    simulated_gear--;
    sm.restart_decel_clip();
    sm.play_random_downshift_overlay();

    if (simulated_gear == 1 || simulated_gear == 2) {
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        if (chance(rng) < SVJ_DOWNSHIFT_BACKFIRE_PROBABILITY)
            sm.play_backfire(SVJ_DOWNSHIFT_BACKFIRE_DELAY_MS);
    }
}

// ---------------------------------------------------------------------------
// Per-frame state handlers
// ---------------------------------------------------------------------------

void SvjEngineSimulation::handle_engine_off(float /*dt*/) {
    // Start when we first see throttle or any user activity.
    if (raw_throttle > SVJ_THROTTLE_IDLE_THRESHOLD
        || left_event_pending || right_event_pending) {
        state = EngineState::STARTING;
        sm.play_startup_sound();
        sm.set_idle_target_volume(SVJ_NORMAL_IDLE_VOLUME, true);
    }
}

void SvjEngineSimulation::handle_starting(float /*dt*/) {
    if (!sm.is_startup_busy()) {
        state = EngineState::IDLE;
        sm.set_idle_target_volume(SVJ_NORMAL_IDLE_VOLUME);
        sm.play_idle_fade_in();
    }
}

void SvjEngineSimulation::handle_idle(float /*dt*/) {
    sm.play_idle_loop();

    // Stationary button mapping: right = NEUTRAL, left = LAUNCH HOLD
    if (right_event_pending) {
        enter_neutral();
        return;
    }
    if (left_event_pending) {
        enter_launch_hold();
        return;
    }

    if (raw_throttle > SVJ_THROTTLE_IDLE_THRESHOLD) {
        enter_accelerating(0);
    }
}

void SvjEngineSimulation::handle_accelerating(float dt) {
    if (ignition_cut_pending) return;

    gear_clip_elapsed += dt;

    // Partial-throttle volume scaling
    sm.set_gear_clip_volume(partial_throttle_volume(raw_throttle));

    // Throttle drop -> DECELERATING
    if (raw_throttle <= SVJ_THROTTLE_IDLE_THRESHOLD) {
        enter_decelerating();
        return;
    }

    // Button routing (moving)
    if (right_event_pending) {
        attempt_upshift();
        return;
    }
    if (left_event_pending) {
        // Downshift only allowed during DECELERATING -- ignore here.
    }

    // Auto-transitions
    if (simulated_gear == SVJ_NUM_GEARS) {
        // 6th: no redline; when clip finishes -> CRUISING
        if (sm.is_gear_clip_finished()) {
            enter_cruising();
            return;
        }
    } else {
        float threshold = SVJ_REDLINE_THRESHOLD_SEC[simulated_gear - 1];
        float dur       = sm.current_gear_clip_duration_sec();
        float pos       = sm.current_gear_clip_position_sec();
        bool time_hit   = (threshold > 0.0f && gear_clip_elapsed >= threshold);
        bool near_end   = (dur > 0.05f
                        && pos >= std::max(0.0f, dur - SVJ_REDLINE_CLIP_END_LEAD_SEC));
        bool clip_done  = sm.is_gear_clip_finished();
        if (time_hit || near_end || clip_done) {
            enter_redline();
            return;
        }
    }
}

void SvjEngineSimulation::handle_redline(float /*dt*/) {
    if (ignition_cut_pending) return;

    if (raw_throttle <= SVJ_THROTTLE_IDLE_THRESHOLD) {
        enter_decelerating();
        return;
    }

    if (right_event_pending) {
        // Upshift out of redline (or, if gear 6, transition to CRUISING)
        if (simulated_gear >= SVJ_NUM_GEARS) {
            enter_cruising();
            return;
        }
        attempt_upshift();
        return;
    }
}

void SvjEngineSimulation::handle_cruising(float /*dt*/) {
    if (raw_throttle <= SVJ_THROTTLE_IDLE_THRESHOLD) {
        enter_decelerating();
        return;
    }
    if (left_event_pending && simulated_gear > 1) {
        // Cruise downshift drops us back into accelerating in a lower gear
        simulated_gear--;
        sm.halt_cruise_loop();
        sm.play_random_downshift_overlay();
        enter_accelerating(0);
    }
}

void SvjEngineSimulation::handle_decelerating(float /*dt*/) {
    if (raw_throttle > SVJ_THROTTLE_IDLE_THRESHOLD) {
        // Re-tap: 100ms crossfade into current gear's clip
        enter_accelerating(SVJ_DECEL_TO_ACCEL_CROSSFADE_MS);
        return;
    }
    if (left_event_pending) {
        attempt_downshift();
        return;
    }
    if (sm.is_decel_clip_finished()) {
        enter_idle();
        return;
    }
}

void SvjEngineSimulation::handle_neutral(float dt) {
    // Throttle controls rev sounds; never transitions on its own.
    sm.play_rev_for_throttle(raw_throttle, dt);

    if (right_event_pending) {
        exit_neutral_with_clunk();
        return;
    }
    // Left ignored
}

void SvjEngineSimulation::handle_launch_hold(float /*dt*/) {
    if (launch_firing) {
        // Servicing in main update(); ignore button inputs during launch
        return;
    }
    if (right_event_pending) {
        trigger_launch();
        return;
    }
    if (left_event_pending) {
        cancel_launch();
        return;
    }
}
