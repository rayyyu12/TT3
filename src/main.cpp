#include "config.h"
#include "hardware.h"
#include "car_system.h"
#include "logging.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <csignal>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

#ifndef RASPI_HW
#ifdef _WIN32
#include <conio.h>
#endif
#endif

static std::atomic<bool> running{true};
static std::atomic<bool> switch_requested{false};
static std::atomic<int>  shared_adc_value{MIN_ADC_VALUE};

static void signal_handler(int /*sig*/) {
    running.store(false);
}

// ---------------------------------------------------------------------------
// Desktop keyboard input (non-Pi builds only)
// ---------------------------------------------------------------------------
#ifndef RASPI_HW

static float desktop_throttle = 0.0f;
static constexpr float THROTTLE_STEP       = 0.05f;
static constexpr float THROTTLE_STEP_FINE  = 0.01f;

static void poll_keyboard(bool& should_switch) {
#ifdef _WIN32
    while (_kbhit()) {
        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            int arrow = _getch();
            if (arrow == 72)      // Up arrow
                desktop_throttle = std::min(1.0f, desktop_throttle + THROTTLE_STEP);
            else if (arrow == 80) // Down arrow
                desktop_throttle = std::max(0.0f, desktop_throttle - THROTTLE_STEP);
            else if (arrow == 75) // Left arrow
                post_left_signal_event();
            else if (arrow == 77) // Right arrow
                post_right_signal_event();
        } else if (ch == 'w' || ch == 'W') {
            desktop_throttle = std::min(1.0f, desktop_throttle + THROTTLE_STEP);
        } else if (ch == 's' || ch == 'S') {
            desktop_throttle = std::max(0.0f, desktop_throttle - THROTTLE_STEP);
        } else if (ch == 'a' || ch == 'A') {
            desktop_throttle = std::min(1.0f, desktop_throttle + THROTTLE_STEP_FINE);
        } else if (ch == 'd' || ch == 'D') {
            desktop_throttle = std::max(0.0f, desktop_throttle - THROTTLE_STEP_FINE);
        } else if (ch == ' ') {
            should_switch = true;
        } else if (ch == '1') {
            desktop_throttle = 1.0f;
        } else if (ch == '0') {
            desktop_throttle = 0.0f;
        } else if (ch == 27) { // Escape
            running.store(false);
        }
    }
#else
    (void)should_switch;
#endif
}

#endif // !RASPI_HW

static void update_display(const CarSystem& system,
                           float smoothed, float raw, int raw_adc) {
    const EngineSimulation* engine = system.get_active_engine();
    const char* car = system.get_current_car_name();
    float idle_vol = 0.0f;
    std::string extra;

    std::string car_str(car);
    if (car_str == "M4") {
        const auto& sm = system.get_m4_sm();
        idle_vol = sm.idle_current_volume;
        float rpm = engine->get_simulated_rpm();
        const auto& eng = system.get_m4_engine();
        (void)eng;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "SimRPM: %-4.0f", rpm);
        extra = buf;
    } else if (car_str == "Supra") {
        const auto& sm = system.get_supra_sm();
        idle_vol = sm.idle_current_volume;
        float rpm = engine->get_simulated_rpm();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "SimRPM: %-4.0f", rpm);
        extra = buf;
    } else if (car_str == "Hellcat") {
        const auto& sm  = system.get_hellcat_sm();
        const auto& eng = system.get_hellcat_engine();
        idle_vol = sm.idle_current_volume;
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "SimRPM: %-4.0f | Gear: %d | Load: %5.2f | EMA: %.3f",
                      eng.get_simulated_rpm(), eng.simulated_gear,
                      eng.engine_load, eng.smoothed_throttle);
        extra = buf;
    } else {  // SVJ
        const auto& sm  = system.get_svj_sm();
        const auto& eng = system.get_svj_engine();
        idle_vol = sm.idle_current_volume;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "Gear: %d | ClipPos: %5.2fs | RevZone: %d | LaunchT: %4.2f | Firing: %s",
                      eng.simulated_gear,
                      eng.gear_clip_elapsed,
                      eng.current_rev_zone,
                      eng.launch_release_t,
                      eng.launch_firing ? "Y" : "N");
        extra = buf;
    }

    std::printf("\rCar: %-5s | State: %-15s | RawThr: %4.2f | SmoothThr: %4.2f | "
                "ADC: %-5d | IdleVol: %4.2f | %s | Switching: %s",
                car, engine->get_state_name(), raw, smoothed,
                raw_adc, idle_vol, extra.c_str(),
                system.switching_cars ? "true" : "false");
    std::fflush(stdout);
}

