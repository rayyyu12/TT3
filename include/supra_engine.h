#pragma once

#include "engine_simulation.h"
#include "supra_sound_manager.h"
#include "ring_buffer.h"
#include <string>
#include <utility>

// Optional: lightweight struct for the audio queue (replaces Python dict)
struct SupraAudioQueueEntry {
    std::string range;
    std::string sound_type;
};

class SupraEngineSimulation : public EngineSimulation {
public:
    explicit SupraEngineSimulation(SupraSoundManager& sound_manager);

    void update(float dt, float new_throttle) override;
    void reset() override;
    float get_current_throttle() const override { return raw_throttle; }

private:
    // State handlers (mirror the Python _handle_* methods)
    void handle_idle_state(float current_time, float previous_raw_throttle);
    void handle_pre_accel_state(float current_time);
    void handle_accelerating_state(float current_time);
    void handle_cruising_state(float current_time);
    void handle_decelerating_state(float current_time);

    // Convenience wrappers that forward to the sound manager
    bool play_pull_sound_for_range(const std::string& range_name);
    bool play_cruise_sound_for_range(const std::string& range_name,
                                     const std::string& cruise_type);

    // Rev gesture detection (adapted from M4 but with Supra-specific tuning)
    void check_rev_gestures(float current_time, float old_throttle);

    SupraSoundManager& sm;

    float raw_throttle = 0.0f;
    float ema_throttle = 0.0f;

    RingBuffer<std::pair<float, float>, GESTURE_HISTORY_CAPACITY> throttle_history;
    bool  in_potential_rev_gesture       = false;
    float rev_gesture_start_time         = 0.0f;
    float peak_throttle_in_rev_gesture   = 0.0f;
    float rev_gesture_lockout_until_time = 0.0f;

    float state_start_time = 0.0f;

    // Audio queue: when a clip is playing and the throttle range changes,
    // the new range is queued and played once the current clip ends.
    bool has_audio_queue = false;
    SupraAudioQueueEntry audio_queue;

    std::string current_playing_sound_range;

    float last_rev_sound_finish_time = 0.0f;
};
