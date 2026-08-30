"""Standalone keyboard/audio simulator for the Lamborghini SVJ sound set.

Install pygame if needed:
    py -m pip install pygame

Run this file from anywhere; audio is loaded from the adjacent ``svj`` folder:
    py svj_keyboard_sim.py

The most useful tuning values are grouped in ``Tuning`` below. Command-line
flags are also available; run with ``--help`` for details.
"""

from __future__ import annotations

import argparse
import math
import random
import sys
import time
from dataclasses import dataclass
from pathlib import Path

try:
    import pygame
except ImportError:
    raise SystemExit("pygame is required. Install it with: py -m pip install pygame")


@dataclass
class Tuning:
    # Requested fast behavior: launch is effectively immediate, shifts overlap,
    # and the limiter is heard briefly before an automatic upshift.
    launch_release_delay_s: float = 0.05
    shift_crossfade_s: float = 0.06
    redline_crossfade_s: float = 0.08
    redline_hold_s: float = 0.30
    auto_upshift_after_redline: bool = True

    # Time spent accelerating before entering the limiter in gears 1-5.
    redline_at_s: tuple[float, ...] = (3.2, 5.0, 7.2, 9.5, 12.0)
    redline_clip_end_lead_s: float = 0.20

    fps: int = 60
    master_volume: float = 0.99
    idle_volume: float = 0.70
    gear_volume: float = 1.00
    redline_volume: float = 1.00
    cruise_volume: float = 0.85
    decel_volume: float = 0.90
    launch_volume: float = 1.00
    overlay_volume: float = 1.00
    throttle_deadzone: float = 0.05
    partial_throttle_floor: float = 0.15
    partial_throttle_power: float = 1.8
    neutral_redline_delay_s: float = 0.50
    pop_probability: float = 0.75
    throttle_ramp_per_s: float = 0.65
    throttle_fine_ramp_per_s: float = 0.15
    mouse_wheel_throttle_step: float = 0.05


FILES = {
    "startup": "Startup.wav",
    "idle": "Idle.wav",
    "redline": "Redline.wav",
    "cruise": "CruisingUnlooped.wav",
    "decel": "Deaccerleration1.wav",  # Filename is intentionally misspelled.
    "launch": "LaunchControl.wav",
    "backfire": "Backfire.wav",
}
FILES.update({f"gear{i}": f"{name}G.wav" for i, name in enumerate(
    ("1st", "2nd", "3rd", "4th", "5th", "6th"), start=1
)})
FILES.update({f"pop{i}": f"Pop{i}.wav" for i in range(1, 5)})
FILES.update({f"downshift{i}": f"Downshift{i}.wav" for i in range(1, 4)})
for prefix, filename in (("low", "LowRev"), ("medium", "MediumRev"), ("high", "HighRev")):
    FILES.update({f"{prefix}{i}": f"{filename}{i}.wav" for i in range(1, 3)})


class Crossfade:
    def __init__(self) -> None:
        self.active = False
        self.started = 0.0
        self.duration = 0.01
        self.outgoing: pygame.mixer.Channel | None = None
        self.incoming: pygame.mixer.Channel | None = None
        self.out_volume = 1.0
        self.in_volume = 1.0

    def begin(
        self,
        outgoing: pygame.mixer.Channel | None,
        incoming: pygame.mixer.Channel,
        duration: float,
        out_volume: float,
        in_volume: float,
    ) -> None:
        self.outgoing = outgoing
        self.incoming = incoming
        self.duration = max(0.001, duration)
        self.out_volume = out_volume
        self.in_volume = in_volume
        self.started = time.monotonic()
        self.active = True
        incoming.set_volume(0.0)

    def update(self) -> None:
        if not self.active or self.incoming is None:
            return
        progress = min(1.0, (time.monotonic() - self.started) / self.duration)
        if self.outgoing is not None:
            self.outgoing.set_volume(self.out_volume * math.cos(progress * math.pi / 2))
        self.incoming.set_volume(self.in_volume * math.sin(progress * math.pi / 2))
        if progress >= 1.0:
            if self.outgoing is not None:
                self.outgoing.stop()
            self.incoming.set_volume(self.in_volume)
            self.active = False