static bool init_sdl_mixer() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    int flags = MIX_INIT_OGG;
    Mix_Init(flags);

    if (Mix_OpenAudio(MIXER_FREQUENCY, MIX_DEFAULT_FORMAT,
                      MIXER_CHANNELS_STEREO, MIXER_BUFFER) < 0) {
        std::printf("Mix_OpenAudio failed: %s\n", Mix_GetError());
        std::printf("Attempting default Mix_OpenAudio...\n");
        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
            std::printf("CRITICAL: Mix_OpenAudio fallback also failed: %s\n",
                        Mix_GetError());
            return false;
        }
    }

    int allocated = Mix_AllocateChannels(NUM_MIXER_CHANNELS);
    std::printf("SDL_mixer initialized. Requested %d, got %d channels.\n",
                NUM_MIXER_CHANNELS, allocated);

    if (allocated < 32) {
        std::printf("CRITICAL WARNING: Only %d channels allocated, at least 32 recommended.\n",
                    allocated);
    }
    return true;
}

static void check_sound_files() {
    namespace fs = std::filesystem;

    auto check = [](const char* dir, const std::vector<std::string>& files) {
        bool any_missing = false;
        for (const auto& f : files) {
            auto path = fs::path(dir) / f;
            if (!fs::exists(path)) {
                std::printf("Warning: Essential sound file '%s' not found in '%s/'\n",
                            f.c_str(), dir);
                any_missing = true;
            }
        }
        return any_missing;
    };

    bool missing = false;

    missing |= check(M4_SOUND_FILES_PATH, {
        "engine_idle_loop.wav",
        "engine_rev_stage1.wav", "engine_rev_stage2.wav",
        "engine_rev_stage3.wav", "engine_rev_stage4.wav",
        "launch_control_engage.wav", "launch_control_hold_loop.wav",
        "acceleration_gears_1_to_4.wav"
    });

    missing |= check(SUPRA_SOUND_FILES_PATH, {
        "supra_idle_loop.wav", "supra_startup.wav",
        "light_pull_1.wav", "aggressive_push_1.wav", "violent_pull_1.wav",
        "supra_rev_stage1.wav", "supra_rev_stage2.wav",
        "supra_rev_stage3.wav", "supra_rev_stage4.wav"
    });

    missing |= check(HELLCAT_SOUND_FILES_PATH, {
        "hellcat_idle_loop.wav", "hellcat_startup_roar.wav",
        "hellcat_rumble_low_rpm_loop.wav", "hellcat_rumble_mid_rpm_loop.wav",
        "hellcat_whine_low_rpm_loop.wav", "hellcat_whine_high_rpm_loop.wav",
        "hellcat_exhaust_roar.wav", "hellcat_decel_burble.wav",
        "hellcat_upshift_bark.wav", "hellcat_downshift_revmatch1.wav",
        "hellcat_downshift_revmatch2.wav",
        "hellcat_rev_1.wav", "hellcat_rev_2.wav", "hellcat_rev_3.wav"
    });

    missing |= check(SVJ_SOUND_FILES_PATH, {
        "Startup.wav", "Idle.wav", "Redline.wav", "CruisingUnlooped.wav",
        "Deaccerleration1.wav", "LaunchControl.wav", "Backfire.wav",
        "1stG.wav", "2ndG.wav", "3rdG.wav", "4thG.wav", "5thG.wav", "6thG.wav",
        "Pop1.wav", "Pop2.wav", "Pop3.wav", "Pop4.wav",
        "Downshift1.wav", "Downshift2.wav", "Downshift3.wav",
        "LowRev1.wav", "LowRev2.wav",
        "MediumRev1.wav", "MediumRev2.wav",
        "HighRev1.wav", "HighRev2.wav"
    });

    if (missing) {
        std::printf("--- Some essential sound files are missing. "
                    "Functionality will be significantly affected. ---\n");
    }
}

