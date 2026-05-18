#pragma once

#include "engine_simulation.h"
#include "m4_sound_manager.h"
#include "ring_buffer.h"
#include <utility>

class M4EngineSimulation : public EngineSimulation {
public:
    explicit M4EngineSimulation(M4SoundManager& sound_manager);

    void update(float dt, float new_throttle) override;
    void reset() override;
    float get_current_throttle() const override { return current_throttle; }

private:
    void check_playful_gestures(float current_time, float old_throttle);

    M4SoundManager& sm;

    float current_throttle = 0.0f;
    RingBuffer<std::pair<float, float>, GESTURE_HISTORY_CAPACITY> throttle_history;
    float peak_throttle_in_gesture = 0.0f;
    float gesture_start_time       = 0.0f;
    bool  in_potential_gesture      = false;
    float gesture_lockout_until_time = 0.0f;

    float time_at_100_throttle   = 0.0f;
    float time_in_idle           = 0.0f;
    bool  played_full_accel_sequence_recently = false;
    float time_in_launch_control_range = 0.0f;

    float last_rev_sound_finish_time = 0.0f;
};
