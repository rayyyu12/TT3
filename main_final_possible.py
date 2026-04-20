#version 4 - Corrected and Refined
import pygame
import os
import time
import random
import sys
import signal
import csv
import datetime
import collections
import subprocess
from abc import ABC, abstractmethod

# Attempt to import Raspberry Pi specific modules
try:
    import board
    import busio
    import digitalio
    import adafruit_mcp3xxx.mcp3008 as MCP
    from adafruit_mcp3xxx.analog_in import AnalogIn
    import RPi.GPIO as GPIO
    RASPI_HW_AVAILABLE = True
except ImportError:
    RASPI_HW_AVAILABLE = False
except RuntimeError:
    RASPI_HW_AVAILABLE = False

# --- Constants and Parameters ---
FPS = 60

# --- Hardware Configuration ---
ADC_CHANNEL_NUMBER = 0
MIN_ADC_VALUE = 15823
MAX_ADC_VALUE = 65535
BUTTON_PIN = 17
BUTTON_LONG_PRESS_TIME = 3.0
BUTTON_DEBOUNCE_TIME = 0.3

# --- Audio Configuration ---
MIXER_FREQUENCY = 44100
MIXER_SIZE = -16
MIXER_CHANNELS_STEREO = 2
MIXER_BUFFER = 512
NUM_PYGAME_MIXER_CHANNELS = 16

# --- Centralized Master Volume Control ---
# All sound volumes are scaled by this value for consistent balancing.
# Set this between 0.0 (silent) and 1.0 (full volume). 0.8 is a good starting point.
MASTER_VOLUME = 0.3

# --- Throttle and Input ---
THROTTLE_DEADZONE_LOW = 0.05
THROTTLE_SMOOTHING_WINDOW_SIZE = 5

# --- Channel Definitions ---
CH_IDLE = 0
CH_TURBO_LIMITER_SFX = 1
CH_LONG_SEQUENCE_A = 2
CH_LONG_SEQUENCE_B = 3
CH_STAGED_REV_SOUND = 4
CH_STARTUP = 5

# --- Sound Profiles ---
SOUND_PROFILES = ["m4", "supra"]
current_profile_index = 0

# --- Logging ---
DISPLAY_UPDATE_INTERVAL = 0.1
LOG_FILE_NAME = "ev_sound_log.csv"

# --- Global State Variables ---
adc_throttle_channel = None
button_press_start_time = None
last_button_state = GPIO.HIGH if RASPI_HW_AVAILABLE else 1
log_data = []
running_script = True

def initialize_gpio():
    """Initialize GPIO for button input"""
    if not RASPI_HW_AVAILABLE:
        return False
    try:
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        print(f"GPIO initialized. Button on pin {BUTTON_PIN}")
        return True
    except Exception as e:
        print(f"Error initializing GPIO: {e}")
        return False

def initialize_adc():
    global adc_throttle_channel
    if not RASPI_HW_AVAILABLE:
        print("ADC hardware modules not available. Cannot initialize ADC.")
        return False
    try:
        spi = busio.SPI(clock=board.SCK, MISO=board.MISO, MOSI=board.MOSI)
        cs = digitalio.DigitalInOut(board.D8)
        mcp = MCP.MCP3008(spi, cs)
        adc_throttle_channel = AnalogIn(mcp, getattr(MCP, f"P{ADC_CHANNEL_NUMBER}"))
        print(f"MCP3008 ADC initialized on channel P{ADC_CHANNEL_NUMBER}.")
        return True
    except Exception as e:
        print(f"FATAL ERROR initializing ADC: {e}")
        adc_throttle_channel = None
        return False

def read_adc_value():
    global adc_throttle_channel
    if adc_throttle_channel:
        try:
            return adc_throttle_channel.value
        except Exception:
            return MIN_ADC_VALUE
    else:
        # Simulate 0% throttle if ADC is not available
        return MIN_ADC_VALUE

def get_throttle_percentage_from_adc(raw_adc_value):
    if MAX_ADC_VALUE == MIN_ADC_VALUE: return 0.0
    clamped_value = max(MIN_ADC_VALUE, min(raw_adc_value, MAX_ADC_VALUE))
    percentage = (clamped_value - MIN_ADC_VALUE) / (MAX_ADC_VALUE - MIN_ADC_VALUE)
    return percentage