int main() {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Apply real-time scheduling before any audio work (Pi only; no-op elsewhere)
    apply_realtime_scheduling();

    if (!init_sdl_mixer()) {
        return 1;
    }

#ifdef RASPI_HW
    std::printf("--- RUNNING WITH RASPBERRY PI ADC HARDWARE ---\n");
    if (!initialize_adc())
        std::printf("--- FAILED TO INITIALIZE ADC. SIMULATING 0%% THROTTLE ---\n");
    if (!initialize_button(running, switch_requested))
        std::printf("--- BUTTON DISABLED ---\n");
    if (!initialize_signal_buttons())
        std::printf("--- TURN SIGNAL BUTTONS DISABLED ---\n");

    // Pin the main (audio) thread to core 3 and start the ADC thread on core 2
    pin_to_core(3);
    start_adc_thread(running, shared_adc_value);
#else
    std::printf("--- RUNNING IN SIMULATED ADC MODE (NO RASPBERRY PI HARDWARE) ---\n");
#endif

    std::filesystem::create_directories(M4_SOUND_FILES_PATH);
    std::filesystem::create_directories(SUPRA_SOUND_FILES_PATH);
    std::filesystem::create_directories(HELLCAT_SOUND_FILES_PATH);
    std::filesystem::create_directories(SVJ_SOUND_FILES_PATH);

    check_sound_files();

    CarSystem system;
    DataLogger logger;

    std::printf("\nMulti-Car EV Sound Simulation Running (Headless)...\n");
    std::printf("Starting car: %s\n", system.get_current_car_name());
    std::printf("Throttle smoothing window: %d samples\n", THROTTLE_SMOOTHING_WINDOW_SIZE);
    std::printf("M4 - Staged Rev System Active. Simulating RPM: Idle %.0f, Decay %.0f/s\n",
                M4_RPM_IDLE, M4_RPM_DECAY_RATE_PER_SEC);
    std::printf("M4 - Gesture Retrigger Lockout: %.1fs\n", M4_GESTURE_RETRIGGER_LOCKOUT);
    std::printf("M4 - Launch Control: Hold throttle %.0f%%-%.0f%% for %.1fs\n",
                M4_LAUNCH_CONTROL_THROTTLE_MIN * 100.0f,
                M4_LAUNCH_CONTROL_THROTTLE_MAX * 100.0f,
                M4_LAUNCH_CONTROL_HOLD_DURATION);
    std::printf("Supra - ENHANCED Engine with EMA Throttle (a=%.1f) and PRE_ACCEL State\n",
                SUPRA_EMA_ALPHA);
    std::printf("Supra - Staged Rev System Active. Simulating RPM: Idle %.0f, Decay %.0f/s\n",
                SUPRA_RPM_IDLE, SUPRA_RPM_DECAY_RATE_PER_SEC);
    std::printf("Supra - Rev Gesture Lockout: %.1fs | Pre-Accel Delay: %.2fs\n",
                SUPRA_REV_RETRIGGER_LOCKOUT, SUPRA_PRE_ACCEL_DELAY);
    std::printf("Hellcat - Virtual Engine with EMA Throttle (a=%.1f) and Automatic Transmission\n",
                HELLCAT_EMA_ALPHA);
    std::printf("Hellcat - RPM Simulation: Idle %.0f, Redline %.0f, 5-Speed Auto\n",
                HELLCAT_IDLE_RPM, HELLCAT_REDLINE_RPM);
    std::printf("Hellcat - Foundation Layer: Idle + Rumble + Supercharger Whine with Crossfading\n");
    std::printf("SVJ - Manual 6-speed paddle-shift with throttle-modal buttons\n");
    std::printf("SVJ -   Moving: right=upshift, left=downshift (DECEL only)\n");
    std::printf("SVJ -   IDLE:   right=NEUTRAL rev mode, left=LAUNCH CONTROL\n");
    std::printf("SVJ -   NEUTRAL: right=exit (clunk), throttle->LowRev/MedRev/HighRev/Redline\n");
    std::printf("SVJ -   LAUNCH: right=launch (1.5s delay, gear 1), left=cancel\n");
    std::printf("SVJ - Ignition cut: %d ms, decel->accel crossfade: %d ms\n",
                SVJ_IGNITION_CUT_MS, SVJ_DECEL_TO_ACCEL_CROSSFADE_MS);
    std::printf("SVJ - Pops: %.0f%% no-pop on upshift | Backfire: %.0f%% accel->decel, %.0f%% downshift to 1st/2nd\n",
                SVJ_UPSHIFT_NO_POP_PROBABILITY * 100.0f,
                SVJ_DECEL_BACKFIRE_PROBABILITY * 100.0f,
                SVJ_DOWNSHIFT_BACKFIRE_PROBABILITY * 100.0f);
    std::printf("SVJ - Turn signals: left=GPIO %d, right=GPIO %d (active-low, pull-up)\n",
                SVJ_LEFT_SIGNAL_GPIO_PIN, SVJ_RIGHT_SIGNAL_GPIO_PIN);
    std::printf("Throttle Input: ADC P%d -> %d (0%%) to %d (100%%)\n",
                ADC_CHANNEL_NUMBER, MIN_ADC_VALUE, MAX_ADC_VALUE);
    std::printf("Button: GPIO %d (short press = switch car, long press = shutdown)\n",
                BUTTON_GPIO_PIN);
    std::printf("Log file will be: %s\n", LOG_FILE_NAME);
#ifndef RASPI_HW
    std::printf("\n--- DESKTOP CONTROLS ---\n");
    std::printf("  W / Up Arrow    = throttle up (+5%%)\n");
    std::printf("  S / Down Arrow  = throttle down (-5%%)\n");
    std::printf("  A              = throttle up fine (+1%%)\n");
    std::printf("  D              = throttle down fine (-1%%)\n");
    std::printf("  1              = full throttle (100%%)\n");
    std::printf("  0              = release throttle (0%%)\n");
    std::printf("  Left Arrow     = left turn signal  (SVJ: downshift / launch / cancel)\n");
    std::printf("  Right Arrow    = right turn signal (SVJ: upshift / neutral / launch fire)\n");
    std::printf("  Space          = switch car\n");
    std::printf("  Escape         = quit\n");
    std::printf("------------------------\n\n");
#else
    std::printf("Press Ctrl+C to exit gracefully.\n\n");
#endif

    auto last_time = std::chrono::steady_clock::now();
    auto last_display = last_time;
    const auto display_interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(DISPLAY_UPDATE_INTERVAL));
    const auto frame_duration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(1.0f / FPS));

    while (running.load(std::memory_order_acquire)) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_time).count();
        if (dt <= 0.0f) dt = 1.0f / FPS;
        last_time = now;