class SvjSimulator:
    IDLE = "IDLE"
    ACCEL = "ACCELERATING"
    REDLINE = "REDLINE"
    CRUISE = "CRUISING"
    DECEL = "DECELERATING"
    NEUTRAL = "NEUTRAL"
    LAUNCH = "LAUNCH HOLD"
    LAUNCHING = "LAUNCHING"

    def __init__(self, sound_dir: Path, tuning: Tuning) -> None:
        self.t = tuning
        self.sounds: dict[str, pygame.mixer.Sound] = {}
        missing: list[str] = []
        for key, filename in FILES.items():
            path = sound_dir / filename
            if path.is_file():
                self.sounds[key] = pygame.mixer.Sound(str(path))
            else:
                missing.append(filename)
        if missing:
            print("Warning: missing audio files (related actions will be silent):")
            print("  " + "\n  ".join(missing))

        self.idle_ch = pygame.mixer.Channel(0)
        self.gear_channels = (pygame.mixer.Channel(1), pygame.mixer.Channel(2))
        self.redline_ch = pygame.mixer.Channel(3)
        self.base_ch = pygame.mixer.Channel(4)
        self.overlay_ch = pygame.mixer.Channel(5)
        self.rev_channels = (pygame.mixer.Channel(6), pygame.mixer.Channel(7))
        self.active_gear_channel = 0
        self.active_rev_channel = 0
        self.crossfade = Crossfade()
        self.rev_crossfade = Crossfade()
        self.state = self.IDLE
        self.gear = 1
        self.throttle = 0.0
        self.gear_started = time.monotonic()
        self.redline_started = 0.0
        self.launch_started = 0.0
        self.neutral_full_throttle_started: float | None = None
        self.rev_zone = 0
        self.running = True
        self._play_idle()

    def _sound(self, key: str) -> pygame.mixer.Sound | None:
        return self.sounds.get(key)

    def _play(self, channel: pygame.mixer.Channel, key: str, volume: float, loops: int = 0) -> bool:
        sound = self._sound(key)
        if sound is None:
            return False
        channel.set_volume(max(0.0, min(1.0, volume * self.t.master_volume)))
        channel.play(sound, loops=loops)
        return True

    def _play_idle(self) -> None:
        self.base_ch.stop()
        self.redline_ch.stop()
        for channel in self.gear_channels + self.rev_channels:
            channel.stop()
        self._play(self.idle_ch, "idle", self.t.idle_volume, loops=-1)
        self.state = self.IDLE
        self.gear = 1

    def _gear_level(self) -> float:
        shaped = self.throttle ** self.t.partial_throttle_power
        return self.t.gear_volume * max(self.t.partial_throttle_floor, shaped)

    def _start_gear(self, gear: int, crossfade_s: float | None = None) -> None:
        gear = max(1, min(6, gear))
        outgoing = self.gear_channels[self.active_gear_channel]
        incoming_index = 1 - self.active_gear_channel
        incoming = self.gear_channels[incoming_index]
        incoming.stop()
        sound = self._sound(f"gear{gear}")
        if sound is not None:
            incoming.play(sound)
        level = self._gear_level() * self.t.master_volume
        duration = self.t.shift_crossfade_s if crossfade_s is None else crossfade_s
        if outgoing.get_busy() or self.redline_ch.get_busy():
            old = self.redline_ch if self.redline_ch.get_busy() else outgoing
            self.crossfade.begin(old, incoming, duration, old.get_volume(), level)
        else:
            incoming.set_volume(level)
        self.idle_ch.fadeout(max(1, int(duration * 1000)))
        self.base_ch.fadeout(max(1, int(duration * 1000)))
        self.active_gear_channel = incoming_index
        self.gear = gear
        self.gear_started = time.monotonic()
        self.state = self.ACCEL

    def _play_pop(self) -> None:
        if random.random() <= self.t.pop_probability:
            self._play(self.overlay_ch, f"pop{random.randint(1, 4)}", self.t.overlay_volume)

    def upshift(self, automatic: bool = False) -> None:
        if self.state not in (self.ACCEL, self.REDLINE) or self.gear >= 6:
            return
        self._play_pop()
        self._start_gear(self.gear + 1)
        print(f"{'Auto' if automatic else 'Manual'} upshift -> gear {self.gear}")

    def downshift(self) -> None:
        if self.state not in (self.DECEL, self.CRUISE) or self.gear <= 1:
            return
        self.gear -= 1
        self._play(self.overlay_ch, f"downshift{random.randint(1, 3)}", self.t.overlay_volume)
        if self.state == self.CRUISE:
            self._start_gear(self.gear)
        else:
            self._play(self.base_ch, "decel", self.t.decel_volume)

    def enter_redline(self) -> None:
        if self.state != self.ACCEL or self.gear >= 6:
            return
        sound = self._sound("redline")
        if sound is not None:
            self.redline_ch.play(sound, loops=-1)
        outgoing = self.gear_channels[self.active_gear_channel]
        self.crossfade.begin(
            outgoing,
            self.redline_ch,
            self.t.redline_crossfade_s,
            outgoing.get_volume(),
            self.t.redline_volume * self.t.master_volume,
        )
        self.redline_started = time.monotonic()
        self.state = self.REDLINE
        print(f"Rev limiter: gear {self.gear} ({self.t.redline_hold_s:.2f}s max)")

    def set_throttle(self, value: float) -> None:
        self.throttle = max(0.0, min(1.0, value))

    def left(self) -> None:
        if self.state == self.IDLE:
            self.idle_ch.fadeout(max(1, int(self.t.shift_crossfade_s * 1000)))
            self._play(self.base_ch, "launch", self.t.launch_volume, loops=-1)
            self.state = self.LAUNCH
        elif self.state == self.LAUNCH:
            self._play_idle()
        else:
            self.downshift()

    def right(self) -> None:
        if self.state == self.IDLE:
            self.idle_ch.fadeout(max(1, int(self.t.shift_crossfade_s * 1000)))
            self._play_pop()
            self.state = self.NEUTRAL
        elif self.state == self.NEUTRAL:
            self._play(self.overlay_ch, "downshift3", self.t.overlay_volume)
            self._play_idle()
        elif self.state == self.LAUNCH:
            self.launch_started = time.monotonic()
            self.state = self.LAUNCHING
        else:
            self.upshift()

    def _update_neutral(self) -> None:
        now = time.monotonic()
        if self.throttle <= self.t.throttle_deadzone:
            zone = 0
        elif self.throttle <= 0.35:
            zone = 1
        elif self.throttle <= 0.70:
            zone = 2
        elif self.throttle <= 0.95:
            zone = 3
        else:
            zone = 4

        if zone == 4:
            if self.neutral_full_throttle_started is None:
                self.neutral_full_throttle_started = now
            if now - self.neutral_full_throttle_started >= self.t.neutral_redline_delay_s:
                if self.rev_zone != 4:
                    for channel in self.rev_channels:
                        channel.stop()
                    self._play(self.redline_ch, "redline", self.t.redline_volume, loops=-1)
                self.rev_zone = 4
                return
            zone = 3
        else:
            self.neutral_full_throttle_started = None
            self.redline_ch.stop()

        if zone == 0:
            for channel in self.rev_channels:
                channel.stop()
            self.rev_zone = 0
        elif zone != self.rev_zone:
            names = {1: "low", 2: "medium", 3: "high"}
            incoming_index = 1 - self.active_rev_channel
            incoming = self.rev_channels[incoming_index]
            outgoing = self.rev_channels[self.active_rev_channel]
            self._play(incoming, f"{names[zone]}{random.randint(1, 2)}", 0.0, loops=-1)
            self.rev_crossfade.begin(
                outgoing if outgoing.get_busy() else None,
                incoming,
                0.10,
                outgoing.get_volume(),
                self.t.master_volume,
            )
            self.active_rev_channel = incoming_index
            self.rev_zone = zone

    def update(self) -> None:
        now = time.monotonic()
        self.crossfade.update()
        self.rev_crossfade.update()

        if self.state == self.IDLE:
            if self.throttle > self.t.throttle_deadzone:
                self._start_gear(1, self.t.shift_crossfade_s)
        elif self.state == self.ACCEL:
            self.gear_channels[self.active_gear_channel].set_volume(
                self._gear_level() * self.t.master_volume
            )
            if self.throttle <= self.t.throttle_deadzone:
                self._enter_decel()
            elif self.gear < 6:
                elapsed = now - self.gear_started
                sound = self._sound(f"gear{self.gear}")
                clip_trigger = math.inf
                if sound is not None:
                    clip_trigger = max(0.0, sound.get_length() - self.t.redline_clip_end_lead_s)
                if elapsed >= min(self.t.redline_at_s[self.gear - 1], clip_trigger):
                    self.enter_redline()
            elif not self.gear_channels[self.active_gear_channel].get_busy():
                self._play(self.base_ch, "cruise", self.t.cruise_volume, loops=-1)
                self.state = self.CRUISE
        elif self.state == self.REDLINE:
            if self.throttle <= self.t.throttle_deadzone:
                self._enter_decel()
            elif (
                self.t.auto_upshift_after_redline
                and now - self.redline_started >= self.t.redline_hold_s
            ):
                self.upshift(automatic=True)
        elif self.state == self.CRUISE:
            if self.throttle <= self.t.throttle_deadzone:
                self._enter_decel()
        elif self.state == self.DECEL:
            if self.throttle > self.t.throttle_deadzone:
                self._start_gear(self.gear)
            elif not self.base_ch.get_busy():
                self._play_idle()
        elif self.state == self.NEUTRAL:
            self._update_neutral()
        elif self.state == self.LAUNCHING:
            if now - self.launch_started >= self.t.launch_release_delay_s:
                self.base_ch.fadeout(max(1, int(self.t.shift_crossfade_s * 1000)))
                self._start_gear(1, self.t.shift_crossfade_s)

    def _enter_decel(self) -> None:
        self.crossfade.active = False
        self.redline_ch.fadeout(max(1, int(self.t.shift_crossfade_s * 1000)))
        self.gear_channels[self.active_gear_channel].fadeout(
            max(1, int(self.t.shift_crossfade_s * 1000))
        )
        self._play(self.base_ch, "decel", self.t.decel_volume)
        self.state = self.DECEL


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Standalone SVJ keyboard audio simulator")
    parser.add_argument("--sounds", type=Path, default=Path(__file__).parent / "svj")
    parser.add_argument("--launch-delay", type=float, default=0.05)
    parser.add_argument("--limiter-time", type=float, default=0.30)
    parser.add_argument("--shift-crossfade", type=float, default=0.06)
    parser.add_argument("--no-auto-upshift", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    tuning = Tuning(
        launch_release_delay_s=max(0.0, args.launch_delay),
        redline_hold_s=max(0.0, args.limiter_time),
        shift_crossfade_s=max(0.001, args.shift_crossfade),
        auto_upshift_after_redline=not args.no_auto_upshift,
    )
    pygame.mixer.pre_init(44100, -16, 2, 512)
    pygame.init()
    pygame.mixer.set_num_channels(8)
    screen = pygame.display.set_mode((780, 310))
    pygame.display.set_caption("SVJ Keyboard Simulator")
    font = pygame.font.SysFont("consolas", 23)
    small = pygame.font.SysFont("consolas", 17)
    sim = SvjSimulator(args.sounds.resolve(), tuning)
    clock = pygame.time.Clock()

    controls = (
        "Hold W/Up = throttle   Hold S/Down = release   Wheel = +/-5%",
        "Hold A/D = fine throttle +/-   1=100%   0=0%",
        "Left: launch/cancel/downshift   Right: fire/neutral/upshift",
        "R: reset to idle   Esc: quit",
    )
    while sim.running:
        dt = clock.tick(tuning.fps) / 1000.0
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                sim.running = False
            elif event.type == pygame.MOUSEWHEEL:
                sim.set_throttle(
                    sim.throttle + event.y * tuning.mouse_wheel_throttle_step
                )
            elif event.type == pygame.KEYDOWN:
                key = event.key
                if key == pygame.K_ESCAPE:
                    sim.running = False
                elif key == pygame.K_1:
                    sim.set_throttle(1.0)
                elif key == pygame.K_0:
                    sim.set_throttle(0.0)
                elif key == pygame.K_LEFT:
                    sim.left()
                elif key == pygame.K_RIGHT:
                    sim.right()
                elif key == pygame.K_r:
                    sim._play_idle()

        keys = pygame.key.get_pressed()
        throttle_direction = int(keys[pygame.K_w] or keys[pygame.K_UP])
        throttle_direction -= int(keys[pygame.K_s] or keys[pygame.K_DOWN])
        fine_direction = int(keys[pygame.K_a]) - int(keys[pygame.K_d])
        sim.set_throttle(
            sim.throttle
            + throttle_direction * tuning.throttle_ramp_per_s * dt
            + fine_direction * tuning.throttle_fine_ramp_per_s * dt
        )

        sim.update()
        screen.fill((15, 18, 22))
        status = f"SVJ  |  {sim.state}  |  Gear {sim.gear}  |  Throttle {sim.throttle:5.0%}"
        screen.blit(font.render(status, True, (241, 180, 52)), (24, 30))
        screen.blit(
            small.render(
                f"Launch delay {tuning.launch_release_delay_s:.2f}s  |  "
                f"Limiter {tuning.redline_hold_s:.2f}s  |  "
                f"Shift blend {tuning.shift_crossfade_s:.2f}s",
                True,
                (175, 185, 195),
            ),
            (24, 82),
        )
        for index, line in enumerate(controls):
            screen.blit(small.render(line, True, (215, 220, 225)), (24, 135 + index * 31))
        pygame.display.flip()

    pygame.mixer.fadeout(100)
    pygame.quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