def handle_button():
    """Handle button press logic - returns 'switch' for short press, 'shutdown' for long press, None otherwise"""
    global button_press_start_time, last_button_state
    
    if not RASPI_HW_AVAILABLE:
        return None
        
    try:
        current_button_state = GPIO.input(BUTTON_PIN)
        current_time = time.time()
        
        # Button pressed (LOW when pressed due to pull-up)
        if current_button_state == GPIO.LOW and last_button_state == GPIO.HIGH:
            button_press_start_time = current_time
        
        # Button held
        elif current_button_state == GPIO.LOW and button_press_start_time:
            hold_duration = current_time - button_press_start_time
            if hold_duration >= BUTTON_LONG_PRESS_TIME:
                return 'shutdown'
        
        # Button released
        elif current_button_state == GPIO.HIGH and last_button_state == GPIO.LOW and button_press_start_time:
            press_duration = current_time - button_press_start_time
            button_press_start_time = None
            
            if press_duration < BUTTON_LONG_PRESS_TIME:
                return 'switch'
        
        last_button_state = current_button_state
        
    except Exception as e:
        print(f"Button handling error: {e}")
    
    return None

class SoundManager:
    def __init__(self, sound_profile="m4"):
        self.sounds = {}
        self.rev_stages = []
        self.sound_profile = sound_profile
        
        # Initialize channels
        self.channel_idle = pygame.mixer.Channel(CH_IDLE)
        self.channel_turbo_limiter_sfx = pygame.mixer.Channel(CH_TURBO_LIMITER_SFX)
        self.channel_startup = pygame.mixer.Channel(CH_STARTUP)
        self.channel_staged_rev = pygame.mixer.Channel(CH_STAGED_REV_SOUND) if pygame.mixer.get_num_channels() > CH_STAGED_REV_SOUND else None
        self.channel_long_A = pygame.mixer.Channel(CH_LONG_SEQUENCE_A)
        self.channel_long_B = pygame.mixer.Channel(CH_LONG_SEQUENCE_B)
        
        self.active_long_channel = self.channel_long_A
        self.transitioning_long_sound = False
        self.transition_start_time = 0
        self.crossfade_duration_ms = 500
        
        # Volume management (relative volumes)
        self.idle_target_volume = 0.7
        self.idle_current_volume = 0.7
        self.idle_is_fading = False
        
        # Launch control state
        self.waiting_for_launch_hold_loop = False
        self.launch_control_sounds_active = False
        self.just_switched_to_lc_hold = False
        
        self.load_sounds()

    def _load_sound_with_duration(self, filename):
        path = os.path.join(self.sound_profile, filename)
        if os.path.exists(path):
            try:
                sound = pygame.mixer.Sound(path)
                return sound, sound.get_length()
            except pygame.error as e:
                print(f"Warning: Could not load '{filename}': {e}")
        return None, 0

    def load_sounds(self):
        """Load sounds based on current profile"""
        self.sounds.clear()
        self.rev_stages.clear()
        
        if self.sound_profile == "supra":
            self.crossfade_duration_ms = 200
        else:
            self.crossfade_duration_ms = 500
            
        print(f"Loading sounds from '{self.sound_profile}' folder...")
        
        # Dynamically call the loading method for the current profile
        load_method = getattr(self, f"_load_{self.sound_profile}_sounds", None)
        if callable(load_method):
            load_method()
        else:
            print(f"Warning: No sound loading method found for profile '{self.sound_profile}'")
            
    def _load_m4_sounds(self):
        """Load M4 sound set"""
        self.sounds['idle'], _ = self._load_sound_with_duration("engine_idle_loop.wav")
        
        # Staged rev sounds
        stages_to_load = [("engine_rev_stage1.wav", 3000), ("engine_rev_stage2.wav", 5000), 
                          ("engine_rev_stage3.wav", 7000), ("engine_rev_stage4.wav", 8500)]
        for i, (fname, rpm) in enumerate(stages_to_load):
            snd, dur = self._load_sound_with_duration(fname)
            if snd: self.rev_stages.append({'key': f'rev_stage{i+1}', 'sound': snd, 'rpm_peak': rpm, 'duration': dur})
        
        self.sounds['turbo_bov'], _ = self._load_sound_with_duration("turbo_spool_and_bov.wav")
        self.sounds['rev_limiter'], _ = self._load_sound_with_duration("engine_high_rev_with_limiter.wav")
        self.sounds['accel_gears'], _ = self._load_sound_with_duration("acceleration_gears_1_to_4.wav")
        self.sounds['cruising'], _ = self._load_sound_with_duration("engine_cruising_loop.wav")
        self.sounds['decel_downshifts'], _ = self._load_sound_with_duration("deceleration_downshifts_to_idle.wav")
        self.sounds['starter'], _ = self._load_sound_with_duration("engine_starter.wav")
        self.sounds['launch_control_engage'], _ = self._load_sound_with_duration("launch_control_engage.wav")
        self.sounds['launch_control_hold_loop'], _ = self._load_sound_with_duration("launch_control_hold_loop.wav")
        
    def _load_supra_sounds(self):
        """Load Supra sound set"""
        self.sounds['startup'], self.sounds['startup_duration'] = self._load_sound_with_duration("supra_startup.wav")
        self.sounds['idle'], _ = self._load_sound_with_duration("supra_idle_loop.wav")
        
        # Load sounds and their durations into the dictionary
        sound_files = [
            'light_pull_1', 'light_pull_2', 'light_cruise_1', 'light_cruise_2', 'light_cruise_3',
            'aggressive_push_1', 'aggressive_push_2', 'aggressive_push_3', 'aggressive_push_4', 'aggressive_push_5', 'aggressive_push_6',
            'violent_pull_1', 'violent_pull_2', 'violent_pull_3',
            'highway_cruise_loop'
        ]
        for fname in sound_files:
            self.sounds[fname], self.sounds[f'{fname}_duration'] = self._load_sound_with_duration(f"{fname}.wav")

        for i in range(1, 4):
            key = f'rev_{i}'
            snd, dur = self._load_sound_with_duration(f"supra_rev_{i}.wav")
            if snd: self.rev_stages.append({'key': key, 'sound': snd, 'intensity': i, 'duration': dur})

    def play_startup(self):
        """Play startup sound for current profile"""
        startup_sound = self.sounds.get('startup') or self.sounds.get('starter')
        if startup_sound and not self.channel_startup.get_busy():
            self.channel_startup.set_volume(MASTER_VOLUME)
            self.channel_startup.play(startup_sound)
            return True
        return False

    def switch_profile(self, new_profile):
        """Switch to a new sound profile"""
        print(f"\nSwitching from {self.sound_profile} to {new_profile}")
        self.sound_profile = new_profile
        self.stop_all_sounds(fade_ms=100)
        self.load_sounds()
        self.play_startup()  # Play startup sound for the new car

    def play_idle(self):
        idle_sound = self.sounds.get('idle')
        if idle_sound:
            if not self.channel_idle.get_busy() or self.channel_idle.get_sound() != idle_sound:
                self.channel_idle.play(idle_sound, loops=-1)
            self.channel_idle.set_volume(self.idle_current_volume * MASTER_VOLUME)

    def set_idle_target_volume(self, target_volume, instant=False):
        target_volume = max(0.0, min(1.0, target_volume))
        self.idle_target_volume = target_volume
        if instant:
            self.idle_current_volume = target_volume
            if self.channel_idle.get_sound():
                self.channel_idle.set_volume(self.idle_current_volume * MASTER_VOLUME)
            self.idle_is_fading = False
        else:
            if abs(self.idle_current_volume - self.idle_target_volume) > 0.01:
                self.idle_is_fading = True

    def update_idle_fade(self, dt):
        if self.idle_is_fading and self.channel_idle.get_busy():
            fade_speed = 2.5 if self.sound_profile == "m4" else 4.0
            
            if abs(self.idle_current_volume - self.idle_target_volume) < 0.01:
                self.idle_current_volume = self.idle_target_volume
                self.idle_is_fading = False
            elif self.idle_current_volume < self.idle_target_volume:
                self.idle_current_volume = min(self.idle_current_volume + fade_speed * dt, self.idle_target_volume)
            else:
                self.idle_current_volume = max(self.idle_current_volume - fade_speed * dt, self.idle_target_volume)
            
            if self.channel_idle.get_sound():
                self.channel_idle.set_volume(self.idle_current_volume * MASTER_VOLUME)

    def play_long_sequence(self, sound_key, loops=0, transition_from_other=False):
        sound_to_play = self.sounds.get(sound_key)
        if not sound_to_play: return

        target_volume = 1.0 * MASTER_VOLUME

        if not transition_from_other:
            other_channel = self.channel_long_B if self.active_long_channel == self.channel_long_A else self.channel_long_A
            other_channel.stop()
            self.active_long_channel.set_volume(target_volume)
            self.active_long_channel.play(sound_to_play, loops=loops)
            self.transitioning_long_sound = False
        else:
            fade_out_channel = self.active_long_channel
            fade_in_channel = self.channel_long_B if self.active_long_channel == self.channel_long_A else self.channel_long_A
            fade_out_channel.fadeout(self.crossfade_duration_ms)
            fade_in_channel.set_volume(0) # Start silent for the fade-in
            fade_in_channel.play(sound_to_play, loops=loops)
            self.active_long_channel = fade_in_channel
            self.transitioning_long_sound = True
            self.transition_start_time = time.time()

    def update_long_sequence_crossfade(self):
        if self.transitioning_long_sound:
            elapsed_time_ms = (time.time() - self.transition_start_time) * 1000
            progress = min(1.0, elapsed_time_ms / self.crossfade_duration_ms)
            
            target_volume = progress * MASTER_VOLUME
            
            if self.active_long_channel.get_busy():
                self.active_long_channel.set_volume(target_volume)
            if progress >= 1.0:
                self.transitioning_long_sound = False

    def stop_all_sounds(self, fade_ms=0):
        if fade_ms > 0:
            pygame.mixer.fadeout(fade_ms)
        else:
            pygame.mixer.stop()
                
    def update(self):
        """Update sound manager state, primarily for launch control."""
        self.just_switched_to_lc_hold = False
        
        lc_engage_sound = self.sounds.get('launch_control_engage')
        lc_hold_sound = self.sounds.get('launch_control_hold_loop')
        current_sfx_sound_at_call = self.channel_turbo_limiter_sfx.get_sound()

        if self.waiting_for_launch_hold_loop:
            if not self.channel_turbo_limiter_sfx.get_busy() or current_sfx_sound_at_call != lc_engage_sound:
                if lc_hold_sound:
                    self.channel_turbo_limiter_sfx.set_volume(MASTER_VOLUME)
                    self.channel_turbo_limiter_sfx.play(lc_hold_sound, loops=-1)
                    self.just_switched_to_lc_hold = True
                else:
                    self.launch_control_sounds_active = False
                self.waiting_for_launch_hold_loop = False
        
        if self.launch_control_sounds_active and not self.just_switched_to_lc_hold:
            sfx_sound_now = self.channel_turbo_limiter_sfx.get_sound()
            sfx_channel_busy_now = self.channel_turbo_limiter_sfx.get_busy()

            if not self.waiting_for_launch_hold_loop and \
               (not sfx_channel_busy_now or \
                (sfx_sound_now != lc_engage_sound and sfx_sound_now != lc_hold_sound)):
                self.launch_control_sounds_active = False

    def play_staged_rev(self, gesture_peak_throttle):
        """Play rev sound based on gesture intensity"""
        if not self.channel_staged_rev or self.channel_staged_rev.get_busy():
            return None
        
        rev_volume = 0.9 * MASTER_VOLUME
        selected = None

        if self.sound_profile == "supra":
            if len(self.rev_stages) >= 3:
                if gesture_peak_throttle <= 0.4: selected = self.rev_stages[0]
                elif gesture_peak_throttle <= 0.7: selected = self.rev_stages[1]
                else: selected = self.rev_stages[2]
            elif self.rev_stages: selected = self.rev_stages[0]
                    
            if selected and selected['sound']:
                self.channel_staged_rev.set_volume(rev_volume)
                self.channel_staged_rev.play(selected['sound'])
                return {'duration': selected['duration']}
        else: # M4 rev logic
            if self.rev_stages:
                if gesture_peak_throttle <= 0.4: selected = self.rev_stages[0]
                elif gesture_peak_throttle <= 0.75: selected = self.rev_stages[1] if len(self.rev_stages) > 1 else self.rev_stages[0]
                else: selected = self.rev_stages[2] if len(self.rev_stages) > 2 else self.rev_stages[-1]
                
            if selected and selected['sound']:
                self.channel_staged_rev.set_volume(rev_volume)
                self.channel_staged_rev.play(selected['sound'])
                return {'rpm_peak': selected['rpm_peak'], 'duration': selected['duration']}
        return None

    def stop_long_sequence(self, fade_ms=0):
        if fade_ms > 0:
            self.channel_long_A.fadeout(fade_ms)
            self.channel_long_B.fadeout(fade_ms)
        else:
            self.channel_long_A.stop()
            self.channel_long_B.stop()
        self.transitioning_long_sound = False

    def is_long_sequence_busy(self):
        return self.channel_long_A.get_busy() or self.channel_long_B.get_busy() or self.transitioning_long_sound

