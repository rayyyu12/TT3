#pragma once

#include "config.h"
#include "m4_sound_manager.h"
#include "supra_sound_manager.h"
#include "hellcat_sound_manager.h"
#include "svj_sound_manager.h"
#include "m4_engine.h"
#include "supra_engine.h"
#include "hellcat_engine.h"
#include "svj_engine.h"
#include "ring_buffer.h"
#include <array>
#include <string>
#include <memory>

class CarSystem {
public:
    CarSystem();

    CarSystem(const CarSystem&) = delete;
    CarSystem& operator=(const CarSystem&) = delete;

    void switch_car();

    struct InputEvents {
        bool left  = false;
        bool right = false;
    };

    struct UpdateResult {
        float smoothed_throttle;
        float raw_throttle;
    };
    UpdateResult update(float dt, float raw_throttle, InputEvents events = {});

    EngineSimulation* get_active_engine();
    const EngineSimulation* get_active_engine() const;

    M4SoundManager&       get_m4_sm()       { return m4_sound_manager; }
    SupraSoundManager&    get_supra_sm()    { return supra_sound_manager; }
    HellcatSoundManager&  get_hellcat_sm()  { return hellcat_sound_manager; }
    SvjSoundManager&      get_svj_sm()      { return svj_sound_manager; }
    const M4SoundManager&       get_m4_sm()       const { return m4_sound_manager; }
    const SupraSoundManager&    get_supra_sm()    const { return supra_sound_manager; }
    const HellcatSoundManager&  get_hellcat_sm()  const { return hellcat_sound_manager; }
    const SvjSoundManager&      get_svj_sm()      const { return svj_sound_manager; }

    M4EngineSimulation&       get_m4_engine()       { return m4_engine; }
    SupraEngineSimulation&    get_supra_engine()    { return supra_engine; }
    HellcatEngineSimulation&  get_hellcat_engine()  { return hellcat_engine; }
    SvjEngineSimulation&      get_svj_engine()      { return svj_engine; }
    const M4EngineSimulation&       get_m4_engine()       const { return m4_engine; }
    const SupraEngineSimulation&    get_supra_engine()    const { return supra_engine; }
    const HellcatEngineSimulation&  get_hellcat_engine()  const { return hellcat_engine; }
    const SvjEngineSimulation&      get_svj_engine()      const { return svj_engine; }

    int current_car_index = 0;
    bool switching_cars   = false;

    const char* get_current_car_name() const { return cars[current_car_index]; }

    static constexpr int NUM_CARS = 4;
    static constexpr int CAR_INDEX_M4      = 0;
    static constexpr int CAR_INDEX_SUPRA   = 1;
    static constexpr int CAR_INDEX_HELLCAT = 2;
    static constexpr int CAR_INDEX_SVJ     = 3;

private:
    static constexpr const char* cars[NUM_CARS] = { "M4", "Supra", "Hellcat", "SVJ" };

    M4SoundManager       m4_sound_manager;
    SupraSoundManager    supra_sound_manager;
    HellcatSoundManager  hellcat_sound_manager;
    SvjSoundManager      svj_sound_manager;

    M4EngineSimulation       m4_engine;
    SupraEngineSimulation    supra_engine;
    HellcatEngineSimulation  hellcat_engine;
    SvjEngineSimulation      svj_engine;

    float switch_start_time = 0.0f;

    RingBuffer<float, THROTTLE_SMOOTHING_WINDOW_SIZE> throttle_buffer;
};

// Backwards-compat alias during the rename
using TripleCarSystem = CarSystem;
