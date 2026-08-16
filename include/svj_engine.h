#pragma once

#include "engine_simulation.h"
#include "svj_sound_manager.h"
#include <random>

class SvjEngineSimulation : public EngineSimulation {
public:
    explicit SvjEngineSimulation(SvjSoundManager& sound_manager);

    void update(float dt, float new_throttle) override;
    void reset() override;
    float get_current_throttle() const override { return raw_throttle; }

    // Edge-triggered button events: called by the host before update().
    // Multiple presses between updates collapse to a single press.
    void on_left_signal();
    void on_right_signal();

    // Diagnostics / logging
    float raw_throttle      = 0.0f;
    float smoothed_throttle = 0.0f;
    int   simulated_gear    = 1;
    float gear_clip_elapsed = 0.0f;
    float launch_release_t  = 0.0f;
    bool  launch_firing     = false;
    int   current_rev_zone  = 0;

private:
    // State entry/exit helpers
    void enter_idle();
    void enter_accelerating(int crossfade_ms = 0);
    void enter_redline();
    void enter_cruising();
    void enter_decelerating();
    void enter_neutral();
    void exit_neutral_with_clunk();
    void enter_launch_hold();
    void trigger_launch();
    void cancel_launch();

    // Shift actions (moving states)
    void attempt_upshift();
    void attempt_downshift();

    // Per-frame state handlers
    void handle_engine_off(float dt);
    void handle_starting(float dt);
    void handle_idle(float dt);
    void handle_accelerating(float dt);
    void handle_redline(float dt);
    void handle_cruising(float dt);
    void handle_decelerating(float dt);
    void handle_neutral(float dt);
    void handle_launch_hold(float dt);

    // Helpers
    float partial_throttle_volume(float throttle) const;
    void  schedule_ignition_cut(int new_gear);

    SvjSoundManager& sm;

    bool left_event_pending  = false;
    bool right_event_pending = false;

    // Pending ignition-cut commit (upshift)
    bool  ignition_cut_pending = false;
    float ignition_cut_due_sec = 0.0f;
    int   ignition_cut_target_gear = 1;

    std::mt19937 rng;
};