# --- Behavior Classes ---

class BaseBehavior(ABC):
    """Abstract base class for car sound behaviors."""
    def __init__(self, sound_manager):
        self.sm = sound_manager
        self.state = "ENGINE_OFF"
        self.current_throttle = 0.0
        self.time_in_state = 0.0
        
    @abstractmethod
    def update(self, dt, new_throttle_value):
        pass

class M4Behavior(BaseBehavior):
    def __init__(self, sound_manager):
        super().__init__(sound_manager)
        self.throttle_history = collections.deque(maxlen=20)
        self.peak_throttle_in_gesture = 0.0
        self.gesture_start_time = 0.0
        self.in_potential_gesture = False
        self.gesture_lockout_until_time = 0.0
        
        self.time_at_100_throttle = 0.0
        self.time_in_idle = 0.0
        self.played_full_accel_sequence_recently = False
        self.time_in_launch_control_range = 0.0
        
        self.simulated_rpm = 800
        self.last_rev_sound_finish_time = 0.0
        
        # M4 specific constants
        self.SUSTAINED_100_THROTTLE_TIME = 1.5
        self.GESTURE_WINDOW_TIME = 0.75
        self.GESTURE_RETRIGGER_LOCKOUT = 0.3
        self.LAUNCH_CONTROL_THROTTLE_MIN = 0.55
        self.LAUNCH_CONTROL_THROTTLE_MAX = 0.85
        self.LAUNCH_CONTROL_HOLD_DURATION = 0.5
        self.RPM_DECAY_RATE_PER_SEC = 1500
        self.RPM_DECAY_COOLDOWN_AFTER_REV = 0.1

    def update(self, dt, new_throttle_value):
        previous_throttle = self.current_throttle
        self.current_throttle = new_throttle_value
        current_time = time.time()
        self.time_in_state += dt

        self.throttle_history.append((current_time, self.current_throttle))
        
        self.sm.update_long_sequence_crossfade()
        self.sm.update_idle_fade(dt)
        self.sm.update()

        is_rev_sound_playing = self.sm.channel_staged_rev and self.sm.channel_staged_rev.get_busy()
        if not is_rev_sound_playing and current_time > self.last_rev_sound_finish_time + self.RPM_DECAY_COOLDOWN_AFTER_REV:
            if self.simulated_rpm > 800:
                self.simulated_rpm = max(800, self.simulated_rpm - self.RPM_DECAY_RATE_PER_SEC * dt)

        # --- State Machine Logic ---
        if self.state == "ENGINE_OFF":
            if self.current_throttle > THROTTLE_DEADZONE_LOW + 0.05:
                self.state = "STARTING"
                self.sm.play_startup()
                self.time_in_state = 0
                
        elif self.state == "STARTING":
            if not self.sm.channel_startup.get_busy():
                self.state = "IDLING"
                self.sm.set_idle_target_volume(0.7)
                self.sm.play_idle()
                self.time_in_idle = 0
                self.time_in_state = 0
                
        elif self.state == "IDLING" or self.state == "PLAYFUL_REV":
            # If we are in the rev state but the rev sound has finished,
            # transition back to IDLING so other actions can be taken.
            if self.state == "PLAYFUL_REV" and not is_rev_sound_playing:
                self.state = "IDLING"
                self.sm.set_idle_target_volume(0.7) # Restore idle volume
            
            if self.state == "IDLING":
                self.time_in_idle += dt
                if self.time_in_idle > 3.0:
                    self.played_full_accel_sequence_recently = False
                    
            # Launch control check
            is_in_lc_range = (self.LAUNCH_CONTROL_THROTTLE_MIN < self.current_throttle < self.LAUNCH_CONTROL_THROTTLE_MAX)
            if is_in_lc_range:
                self.time_in_launch_control_range += dt
                if self.time_in_launch_control_range >= self.LAUNCH_CONTROL_HOLD_DURATION:
                    self.state = "LAUNCH_HOLD"
                    # ... launch control sound logic is handled in the sound manager
            else:
                self.time_in_launch_control_range = 0.0
                
            self._check_playful_gestures(current_time, previous_throttle)
                
            # Full acceleration check
            if self.current_throttle >= 0.98:
                self.time_at_100_throttle += dt
                if self.time_at_100_throttle >= self.SUSTAINED_100_THROTTLE_TIME and not self.played_full_accel_sequence_recently:
                    self.state = "ACCELERATING"
                    self.sm.set_idle_target_volume(0.0, instant=True)
                    self.sm.play_long_sequence('accel_gears')
                    self.played_full_accel_sequence_recently = True
                    self.time_in_state = 0
            else:
                self.time_at_100_throttle = 0.0

        elif self.state == "ACCELERATING":
            self.sm.set_idle_target_volume(0.0, instant=True)
            if self.current_throttle < 0.90:
                self.state = "DECELERATING"
                self.sm.play_long_sequence('decel_downshifts', transition_from_other=True)
            elif not self.sm.is_long_sequence_busy():
                self.state = "CRUISING"
                self.sm.play_long_sequence('cruising', loops=-1, transition_from_other=True)
            self.time_in_state = 0
                    
        elif self.state == "CRUISING":
            if self.current_throttle < 0.90:
                self.state = "DECELERATING"
                self.sm.play_long_sequence('decel_downshifts', transition_from_other=True)
                self.time_in_state = 0
                
        elif self.state == "DECELERATING":
            if self.current_throttle >= 0.95:
                self.state = "CRUISING"
                self.sm.play_long_sequence('cruising', loops=-1, transition_from_other=True)
            elif not self.sm.is_long_sequence_busy():
                self.state = "IDLING"
                self.sm.set_idle_target_volume(0.7)
                self.sm.play_idle()
            self.time_in_state = 0

    def _check_playful_gestures(self, current_time, previous_throttle):
        # This gesture logic is only checked if not in a driving state.
        if self.state not in ["IDLING", "PLAYFUL_REV"]:
            return

        if not self.in_potential_gesture and current_time >= self.gesture_lockout_until_time:
            is_rising = self.current_throttle > THROTTLE_DEADZONE_LOW and previous_throttle <= THROTTLE_DEADZONE_LOW
            if is_rising:
                self.in_potential_gesture = True
                self.gesture_start_time = current_time
                self.peak_throttle_in_gesture = self.current_throttle
                
        if self.in_potential_gesture:
            self.peak_throttle_in_gesture = max(self.peak_throttle_in_gesture, self.current_throttle)
            
            if current_time - self.gesture_start_time > self.GESTURE_WINDOW_TIME:
                self.in_potential_gesture = False
                return
                
            is_falling_back_to_zero = (self.current_throttle < previous_throttle) and (self.current_throttle <= THROTTLE_DEADZONE_LOW * 1.5)
                         
            if is_falling_back_to_zero and self.peak_throttle_in_gesture > THROTTLE_DEADZONE_LOW + 0.02:
                rev_info = self.sm.play_staged_rev(self.peak_throttle_in_gesture)
                if rev_info:
                    self.state = "PLAYFUL_REV" # Transition to the revving state
                    self.simulated_rpm = rev_info.get('rpm_peak', 800)
                    self.last_rev_sound_finish_time = current_time + rev_info['duration']
                    self.sm.set_idle_target_volume(0.15)
                # Reset gesture detection
                self.in_potential_gesture = False
                self.gesture_lockout_until_time = current_time + self.GESTURE_RETRIGGER_LOCKOUT