#ifdef RASPI_HW
        if (switch_requested.exchange(false, std::memory_order_acq_rel))
            system.switch_car();

        int raw_adc = shared_adc_value.load(std::memory_order_relaxed);
        float raw_throttle = get_throttle_percentage(raw_adc);
#else
        bool kb_switch = false;
        poll_keyboard(kb_switch);
        if (kb_switch)
            system.switch_car();

        int raw_adc = static_cast<int>(desktop_throttle * (MAX_ADC_VALUE - MIN_ADC_VALUE)) + MIN_ADC_VALUE;
        float raw_throttle = desktop_throttle;
#endif

        CarSystem::InputEvents events;
        events.left  = consume_left_signal_event();
        events.right = consume_right_signal_event();

        auto [smoothed, raw_out] = system.update(dt, raw_throttle, events);

        logger.record(system, smoothed, raw_out, raw_adc, dt);

        if (now - last_display >= display_interval) {
            update_display(system, smoothed, raw_out, raw_adc);
            last_display = now;
        }

        auto processing = std::chrono::steady_clock::now() - now;
        auto sleep_time = frame_duration - processing;
        if (sleep_time.count() > 0)
            std::this_thread::sleep_for(sleep_time);
    }

    std::printf("\r%200s\r", "");
    std::printf("\nInterrupt received. Shutting down...\n");
    std::printf("Initiating final cleanup...\n");

    // Wait for ADC thread to finish
    stop_adc_thread();

    if (!logger.empty()) {
        std::printf("Writing log data to %s...\n", LOG_FILE_NAME);
        logger.write_csv(LOG_FILE_NAME);
    } else {
        std::printf("No log data to write.\n");
    }

    std::printf("Stopping all sounds...\n");
    system.get_m4_sm().stop_all_sounds();
    system.get_supra_sm().stop_all_sounds();
    system.get_hellcat_sm().stop_all_sounds();
    system.get_svj_sm().stop_all_sounds();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

#ifdef RASPI_HW
    cleanup_gpio();
#endif

    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    std::printf("Shutdown complete.\n");

    return 0;
}
