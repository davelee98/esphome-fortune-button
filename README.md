# Fortune Button

Press the button and the LED ring becomes a prize wheel. A blue pointer
clicks round a white ring, ticking the buzzer at every step, and slows to a
stop. After a moment of darkness and silence, the toy answers:

- **Yes:** green fills the ring, then a rising two-note chime.
- **No:** red blinks three times, then a sad trombone.

Hold the button for 1.5–8 seconds instead for an easter egg: a magenta
flicker and a dissonant melody.

It runs with no network at all: no Wi-Fi, no Home Assistant, no API. It
works anywhere you plug it in.

## Hardware

Stock parts from the [Apollo Automation ESK-1 ESPHome Starter
Kit](https://apolloautomation.com/products/esk-1-esphome-starter-kit), the
[official ESPHome starter kit](https://esphome.io/starter-kit/). No soldering
or extra parts.

| Part | Connection |
|---|---|
| ESPHome C6 board (ESP32-C6) | Controller, powered over USB-C |
| Button module | GPIO6 |
| LED/buzzer module: 10 WS2812 LEDs | GPIO14 |
| LED/buzzer module: 2.7 kHz buzzer (via a transistor) | GPIO18 |
| Accessory power switch (+3V3_CTRL, the buzzer's supply) | GPIO4 |

The temperature/humidity and PIR modules in the kit aren't used.

## Build and flash

[fortune-button.yaml](fortune-button.yaml) is the complete standalone device
example, including the starter kit's accessory power switch. Keep the
directory layout as it is:

```
esphome-fortune-button/
├── fortune-button.yaml
└── components/fortune_button/  # all animations, timing and sound
```

The example loads the local component with this block. The path resolves
relative to the device YAML:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [fortune_button]
```

With the ESPHome CLI, from the repo root:

```sh
esphome run fortune-button.yaml
```

**With Device Builder:** it only lists YAML files at the top of its config
directory. Copy this repo into the config directory as `fortune-button/`,
copy `fortune-button.yaml` up to the top level beside it, and in that copy
change the component path to `path: fortune-button/components`.

There's no OTA, because there's no network, so flash it over USB-C. It needs
ESP-IDF, as ESPHome doesn't support Arduino on the C6. This component needs
ESPHome 2026.8 or later; it has been compiled on 2026.8.0 and 2026.9.1.

On Windows, turn on long paths (`LongPathsEnabled=1`) before the first
compile, or the ESP-IDF toolchain fails with
`bits/c++config.h: No such file`.

### Load from GitHub

To use the component without copying its source into your config directory,
copy [fortune-button.yaml](fortune-button.yaml) and replace only its
`external_components:` block with:

```yaml
external_components:
  - source: github://davelee98/esphome-fortune-button@main
    components: [fortune_button]
```

This follows `main`. For repeatable builds, replace `main` with a release tag
when one is available. Both source forms use ESPHome's standard
[external component loading](https://esphome.io/components/external_components/).

## Behaviour

| Phase | Light | Sound | Length |
|---|---|---|---|
| Idle | Violet breathing from 0 to 100%, fades out after 60 s idle | — | — |
| Spin | White ring, blue pointer stepping round | One tick per step | 5.6 s |
| Suspense | Dark | Silent | 0.9 s |
| Yes | Green fills the ring, then holds | Rising chime | 3.5 s |
| No | Red, three blinks, then fades | Sad trombone | 5.9 s |
| Easter egg | Magenta flicker, then smooth fade to black | Dissonant melody | Lights: 6.1 s; sound: 6.75 s |

Each spin makes a random 50–60 steps (at least five laps of the ring). It
starts at 17–21 steps a second and slows steadily to 2 a second. Beeps are
half the gap between steps, up to an eighth note at the tempo. The tick pitch
is E7 at 2637 Hz, the nearest note to the buzzer's 2700 Hz resonance. A 150 ms
dark lead-in precedes the full 5.6 s spin, including its final 500 ms settle.

Short presses during the spin or suspense are ignored. Short presses during
a verdict or the easter egg start a fresh reading and cancel the old lights
and sound. A long hold interrupts any phase with the easter egg. A button
held while plugging in is ignored until released; the next fresh press works.

After the idle glow's 2 s fade finishes, the ring stays dark until the next
accepted press, including across millisecond-counter rollover.

The angry fade freezes the last flickered frame and fades it smoothly to
black. Its melody continues briefly into idle unless another accepted press
cuts it off. Deliberate fades retain ESPHome's smooth interpolation; the old
incidental 80 ms transitions are omitted.

The yes/no roll is made fresh on every press. It mixes the microsecond at
which it runs, so it depends on exactly when you pressed. This reduces
repeatable patterns across power-ups while no radio is running. Each roll
is logged, for example
`fortune: Roll 37: yes`.

## Settings

Pins and LED count stay in the YAML's `substitutions:`. The chance of yes is
`yes_percent` in the `fortune_button:` block: an integer from 0 to 100,
defaulting to 50. Zero always gives no; 100 always gives yes.

`brightness` in the same block dims every animation, as a percentage from
1% to 100%, defaulting to 100%. It scales Fortune's pixel colours before
ESPHome's gamma correction, the same way a light's own brightness does.

Other tuning values are constants in `components/fortune_button/fortune_button.h`.
Editing them requires rebuilding and flashing.

| Setting | Default | Effect |
|---|---|---|
| `IDLE_TIMEOUT_MS` | `60000` | Idle glow fades out after this many milliseconds |
| `TEMPO_BPM` | `120` | Tempo of the yes chime and no blinks, and the tick-length cap |
| `YES_FILL_MS` | `1000` | Time for green to fill the ring |
| `SPIN_TICKS` / `SPIN_TICK_SPREAD` | `55` / `5` | Each spin makes a random 50–60 steps |
| `SPIN_END_SPEED` | `2` | Steps per second when the wheel stops |
| `SPIN_MS` | `5600` | Length of the spin, including the settle |
| `SPIN_SETTLE_MS` | `500` | How long the pointer holds on its last step |
| `SPIN_WHITE_LEVEL` | `0.6` | Brightness of the white ring during the spin |
| `SPIN_TONE_HZ` | `2637` | Tick pitch: E7 |
| `SPIN_TONE_FRACTION` | `0.5` | Tick length as a fraction of the gap between steps |
| `SPIN_TONE_LEVEL` | `0.5` | Tick duty (0.5 is loudest) |
| `RING_CLOCKWISE` | `true` | Set to `false` to reverse the spin and fill |

The LED effects assume the 10 LEDs form a closed ring, with LED 9 next to
LED 0.

## The `fortune_button` component

A single external component owns the entire fortune sequence. YAML describes
the hardware and supplies three references. The implementation keeps timing,
rendering and sound in one class to minimize maintained code.

### Add to an existing device YAML

Use either `external_components:` block above, then merge these entries into
your device's existing `output:`, `light:` and `binary_sensor:` lists. This
example uses the starter kit's pins; adapt them to your wiring:

```yaml
output:
  - platform: ledc
    pin: GPIO18
    id: buzzer

light:
  - platform: esp32_rmt_led_strip
    id: led_ring
    internal: true
    pin: GPIO14
    num_leds: 10
    chipset: WS2812
    channel_colors: GRB
    rmt_symbols: 48

binary_sensor:
  - platform: gpio
    id: ask_button
    internal: true
    pin:
      number: GPIO6
      mode:
        input: true
        pullup: true
      inverted: true
    filters:
      - delayed_on: 20ms

fortune_button:
  light: led_ring
  output: buzzer
  button: ask_button
  yes_percent: 50
  brightness: 100%
```

`light` must be an addressable light, `output` must be LEDC, and `button` is
the filtered binary sensor. The LEDC requirement makes this component
ESP32-only. By default, keep the ring and buzzer exclusively under the
component's control: no other light effects, automations or sound players
should write to them. The ring can be shared through an explicit handoff as
described below; the buzzer remains exclusive. Keep the ring and button
internal. The button filter delays presses by 20 ms and lets releases
through immediately.

On the starter kit, also copy the `accessory_power` switch and
`esphome.on_shutdown` action from the complete example: GPIO4 must enable
the buzzer supply before the outputs start. Other boards may not need this
switch. Use the example's ESP32-C6/ESP-IDF configuration for the kit. If your
existing device uses `wifi:` and `api:`, set `reboot_timeout: 0s` in both so
a network disconnection cannot interrupt the toy.

### Share the light with another renderer

Set `share_light: true` to draw only during a reading or the easter egg.
Fortune then leaves the light's state, brightness and effect unchanged and
draws nothing at boot or while idle. The default is `false`, which retains
the standalone idle glow and fade-out.

The other owner must keep the light **on at 100% brightness with its
addressable effect running**. It must pause only its pixel writes while
Fortune is active. Stopping the effect lets the light overwrite Fortune's
pixels. Fortune forces a complete first frame on every takeover so colours
from the other renderer cannot remain behind.

Use `on_start` and `on_finish` to coordinate that handoff. This fragment
assumes the hardware IDs already exist and illustrates another renderer
with ID `display_owner` and a synchronous `set_suspended(bool)` method;
adapt those calls to your renderer's actual API:

```yaml
fortune_button:
  light: led_ring
  output: buzzer
  button: ask_button
  share_light: true
  on_start:
    - lambda: id(display_owner).set_suspended(true);
  on_finish:
    - lambda: id(display_owner).set_suspended(false);
```

On resume, the other owner must redraw its complete current frame, even if
its status has not changed. Its inputs and timeouts can keep updating while
its pixel writes are suspended.

Both triggers also work in standalone mode:

- `on_start` fires when an idle Fortune becomes active, before its first
  pixel write.
- `on_finish` fires when the active sequence returns to idle. This releases
  the LEDs; the angry melody can continue briefly. It is not a shutdown or
  reboot notification.
- Restarts between active phases fire neither trigger: a short press during
  a verdict or the easter egg, or a long hold during the spin, keeps the same
  handoff. Ignored presses and the idle timeout also fire neither.

Each trigger accepts one automation containing multiple actions. Suspend
or resume the other renderer synchronously, before any `delay`, `wait_until`
or asynchronous script work. Fortune continues as soon as the trigger call
returns; it does not wait for deferred actions.

Fortune's pixel levels come from its own `brightness` option. Brightness
encoded only into the other renderer's frames does not affect them, but the
underlying light's brightness still scales Fortune's output. It must remain
at 100%.

The other owner should be ready before a press. An early startup press can
produce overwritten or black pixels, and the stale colour cache can keep
them wrong until those pixels next change. Shared mode adds no startup
readiness or recovery logic.

### Timing and validation

The state machine uses elapsed time without blocking. All animations use a
16 ms render cadence; flicker values change no more often than every 40 ms.
The sad trombone is the existing pitch/loudness contour: 270 steps of 12 ms,
3.24 s in `contour_data.h`. A fast-loop request is used only while replaying
that contour; LED rendering remains throttled. Melodies use fixed note
tables, with no RTTTL parser. Shutdown silences the buzzer and stops the
fast-loop request.

After changing the component, validate and compile the complete example:

```sh
esphome config fortune-button.yaml
esphome compile fortune-button.yaml
```

Then verify on hardware: normal and long presses, restart during verdicts,
ignored short presses during the spin, a held button at boot, idle timeout,
fades and sound. The 0/100 odds settings can force each verdict for these
checks.

### Regenerating the trombone

`contour_data.h` is generated from a recording of a sad trombone. You only
need to regenerate it to change the sound, for example to transpose it or
trim it. It needs Python with NumPy and SciPy, plus ffmpeg.

The recording isn't in the repo, because it isn't ours to redistribute.
Supply your own as `audio/reference.mp3`; `.gitignore` keeps it out of
commits. Only the pitch and loudness contour measured from it is committed.
Then, from `audio/`:

```sh
ffmpeg -i reference.mp3 -ac 1 -ar 22050 ref_trombone.wav
python analyse_ref.py       # pitch and loudness track -> ref_contour.npz
python build_from_ref.py    # -> ../components/fortune_button/contour_data.h
```

The knobs are at the top of `build_from_ref.py`: the part of the recording
to use (`T_START`, `T_END`), where the final note lands (`TARGET_TAIL_HZ`)
and the step length (`STEP_MS`). The time windows are tuned to the original
recording, so a different one needs them adjusted, as does the final-note
window (`t > 2.5` to `t < 3.3`) in `build()`.

## License

[GNU General Public License v3.0](LICENSE). You may use, modify and
redistribute this code, but distributed versions, including modified ones,
must stay under GPL-3.0 and include their source.
