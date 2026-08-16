# Lamborghini SVJ — Rider's Manual

A short user guide for the SVJ car mode. The other three cars (M4, Supra, Hellcat) have their own automatic behaviors; this one is different — you drive it. Six manual gears, paddle shifts, launch control, and a free-rev neutral mode.

## At a glance

| Button (or arrow key) | Riding (moving) | Standing still (IDLE) |
|---|---|---|
| **Right turn signal** (`→`) | Upshift | Enter NEUTRAL rev mode |
| **Left turn signal** (`←`) | Downshift (only while decelerating) | Enter LAUNCH CONTROL |

The same two buttons do different things depending on whether you're moving or stopped. There's no ambiguity — you're either pulling away from a light (shifts) or sitting at one (modes), and the SVJ knows which.

To switch into the SVJ car, short-press the main mode button (GPIO 17) until the on-screen car name reads `SVJ`. Hold that same button for 2 seconds to shut the whole simulator down.

## Riding

### Pulling away

1. Sit at idle.
2. Twist the throttle. You'll roll out of IDLE into ACCELERATING and hear the **gear 1** clip start playing.
3. **Right turn signal** = upshift. The current gear clip is muted instantly, you may hear a sharp exhaust crack, there's a 75 ms ignition cut, then the next gear's clip slams in at full volume. (Pop chance is 75% — a quarter of upshifts are clean and silent in the gap.)

You can upshift any time the engine is pulling — from ACCELERATING **or** from REDLINE. Short-shift early at a third throttle, or wait until the engine is banging off the limiter; both work.

### Hearing the limiter

Each gear's accel clip has a known "redline time." If you stay in that gear long enough, the engine automatically transitions into REDLINE state and `Redline.wav` starts looping. From there, you can keep waiting and the limiter keeps slamming until you upshift (or let off the throttle).

Sixth gear is special — there's no redline. When the 6th gear clip finishes, the car drops into CRUISING and the cruise loop plays.

### Slowing down

Let off the throttle and the SVJ drops into DECELERATING and plays the long decel clip. Things that can happen here:

- **Left turn signal** (downshift): drops you one gear, plays a random downshift overlay sound (rev-match blip), and restarts the decel clip from zero so the audio baseline matches the new RPM. Won't fire if you're already in 1st.
- **Backfire roll**: there's a 25% chance of a small backfire firing 250 ms into the decel clip the moment you enter DECELERATING from above. Pure randomness — that's what gives the SVJ its life.
- **Downshift into 1st or 2nd**: 65% chance of a louder backfire firing 150 ms after the downshift overlay. Very dramatic, very Lambo.
- **Throttle re-tap**: hit the throttle again any time during deceleration and the deceleration clip **fades out** while the current gear’s accel clip **crossfades in** over about **280 ms** (see `SVJ_DECEL_TO_ACCEL_CROSSFADE_MS` in `config.h`). Much smoother than an instant cut.
- **Coast to a stop**: when the decel clip finishes naturally, you drop back to IDLE.

## Standing still — the two modes

When the engine is sitting at IDLE, the buttons completely change their role.

### NEUTRAL rev mode (right tap)

Want to pull up next to someone and just rev the V12? Tap the **right** turn signal once.

- You hear a **guaranteed** sharp exhaust crack on entry (one of the four pops).
- The state goes to NEUTRAL. The throttle is now disconnected from the gear timers — twisting it just plays rev sounds without "driving anywhere."
- Twist a little (under 35%) → `LowRev1.wav` or `LowRev2.wav` loops at random.
- Twist more (35–70%) → `MediumRev1/2.wav`.
- Pin it (70–95%) → `HighRev1/2.wav`.
- Hold it on the stop (over 95%) for half a second → `Redline.wav` kicks in and the engine bangs off the limiter for as long as you hold it.
- Crossfades between zones are about 150 ms so it never sounds choppy.

When you're done, **tap the right turn signal again**. You'll hear a heavy mechanical "clunk" (placeholder is `Downshift3.wav`), the gear resets to 1, and you're back in IDLE ready to ride.

The left turn signal does nothing while you're in NEUTRAL.

### LAUNCH CONTROL (left tap)

Track-day mode. Tap the **left** turn signal while sitting at IDLE.

- The launch loop (`LaunchControl.wav`) starts playing — the SVJ's launch limiter bouncing around 4,000 RPM.
- Pin the throttle to 100% if you want; it has no effect here. The loop just keeps building tension.
- **Tap the right** turn signal when you're ready to go. The launch loop fades out over 1.5 seconds (tires grabbing traction), then gear 1's accel clip slams in at full volume and you're off.
- **Tap the left** turn signal to cancel — you drop back to IDLE with no launch.

## Sounds you'll hear and when

