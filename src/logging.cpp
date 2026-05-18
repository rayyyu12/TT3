#include "logging.h"
#include "triple_car_system.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <set>
#include <algorithm>
#include <cstdio>

static std::string iso_now() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

void DataLogger::record(const TripleCarSystem& system,
                        float smoothed_throttle, float raw_throttle,
                        int raw_adc, float dt) {
    auto now = std::chrono::system_clock::now();
    double unix_ts = std::chrono::duration<double>(
        now.time_since_epoch()).count();

    const EngineSimulation* engine = system.get_active_engine();
    const char* car_name = system.get_current_car_name();

    LogEntry e;
    e.timestamp_unix    = unix_ts;
    e.datetime_iso      = iso_now();
    e.dt                = dt;
    e.active_car        = car_name;
    e.switching_cars    = system.switching_cars;
    e.state             = engine->get_state_name();
    e.raw_adc           = raw_adc;
    e.raw_throttle_pct  = raw_throttle;
    e.smoothed_throttle_pct = smoothed_throttle;
    e.engine_throttle_pct = engine->get_current_throttle();

    if (e.active_car == "M4") {
        const auto& eng = system.get_m4_engine();
        const auto& sm  = system.get_m4_sm();
        e.idle_target_vol  = sm.idle_target_volume;
        e.idle_current_vol = sm.idle_current_volume;
        e.idle_is_fading   = sm.idle_is_fading;
        e.extra_fields["m4_sim_rpm"] =
            std::to_string(eng.get_simulated_rpm());
        e.extra_fields["m4_lc_sounds_active"] =
            sm.launch_control_sounds_active ? "true" : "false";
    } else if (e.active_car == "Supra") {
        const auto& eng = system.get_supra_engine();
        const auto& sm  = system.get_supra_sm();
        e.idle_target_vol  = sm.idle_target_volume;
        e.idle_current_vol = sm.idle_current_volume;
        e.idle_is_fading   = sm.idle_is_fading;
        e.extra_fields["supra_sim_rpm"] =
            std::to_string(eng.get_simulated_rpm());
    } else {
        const auto& eng = system.get_hellcat_engine();
        const auto& sm  = system.get_hellcat_sm();
        e.idle_target_vol  = sm.idle_target_volume;
        e.idle_current_vol = sm.idle_current_volume;
        e.idle_is_fading   = sm.idle_is_fading;
        e.extra_fields["hellcat_sim_rpm"] =
            std::to_string(eng.get_simulated_rpm());
        e.extra_fields["hellcat_sim_gear"] =
            std::to_string(eng.simulated_gear);
        e.extra_fields["hellcat_engine_load"] =
            std::to_string(eng.engine_load);
        e.extra_fields["hellcat_smoothed_throttle"] =
            std::to_string(eng.smoothed_throttle);
    }

    entries.push_back(std::move(e));
}

static void escape_csv(std::ostream& out, const std::string& s) {
    if (s.find(',') != std::string::npos || s.find('"') != std::string::npos) {
        out << '"';
        for (char c : s) {
            if (c == '"') out << '"';
            out << c;
        }
        out << '"';
    } else {
        out << s;
    }
}

void DataLogger::write_csv(const std::string& filename) const {
    if (entries.empty()) return;

    // Collect all extra field names
    std::set<std::string> extra_keys;
    for (const auto& e : entries)
        for (const auto& kv : e.extra_fields)
            extra_keys.insert(kv.first);

    std::vector<std::string> base_cols = {
        "timestamp_unix", "datetime_iso", "dt", "active_car", "switching_cars",
        "state", "raw_adc", "raw_throttle_pct", "smoothed_throttle_pct",
        "engine_throttle_pct", "idle_target_vol", "idle_current_vol",
        "idle_is_fading"
    };

    std::ofstream out(filename);
    if (!out.is_open()) {
        std::printf("Error: cannot open %s for writing\n", filename.c_str());
        return;
    }

    for (size_t i = 0; i < base_cols.size(); ++i) {
        if (i > 0) out << ',';
        out << base_cols[i];
    }
    for (const auto& k : extra_keys)
        out << ',' << k;
    out << '\n';

    for (const auto& e : entries) {
        out << std::fixed << std::setprecision(6) << e.timestamp_unix << ',';
        escape_csv(out, e.datetime_iso); out << ',';
        out << e.dt << ',';
        escape_csv(out, e.active_car); out << ',';
        out << (e.switching_cars ? "true" : "false") << ',';
        escape_csv(out, e.state); out << ',';
        out << e.raw_adc << ',';
        out << e.raw_throttle_pct << ',';
        out << e.smoothed_throttle_pct << ',';
        out << e.engine_throttle_pct << ',';
        out << e.idle_target_vol << ',';
        out << e.idle_current_vol << ',';
        out << (e.idle_is_fading ? "true" : "false");

        for (const auto& k : extra_keys) {
            out << ',';
            auto it = e.extra_fields.find(k);
            if (it != e.extra_fields.end())
                escape_csv(out, it->second);
        }
        out << '\n';
    }

    std::printf("Log successfully written to %s (%zu entries)\n",
                filename.c_str(), entries.size());
}
