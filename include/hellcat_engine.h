#pragma once

#include "engine_simulation.h"
#include "hellcat_sound_manager.h"

class HellcatEngineSimulation : public EngineSimulation {
public:
    explicit HellcatEngineSimulation(HellcatSoundManager& sound_manager);

    void update(float dt, float new_throttle) override;
    void reset() override;
    float get_current_throttle() const override { return raw_throttle; }

    float raw_throttle       = 0.0f;
    float smoothed_throttle  = 0.0f;
    float engine_load        = 0.0f;
    int   simulated_gear     = 1;
    float last_shift_time    = 0.0f;

private:
    void handle_idle_state(float current_time, float dt);
    void handle_driving_state(float current_time, float dt);
    void check_simple_rev_gestures(float current_time);

    HellcatSoundManager& sm;

    float previous_throttle  = 0.0f;
    float state_start_time   = 0.0f;

    bool  in_simple_rev_gesture    = false;
    float rev_gesture_start_time   = 0.0f;
    float rev_peak_throttle        = 0.0f;
    float rev_lockout_until        = 0.0f;
};