| Sound | When it plays |
|---|---|
| `Startup.wav` | Once, when you switch into the SVJ car for the first time |
| `Idle.wav` | Looping while at IDLE |
| `1stG.wav` – `6thG.wav` | The active gear's accel clip while in ACCELERATING |
| `Redline.wav` | Looping while in REDLINE state, or in NEUTRAL with throttle > 95% sustained |
| `CruisingUnlooped.wav` | Looping while in CRUISING (6th gear after its clip ended) |
| `Deaccerleration1.wav` | While in DECELERATING |
| `Pop1.wav` – `Pop4.wav` | Random pick on **upshift** (75% chance any one fires, 25% silent), and guaranteed on **NEUTRAL entry** — slightly boosted vs other overlays (`SVJ_POP_VOLUME_GAIN`) |
| `Downshift1/2/3.wav` | Random pick on every downshift (always plays); same **cosine tail-out** as backfire so the blip doesn’t snap off (`SVJ_DOWNSHIFT_OVERLAY_TAIL_FADE_MS`) |
| `Backfire.wav` | 25% chance 250 ms into decel after accel→decel; 65% chance 150 ms after downshifting to 1st or 2nd. Tail uses a **long cosine volume ramp** (see `SVJ_BACKFIRE_TAIL_FADE_MS`, plus `SVJ_ONE_SHOT_TAIL_END_SAFETY`) so gain eases out before the sample stops. |
| `LaunchControl.wav` | Looping while in LAUNCH_HOLD state |
| `LowRev / MediumRev / HighRev` | Looping in NEUTRAL based on throttle position |

## Part-throttle volume

The SVJ doesn't sound like a wide-open drag race when you're just cruising at 30%. The accel-clip channel is volume-scaled by a 1.8-power curve of the throttle, with a floor of 15%. So 30% throttle sounds noticeably quieter than 100%. The other channels (cruise, decel, launch, etc.) stay at their normal volumes — only the active gear's accel clip is throttle-scaled.

## Desktop testing

Without the Pi hardware, all controls map to the keyboard:

| Key | Action |
|---|---|
| `W` / `↑` | Throttle up (+5%) |
| `S` / `↓` | Throttle down (-5%) |
| `A` | Throttle up fine (+1%) |
| `D` | Throttle down fine (-1%) |
| `1` | Snap to 100% |
| `0` | Snap to 0% |
| `←` | Left turn signal (SVJ: downshift / launch / cancel) |
| `→` | Right turn signal (SVJ: upshift / neutral / launch fire) |
| `Space` | Cycle to the next car (M4 → Supra → Hellcat → SVJ → M4 …) |
| `Esc` | Quit |

The status line at the bottom shows `Gear`, `ClipPos` (seconds into the current gear clip), `RevZone` (1=low, 2=med, 3=high, 4=on the limiter), `LaunchT`, and the current state.

## On the scooter — turn-signal wiring

The two turn-signal switches on the right-grip pod each have their own GPIO pin and share a common ground:

- Common wire → any GND pin on the Pi (e.g. physical pin 6).
- Left switch → GPIO 23 (BCM) → physical pin 16.
- Right switch → GPIO 24 (BCM) → physical pin 18.

The Pi enables its internal pull-up on both lines, so the line reads HIGH when the switch is open and LOW when pressed. Falling-edge interrupts fire the event, with a 100 ms software debounce.

If you choose different pins, change `SVJ_LEFT_SIGNAL_GPIO_PIN` and `SVJ_RIGHT_SIGNAL_GPIO_PIN` in [include/config.h](../include/config.h) and rebuild.

## Troubleshooting

**A sound didn't load.** On startup you'll see a `[svj] Loaded N/M sounds` line. If `N < M`, one or more `.wav` files in `svj/` are missing or unreadable. Check the warnings just above that line for the specific filename.

**The audio sounds stale or wrong even after editing a `.wav`.** The loader caches all SVJ sounds in `svj/sound_cache.bin`. The cache auto-invalidates when any source `.wav` is newer, but if you swap a file with an older timestamp the cache may not pick up the change. To force a regenerate, just delete `svj/sound_cache.bin` and rerun.

**Shifts feel laggy.** They shouldn't — the design fires every shift on press-down (0 ms latency). If you see lag, check the build is Release (`build/Release/ScooterV2.exe`) and the CPU isn't throttled. The 75 ms "ignition cut" gap on upshifts is intentional and is the dual-clutch silence between gear clips, not lag.

**The redline loop has a clicky seam.** Known. `Redline.wav` is not a perfectly loopable clip; the design accepts the seam because redline visits are usually short. If you want it cleaner, source a longer redline clip and replace the file (don't forget to delete the cache).

**The cruise loop has a small click.** Same root cause — `CruisingUnlooped.wav` was sourced as a one-shot and we loop it anyway. Replace with a seamlessly loopable cruise clip for a perfect fix.
