#pragma once

#include "config.h"
#include <string>
#include <vector>
#include <map>

class CarSystem;

struct LogEntry {
    double  timestamp_unix    = 0.0;
    std::string datetime_iso;
    float   dt                = 0.0f;
    std::string active_car;
    bool    switching_cars    = false;
    std::string state;
    int     raw_adc           = 0;
    float   raw_throttle_pct  = 0.0f;
    float   smoothed_throttle_pct = 0.0f;
    float   engine_throttle_pct   = 0.0f;
    float   idle_target_vol   = 0.0f;
    float   idle_current_vol  = 0.0f;
    bool    idle_is_fading    = false;

    std::map<std::string, std::string> extra_fields;
};

class DataLogger {
public:
    DataLogger() { entries.reserve(FPS * 600); }

    void record(const CarSystem& system,
                float smoothed_throttle, float raw_throttle,
                int raw_adc, float dt);

    void write_csv(const std::string& filename) const;
    bool empty() const { return entries.empty(); }

private:
    std::vector<LogEntry> entries;
};
