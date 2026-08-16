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
constexpr int NUM_MIXER_CHANNELS = 36;

// Throttle & Input

constexpr float THROTTLE_DEADZONE_LOW = 0.05f;
constexpr int THROTTLE_SMOOTHING_WINDOW_SIZE = 5;
constexpr int GESTURE_HISTORY_CAPACITY = 20;

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

// =============================================================================
// SVJ (Lamborghini) -- Timing & Thresholds
// =============================================================================

constexpr float SVJ_THROTTLE_IDLE_THRESHOLD       = 0.05f;
constexpr int   SVJ_IGNITION_CUT_MS               = 75;
constexpr int   SVJ_SHIFT_GEAR_FADE_OUT_MS        = 120;  // upshift: soften outgoing accel clip
constexpr int   SVJ_SHIFT_REDLINE_FADE_OUT_MS      = 150; // upshift from limiter
constexpr int   SVJ_IDLE_LEAVE_FADE_MS            = 220;
constexpr int   SVJ_IDLE_ENTER_FADE_MS            = 280;
constexpr int   SVJ_ACCEL_TO_REDLINE_CROSSFADE_MS = 480;
constexpr int   SVJ_REDLINE_AFTER_CLIP_FADE_IN_MS = 420;  // gear WAV ended; no tail to blend
constexpr float SVJ_REDLINE_CLIP_END_LEAD_SEC     = 0.55f; // start gear->limiter blend before WAV ends
constexpr int   SVJ_POST_SHIFT_GEAR_FADE_IN_MS    = 420;
constexpr int   SVJ_DECEL_FADE_IN_MS              = 200;
constexpr int   SVJ_DECEL_TO_ACCEL_CROSSFADE_MS   = 280;
constexpr float SVJ_LAUNCH_RELEASE_DELAY          = 1.5f;
constexpr int   SVJ_BACKFIRE_DELAY_MS             = 250;
constexpr int   SVJ_DOWNSHIFT_BACKFIRE_DELAY_MS   = 150;
// One-shot tail: frame-eased ramp (not Mix linear fade) so endings don't snap
constexpr int   SVJ_BACKFIRE_TAIL_FADE_MS             = 680;
constexpr int   SVJ_DOWNSHIFT_OVERLAY_TAIL_FADE_MS  = 560;
// Shrink scheduled clip length so the fade always starts before a hard stop
// (fixes abrupt ends when duration hints are a bit long vs real playback).
constexpr float SVJ_ONE_SHOT_TAIL_END_SAFETY          = 0.82f;
constexpr int   SVJ_NEUTRAL_REDLINE_HOLD_MS       = 500;
constexpr int   SVJ_REV_ZONE_CROSSFADE_MS         = 150;
constexpr int   SVJ_LAUNCH_FADE_OUT_MS            = 1500;
constexpr int   SVJ_CAR_SWITCH_FADE_MS            = 500;

// SVJ -- Volumes
constexpr float SVJ_NORMAL_IDLE_VOLUME            = 0.7f;
constexpr float SVJ_LOW_IDLE_VOLUME_DURING_SHIFT  = 0.15f;
constexpr float SVJ_PARTIAL_THROTTLE_FLOOR        = 0.15f;
constexpr float SVJ_PARTIAL_THROTTLE_POWER        = 1.8f;
constexpr float SVJ_GEAR_CLIP_FULL_VOLUME         = 1.0f;
constexpr float SVJ_REDLINE_VOLUME                = 1.0f;
constexpr float SVJ_CRUISE_VOLUME                 = 0.85f;
constexpr float SVJ_DECEL_VOLUME                  = 0.9f;
constexpr float SVJ_LAUNCH_LOOP_VOLUME            = 1.0f;
constexpr float SVJ_REV_VOLUME                    = 1.0f;
constexpr float SVJ_OVERLAY_VOLUME                = 1.0f;
// Multiplier for upshift pops (and neutral-entry pops); capped at full channel
constexpr float SVJ_POP_VOLUME_GAIN               = 1.48f;
constexpr float SVJ_IDLE_TRANSITION_SPEED         = 2.5f;

// SVJ -- Event Probabilities
constexpr float SVJ_UPSHIFT_NO_POP_PROBABILITY    = 0.25f;
constexpr float SVJ_DECEL_BACKFIRE_PROBABILITY    = 0.25f;
constexpr float SVJ_DOWNSHIFT_BACKFIRE_PROBABILITY = 0.65f;

// SVJ -- Gear Table (6-speed; 0.0f sentinel in 6th means "no redline, go to CRUISING")
constexpr int SVJ_NUM_GEARS = 6;
// Time in each gear before auto redline (6th uses 0 = cruise at clip end only)
constexpr std::array<float, SVJ_NUM_GEARS> SVJ_REDLINE_THRESHOLD_SEC = {
    3.2f, 5.0f, 7.2f, 9.5f, 12.0f, 0.0f
};

// SVJ -- NEUTRAL Rev Zone Throttle Bounds (rev-mode-only)
constexpr float SVJ_REV_LOW_MAX  = 0.35f;
constexpr float SVJ_REV_MED_MAX  = 0.70f;
constexpr float SVJ_REV_HIGH_MAX = 0.95f;

// SVJ -- Hardware Pins (turn signal buttons)
constexpr int SVJ_LEFT_SIGNAL_GPIO_PIN  = 23;
constexpr int SVJ_RIGHT_SIGNAL_GPIO_PIN = 24;

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

// SVJ channels (20-30)
constexpr int SVJ_CH_IDLE          = 20;
constexpr int SVJ_CH_GEAR_A        = 21;
constexpr int SVJ_CH_GEAR_B        = 22;
constexpr int SVJ_CH_REDLINE       = 23;
constexpr int SVJ_CH_CRUISE        = 24;
constexpr int SVJ_CH_DECEL         = 25;
constexpr int SVJ_CH_REV_A         = 26;
constexpr int SVJ_CH_REV_B         = 27;
constexpr int SVJ_CH_LAUNCH        = 28;
constexpr int SVJ_CH_SHIFT_OVERLAY = 29;  // pop on upshift / downshift overlay
constexpr int SVJ_CH_BACKFIRE      = 30;
constexpr int SVJ_CH_STARTUP       = 31;


// Sound File Paths (relative to working directory)

constexpr const char* M4_SOUND_FILES_PATH      = "m4";
constexpr const char* SUPRA_SOUND_FILES_PATH   = "supra";
constexpr const char* HELLCAT_SOUND_FILES_PATH = "hellcat";
constexpr const char* SVJ_SOUND_FILES_PATH     = "svj";

// Logging & Display

constexpr float       DISPLAY_UPDATE_INTERVAL = 0.1f;
constexpr const char* LOG_FILE_NAME           = "ev_sound_log.csv";