class SupraBehavior(BaseBehavior):
    def __init__(self, sound_manager):
        super().__init__(sound_manager)
        self.previous_throttle = 0.0
        
        # FIX: Changed to store clip names to prevent repetition
        self.last_aggressive_clip_name = None
        self.last_violent_clip_name = None
        self.last_light_clip_name = None
        
        self.gesture_start_time = 0.0
        self.peak_throttle_in_gesture = 0.0
        self.in_potential_gesture = False
        
        self.current_playing_clip = None

    def update(self, dt, new_throttle_value):
        self.previous_throttle = self.current_throttle
        self.current_throttle = new_throttle_value
        current_time = time.time()
        self.time_in_state += dt
        
        self.sm.update_long_sequence_crossfade()
        self.sm.update_idle_fade(dt)
        self.sm.update()
        
        throttle_delta = self.current_throttle - self.previous_throttle
        throttle_drop_rate = max(0, -throttle_delta / dt) if dt > 0 else 0
        
        # --- State Machine ---
        if self.state == "ENGINE_OFF":
            if self.current_throttle > THROTTLE_DEADZONE_LOW:
                self.state = "STARTING"
                self.sm.play_startup()
                self.time_in_state = 0
                
        elif self.state == "STARTING":
            startup_duration = self.sm.sounds.get('startup_duration', 2.0)
            if not self.sm.channel_startup.get_busy() and self.time_in_state > 1.0: # Ensure startup sound finishes
                self.state = "IDLE"
                self.sm.set_idle_target_volume(0.7)
                self.sm.play_idle()
                self.time_in_state = 0
                
        elif self.state == "IDLE":
            self.sm.set_idle_target_volume(0.7) # Ensure idle volume is restored
            if not self.sm.channel_idle.get_busy():
                self.sm.play_idle()
                
            if self._check_rev_gesture(current_time):
                rev_info = self.sm.play_staged_rev(self.peak_throttle_in_gesture)
                if rev_info:
                    self.sm.set_idle_target_volume(0.2, instant=False)
                    self._reset_gesture_detection()
                return
                
            if self.current_throttle >= 0.10:
                # FIX: Replaced non-existent 'stop_idle' with consistent volume control method.
                self.sm.set_idle_target_volume(0.0, instant=True)
                if self.current_throttle <= 0.30: self.state = "LIGHT_CRUISE"
                elif self.current_throttle <= 0.60: self.state = "AGGRESSIVE_PUSH"
                else: self.state = "VIOLENT_PULL"
                self.time_in_state = 0
                self.play_clip_for_state() # Play clip immediately on transition
                
        else: # Handle all driving states
            # If throttle drops to zero, always go back to idle
            if self.current_throttle < 0.05 and self.state != "IDLE":
                self.state = "IDLE"
                self.sm.stop_long_sequence(fade_ms=300)
                self.sm.play_idle()
                self.time_in_state = 0
                return

            # If a clip is playing, let it finish or be interrupted
            if self.sm.is_long_sequence_busy():
                 # Interrupt violent pull on sharp throttle drop
                if self.state == "VIOLENT_PULL" and throttle_drop_rate > 0.7 and self.current_throttle < 0.40:
                    self.sm.stop_long_sequence(fade_ms=200)
                return

            # If no clip is playing, decide on the next one based on throttle
            throttle = self.current_throttle
            next_state = self.state # Default to current state
            
            if throttle < 0.10: next_state = "IDLE"
            elif throttle <= 0.35: next_state = "LIGHT_CRUISE"
            elif throttle <= 0.70: next_state = "AGGRESSIVE_PUSH"
            else: next_state = "VIOLENT_PULL"

            if self.state != next_state or not self.sm.is_long_sequence_busy():
                self.state = next_state
                self.play_clip_for_state()

    def play_clip_for_state(self):
        """Selects and plays a sound clip based on the current state, avoiding immediate repeats."""
        clips = []
        last_clip_attr = None
        loops = 0
        
        if self.state == "LIGHT_CRUISE":
            clips = ['light_cruise_1', 'light_cruise_2', 'light_cruise_3']
            last_clip_attr = 'last_light_clip_name'
            loops = -1 # Cruising sounds should loop
        elif self.state == "AGGRESSIVE_PUSH":
            clips = [f'aggressive_push_{i}' for i in range(1, 7)]
            last_clip_attr = 'last_aggressive_clip_name'
        elif self.state == "VIOLENT_PULL":
            clips = [f'violent_pull_{i}' for i in range(1, 4)]
            last_clip_attr = 'last_violent_clip_name'
        elif self.state == "IDLE":
            self.sm.play_idle()
            return
            
        if not clips: return
        
        # FIX: Logic to prevent the same clip from playing twice in a row.
        last_played = getattr(self, last_clip_attr, None) if last_clip_attr else None
        
        selectable_clips = [clip for clip in clips if clip != last_played]
        
        # If filtering left no options (e.g., only one clip in the list), fall back to the full list.
        if not selectable_clips:
            selectable_clips = clips
            
        chosen_clip = random.choice(selectable_clips)
        
        if last_clip_attr:
            setattr(self, last_clip_attr, chosen_clip)
        
        if self.sm.sounds.get(chosen_clip):
            self.sm.play_long_sequence(chosen_clip, loops=loops, transition_from_other=True)


    def _check_rev_gesture(self, current_time):
        if not self.in_potential_gesture and self.current_throttle > 0.05 and self.previous_throttle <= 0.05:
            self.in_potential_gesture = True
            self.gesture_start_time = current_time
            self.peak_throttle_in_gesture = self.current_throttle
                
        if self.in_potential_gesture:
            self.peak_throttle_in_gesture = max(self.peak_throttle_in_gesture, self.current_throttle)
            
            # Use a short time window for a quick "blip" gesture
            if current_time - self.gesture_start_time > 0.6:
                self._reset_gesture_detection()
                return False
                
            # Trigger if throttle drops significantly after a peak
            if self.current_throttle < self.peak_throttle_in_gesture * 0.5 and self.peak_throttle_in_gesture > 0.15:
                return True
        return False
        
    def _reset_gesture_detection(self):
        self.in_potential_gesture = False
        self.gesture_start_time = 0
        self.peak_throttle_in_gesture = 0

