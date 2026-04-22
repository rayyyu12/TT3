#pragma once

#include <array>
#include <cstdint>

// Global

constexpr int FPS = 60;
constexpr float MASTER_ENGINE_VOL = 0.99f;

// Hardware -- ADC & Button

constexpr int ADC_CHANNEL_NUMBER = 0;
constexpr int MIN_ADC_VALUE = 15823;
constexpr int MAX_ADC_VALUE = 65535;

constexpr int BUTTON_GPIO_PIN = 17;
constexpr float BUTTON_LONG_PRESS_TIME = 2.0f;
constexpr float BUTTON_DEBOUNCE_TIME = 0.1f;

// Audio Mixer (SDL_mixer init params)

constexpr int MIXER_FREQUENCY = 44100;
constexpr int MIXER_SIZE = -16;
constexpr int MIXER_CHANNELS_STEREO = 2;
constexpr int MIXER_BUFFER = 512;
constexpr int NUM_MIXER_CHANNELS = 25;

// Throttle & Input

constexpr float THROTTLE_DEADZONE_LOW = 0.05f;
constexpr int THROTTLE_SMOOTHING_WINDOW_SIZE = 5;

// =============================================================================
// M4 -- Timing & Thresholds
// =============================================================================

constexpr float M4_SUSTAINED_100_THROTTLE_TIME = 1.0f;
constexpr float M4_GESTURE_WINDOW_TIME         = 0.75f;
constexpr int   M4_GESTURE_MAX_POINTS          = 20;
constexpr int   M4_FADE_OUT_MS                 = 300;
constexpr int   M4_CROSSFADE_DURATION_MS       = 500;
constexpr float M4_IDLE_TRANSITION_SPEED       = 2.5f;
constexpr float M4_TURBO_BOV_COOLDOWN          = 1.0f;
constexpr float M4_HIGH_REV_LIMITER_COOLDOWN   = 1.5f;
constexpr float M4_FULL_ACCEL_RESET_IDLE_TIME  = 3.0f;
constexpr float M4_GESTURE_RETRIGGER_LOCKOUT   = 0.3f;

// M4 -- Volumes
constexpr float M4_NORMAL_IDLE_VOLUME              = 0.7f;
constexpr float M4_LOW_IDLE_VOLUME_DURING_SFX      = 0.15f;
constexpr float M4_VERY_LOW_IDLE_VOLUME_DURING_LAUNCH = 0.05f;
constexpr float M4_STAGED_REV_VOLUME               = 0.9f;

// M4 -- Launch Control
constexpr float M4_LAUNCH_CONTROL_THROTTLE_MIN  = 0.55f;
constexpr float M4_LAUNCH_CONTROL_THROTTLE_MAX  = 0.85f;
constexpr float M4_LAUNCH_CONTROL_HOLD_DURATION = 0.5f;
constexpr bool  M4_LAUNCH_CONTROL_BRAKE_REQUIRED = false;
constexpr float M4_LAUNCH_CONTROL_ENGAGE_VOL    = 1.0f;
constexpr float M4_LAUNCH_CONTROL_HOLD_VOL      = 1.0f;

// M4 -- Acceleration Sound Offsets
constexpr float M4_ACCELERATION_SOUND_OFFSET        = 0.5f;
constexpr float M4_LAUNCH_ACCELERATION_SOUND_OFFSET = 1.0f;

// M4 -- RPM Simulation
constexpr float M4_RPM_IDLE                        = 800.0f;
constexpr float M4_RPM_DECAY_RATE_PER_SEC          = 1500.0f;
constexpr float M4_RPM_DECAY_COOLDOWN_AFTER_REV    = 0.1f;
constexpr float M4_RPM_RESET_TO_IDLE_THRESHOLD_TIME = 6.0f;

// =============================================================================
// Supra -- Timing & Thresholds
// =============================================================================

constexpr float SUPRA_NORMAL_IDLE_VOLUME          = 0.7f;
constexpr float SUPRA_LOW_IDLE_VOLUME_DURING_REV  = 0.2f;
constexpr float SUPRA_REV_GESTURE_WINDOW_TIME     = 0.75f;
constexpr float SUPRA_REV_RETRIGGER_LOCKOUT       = 0.5f;
constexpr float SUPRA_CLIP_OVERLAP_PREVENTION_TIME = 0.2f;
constexpr float SUPRA_PRE_ACCEL_DELAY             = 0.15f;
constexpr int   SUPRA_CROSSFADE_DURATION_MS       = 800;
constexpr float SUPRA_IDLE_TRANSITION_SPEED       = 2.5f;
constexpr float SUPRA_CRUISE_TRANSITION_DELAY     = 0.75f;
constexpr float SUPRA_HIGHWAY_CRUISE_THRESHOLD    = 0.90f;
constexpr float SUPRA_STAGED_REV_VOLUME           = 0.9f;

