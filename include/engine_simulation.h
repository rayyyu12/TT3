#pragma once

#include "config.h"
#include <string>

// Union of all states used by M4, Supra, and Hellcat engines.
// Each engine only uses a subset, but a single enum lets the
// TripleCarSystem and logging code work with any engine generically.
enum class EngineState {
    ENGINE_OFF,
    STARTING,
    IDLING,         // M4
    IDLE,           // Supra, Hellcat
    PLAYFUL_REV,    // M4
    PRE_ACCEL,      // Supra
    ACCELERATING,   // M4, Supra
    CRUISING,       // M4, Supra
    DECELERATING,   // M4, Supra
    LAUNCH_HOLD,    // M4
    DRIVING         // Hellcat
};

inline const char* engine_state_to_string(EngineState s) {
    switch (s) {
        case EngineState::ENGINE_OFF:   return "ENGINE_OFF";
        case EngineState::STARTING:     return "STARTING";
        case EngineState::IDLING:       return "IDLING";
        case EngineState::IDLE:         return "IDLE";
        case EngineState::PLAYFUL_REV:  return "PLAYFUL_REV";
        case EngineState::PRE_ACCEL:    return "PRE_ACCEL";
        case EngineState::ACCELERATING: return "ACCELERATING";
        case EngineState::CRUISING:     return "CRUISING";
        case EngineState::DECELERATING: return "DECELERATING";
        case EngineState::LAUNCH_HOLD:  return "LAUNCH_HOLD";
        case EngineState::DRIVING:      return "DRIVING";
    }
    return "UNKNOWN";
}

class EngineSimulation {
public:
    virtual ~EngineSimulation() = default;

    virtual void update(float dt, float new_throttle) = 0;

    EngineState get_state() const { return state; }
    float get_simulated_rpm() const { return simulated_rpm; }

    const char* get_state_name() const {
        return engine_state_to_string(state);
    }

protected:
    EngineState state = EngineState::ENGINE_OFF;
    float simulated_rpm = 0.0f;
};