def signal_handler_main(sig, frame):
    global running_script
    if running_script:
        print("\nInterrupt received. Shutting down...")
        running_script = False

def update_display(behavior, throttle_smooth, throttle_raw, adc_raw, profile):
    # Added RPM display for M4 for better diagnostics
    rpm_display = f"RPM: {int(behavior.simulated_rpm)}" if isinstance(behavior, M4Behavior) else ""
    status = f"Profile: {profile:<6} | State: {behavior.state:<17} | Throttle: {throttle_smooth:>4.2f} | ADC: {adc_raw:<5} | {rpm_display}"
    # Use ANSI escape code to clear the line before printing
    sys.stdout.write("\r\033[K" + status)
    sys.stdout.flush()

def main():
    global running_script, current_profile_index, log_data
    
    signal.signal(signal.SIGINT, signal_handler_main)
    signal.signal(signal.SIGTERM, signal_handler_main)
    
    pygame.init()
    pygame.mixer.init(frequency=MIXER_FREQUENCY, size=MIXER_SIZE, channels=MIXER_CHANNELS_STEREO, buffer=MIXER_BUFFER)
    pygame.mixer.set_num_channels(NUM_PYGAME_MIXER_CHANNELS)
    
    print(f"Pygame Mixer initialized with {pygame.mixer.get_num_channels()} channels")
    
    if RASPI_HW_AVAILABLE:
        initialize_gpio()
        if not initialize_adc():
            print("ADC initialization failed, using simulated throttle")
    else:
        print("Running in simulated mode (no Raspberry Pi hardware)")
    
    active_profile_name = SOUND_PROFILES[current_profile_index]
    sound_manager = SoundManager(active_profile_name)
    
    if active_profile_name == "m4":
        behavior = M4Behavior(sound_manager)
    else:
        behavior = SupraBehavior(sound_manager)
    
    throttle_buffer = collections.deque(maxlen=THROTTLE_SMOOTHING_WINDOW_SIZE)
    for _ in range(THROTTLE_SMOOTHING_WINDOW_SIZE): throttle_buffer.append(0.0)
    
    print("\nEV Sound Simulator - Multi-Car Edition (v4 Corrected)")
    print(f"MASTER VOLUME: {MASTER_VOLUME*100:.0f}%")
    print(f"Available profiles: {', '.join(SOUND_PROFILES)}")
    print(f"Current profile: {active_profile_name}")
    print(f"Button: Short press = switch car, Long hold ({BUTTON_LONG_PRESS_TIME}s) = shutdown")
    print("Press Ctrl+C to exit\n")
    
    sound_manager.play_startup()
    
    last_time = time.time()
    last_display_update = 0
    button_action_cooldown = 0
    button_action = None
    
    try:
        while running_script:
            current_time = time.time()
            dt = current_time - last_time
            if dt <= 0: dt = 1.0 / FPS
            last_time = current_time
            
            if button_action_cooldown <= 0:
                button_action = handle_button()
                if button_action == 'switch':
                    current_profile_index = (current_profile_index + 1) % len(SOUND_PROFILES)
                    new_profile = SOUND_PROFILES[current_profile_index]
                    
                    # Create a new sound manager and behavior instance on profile switch
                    sound_manager.stop_all_sounds() # Stop old sounds before creating new manager
                    sound_manager = SoundManager(new_profile)
                    
                    if new_profile == "m4":
                        behavior = M4Behavior(sound_manager)
                    else:
                        behavior = SupraBehavior(sound_manager)
                    
                    sound_manager.play_startup()
                    button_action_cooldown = BUTTON_DEBOUNCE_TIME
                    
                elif button_action == 'shutdown':
                    print("\nShutdown initiated via button...")
                    running_script = False
                    break
                    
            if button_action_cooldown > 0:
                button_action_cooldown -= dt
            
            raw_adc = read_adc_value()
            raw_throttle = get_throttle_percentage_from_adc(raw_adc)
            
            throttle_buffer.append(raw_throttle)
            smoothed_throttle = sum(throttle_buffer) / len(throttle_buffer)
            
            behavior.update(dt, smoothed_throttle)
            
            log_entry = { "timestamp": datetime.datetime.fromtimestamp(current_time).isoformat(), "profile": sound_manager.sound_profile, "state": behavior.state,
                          "raw_adc": raw_adc, "raw_throttle": raw_throttle, "smoothed_throttle": smoothed_throttle }
            if isinstance(behavior, M4Behavior):
                log_entry["rpm"] = int(behavior.simulated_rpm)
            log_data.append(log_entry)
            
            if current_time - last_display_update >= DISPLAY_UPDATE_INTERVAL:
                update_display(behavior, smoothed_throttle, raw_throttle, raw_adc, sound_manager.sound_profile)
                last_display_update = current_time
            
            time.sleep(max(0, (1.0 / FPS) - (time.time() - current_time)))
            
    except Exception as e:
        print(f"\nFATAL ERROR in main loop: {e}")
        import traceback
        traceback.print_exc()
        
    finally:
        print("\nShutting down...")
        
        if log_data:
            try:
                with open(LOG_FILE_NAME, 'w', newline='') as csvfile:
                    fieldnames = list(log_data[0].keys())
                    writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                    writer.writeheader()
                    writer.writerows(log_data)
                print(f"Log saved to {LOG_FILE_NAME}")
            except Exception as e:
                print(f"Error saving log: {e}")
        
        if 'sound_manager' in locals():
            sound_manager.stop_all_sounds(fade_ms=200)
            
        pygame.quit()
            
        if RASPI_HW_AVAILABLE:
            GPIO.cleanup()
            
        if button_action == 'shutdown' and RASPI_HW_AVAILABLE:
            print("Issuing system shutdown command...")
            time.sleep(1)
            try:
                subprocess.run(['sudo', 'shutdown', '-h', 'now'], check=True)
            except Exception as e:
                print(f"Failed to run shutdown command: {e}")
            
        print("Cleanup complete. Exiting.")

if __name__ == '__main__':
    main()