// Supra -- EMA Throttle
constexpr float SUPRA_EMA_ALPHA = 0.3f;

// Supra -- RPM Simulation
constexpr float SUPRA_RPM_IDLE                        = 900.0f;
constexpr float SUPRA_RPM_DECAY_RATE_PER_SEC          = 1200.0f;
constexpr float SUPRA_RPM_DECAY_COOLDOWN_AFTER_REV    = 0.1f;
constexpr float SUPRA_RPM_RESET_TO_IDLE_THRESHOLD_TIME = 6.0f;

// =============================================================================
// Hellcat -- Timing & Thresholds
// =============================================================================

constexpr float HELLCAT_NORMAL_IDLE_VOLUME          = 0.7f;
constexpr float HELLCAT_LOW_IDLE_VOLUME_DURING_SHIFT = 0.3f;
constexpr int   HELLCAT_CROSSFADE_DURATION          = 500;
constexpr float HELLCAT_THROTTLE_IDLE_THRESHOLD     = 0.05f;
constexpr int   HELLCAT_FADE_IN_DURATION            = 150;
constexpr int   HELLCAT_FADE_OUT_DURATION           = 300;

// Hellcat -- Virtual Engine Physics
constexpr float HELLCAT_RPM_ACCEL_BASE        = 1200.0f;
constexpr float HELLCAT_RPM_DECAY_COAST       = 2000.0f;
constexpr float HELLCAT_RPM_THROTTLE_LIFT_DECAY = 3500.0f;
constexpr float HELLCAT_IDLE_RPM              = 750.0f;
constexpr float HELLCAT_REDLINE_RPM           = 4500.0f;

// Hellcat -- Gear Tables (indexed by gear - 1, so index 0 = gear 1)
constexpr int HELLCAT_NUM_GEARS = 5;

constexpr std::array<float, HELLCAT_NUM_GEARS> HELLCAT_GEAR_ACCEL_MULTIPLIERS = {
    1.2f, 0.9f, 0.7f, 0.5f, 0.4f
};

constexpr std::array<float, HELLCAT_NUM_GEARS> HELLCAT_GEAR_ENGINE_BRAKING = {
    2000.0f, 1500.0f, 1200.0f, 800.0f, 600.0f
};

constexpr std::array<float, HELLCAT_NUM_GEARS> HELLCAT_GEAR_DOWNSHIFT_THRESHOLDS = {
    0.0f, 1400.0f, 1800.0f, 2200.0f, 2800.0f
};

// Hellcat -- Audio Inertia & Shifting
constexpr float HELLCAT_AUDIO_INERTIA_SPEED = 3.0f;
constexpr float HELLCAT_MIN_SHIFT_INTERVAL  = 2.5f;

// Hellcat -- EMA Throttle
constexpr float HELLCAT_EMA_ALPHA = 0.2f;

// Channel Definitions (SDL_mixer channel indices)

// M4 channels (0-4)
constexpr int M4_CH_IDLE              = 0;
constexpr int M4_CH_TURBO_LIMITER_SFX = 1;
constexpr int M4_CH_LONG_SEQUENCE_A   = 2;
constexpr int M4_CH_LONG_SEQUENCE_B   = 3;
constexpr int M4_CH_STAGED_REV_SOUND  = 4;

// Supra channels (5-8)
constexpr int SUPRA_CH_IDLE      = 5;
constexpr int SUPRA_CH_DRIVING_A = 6;
constexpr int SUPRA_CH_DRIVING_B = 7;
constexpr int SUPRA_CH_REV_SFX   = 8;

// Hellcat channels (9-19)
// Foundation Layer
constexpr int HELLCAT_CH_IDLE       = 9;
constexpr int HELLCAT_CH_RUMBLE_LOW = 10;
constexpr int HELLCAT_CH_RUMBLE_MID = 11;
constexpr int HELLCAT_CH_WHINE_LOW_A = 12;
constexpr int HELLCAT_CH_WHINE_LOW_B = 13;
constexpr int HELLCAT_CH_WHINE_HIGH_A = 14;
constexpr int HELLCAT_CH_WHINE_HIGH_B = 15;
// Character Layer
constexpr int HELLCAT_CH_ACCEL_RESPONSE = 16;
constexpr int HELLCAT_CH_DECEL_BURBLE   = 17;
// SFX Layer
constexpr int HELLCAT_CH_STARTUP  = 18;
constexpr int HELLCAT_CH_SHIFT_SFX = 19;


// Sound File Paths (relative to working directory)

constexpr const char* M4_SOUND_FILES_PATH      = "m4";
constexpr const char* SUPRA_SOUND_FILES_PATH   = "supra";
constexpr const char* HELLCAT_SOUND_FILES_PATH = "hellcat";

// Logging & Display

constexpr float       DISPLAY_UPDATE_INTERVAL = 0.1f;
constexpr const char* LOG_FILE_NAME           = "ev_sound_log.csv";