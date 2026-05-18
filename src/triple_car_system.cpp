#include "triple_car_system.h"
#include <SDL2/SDL.h>
#include <cstdio>

TripleCarSystem::TripleCarSystem()
    : m4_engine(m4_sound_manager),
      supra_engine(supra_sound_manager),
      hellcat_engine(hellcat_sound_manager)
{
    throttle_buffer.fill(0.0f);
}

void TripleCarSystem::switch_car() {
    if (switching_cars) {
        std::printf("Switch already in progress, ignoring request (current: %s)\n",
                    cars[current_car_index]);
        return;
    }

    int old_index = current_car_index;
    current_car_index = (current_car_index + 1) % NUM_CARS;

    std::printf("\nCAR SWITCH: %s (index %d) -> %s (index %d)\n",
                cars[old_index], old_index,
                cars[current_car_index], current_car_index);

    switching_cars = true;
    switch_start_time = SDL_GetTicks() / 1000.0f;

    switch (old_index) {
        case 0: m4_sound_manager.fade_out_all_sounds(SUPRA_CROSSFADE_DURATION_MS); break;
        case 1: supra_sound_manager.fade_out_all_sounds(SUPRA_CROSSFADE_DURATION_MS); break;
        case 2: hellcat_sound_manager.fade_out_all_sounds(SUPRA_CROSSFADE_DURATION_MS); break;
    }
}

TripleCarSystem::UpdateResult TripleCarSystem::update(float dt, float raw_throttle) {
    throttle_buffer.push(raw_throttle);
    float smoothed = throttle_buffer.average();

    float current_time = SDL_GetTicks() / 1000.0f;

    if (switching_cars) {
        float fade_sec = SUPRA_CROSSFADE_DURATION_MS / 1000.0f;
        if (current_time - switch_start_time >= fade_sec) {
            int old_index = (current_car_index + NUM_CARS - 1) % NUM_CARS;

            switch (old_index) {
                case 0: m4_sound_manager.stop_all_sounds(); break;
                case 1: supra_sound_manager.stop_all_sounds(); break;
                case 2: hellcat_sound_manager.stop_all_sounds(); break;
            }

            switch (current_car_index) {
                case 0: m4_engine.reset(); break;
                case 1: supra_engine.reset(); break;
                case 2: hellcat_engine.reset(); break;
            }

            std::printf("SWITCH COMPLETE: Now using %s (index %d)\n",
                        cars[current_car_index], current_car_index);
            switching_cars = false;
        } else {
            return { smoothed, raw_throttle };
        }
    }

    switch (current_car_index) {
        case 0: m4_engine.update(dt, smoothed);       break;
        case 1: supra_engine.update(dt, raw_throttle); break;
        case 2: hellcat_engine.update(dt, raw_throttle); break;
    }

    return { smoothed, raw_throttle };
}

EngineSimulation* TripleCarSystem::get_active_engine() {
    switch (current_car_index) {
        case 0:  return &m4_engine;
        case 1:  return &supra_engine;
        default: return &hellcat_engine;
    }
}

const EngineSimulation* TripleCarSystem::get_active_engine() const {
    switch (current_car_index) {
        case 0:  return &m4_engine;
        case 1:  return &supra_engine;
        default: return &hellcat_engine;
    }
}
