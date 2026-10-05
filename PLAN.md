# Plan: move the fortune logic into a `fortune_button` external component

## Context

Today `fortune-button.yaml` holds nearly all the behaviour: about 400 lines of
scripts, `addressable_lambda` effects, a global and about 20 tuning
substitutions. Only the trombone lives in C++ (`components/pwm_speech/`). The
goal is one C++ component that plugs into an ESP32 top-level YAML with a
short block and owns the LED ring and buzzer for all behaviour.

Guidelines from the user:
- A C++ external component, with `pwm_speech` folded into it.
- **Minimal YAML configuration**: hardware references plus the optional
  `yes_percent`. All other tuning values become constants in the header.
- **Minimal complexity**: preserve today's behaviour with one state machine,
  callback-only button handling and one render cadence. No new features,
  hooks, runtime parsing or general-purpose animation framework. The requested
  E7 tick pitch, clean interruption handling and omission of incidental 80 ms
  transitions are the deliberate changes.
- **Do not test-compile.** The code targets ESPHome 2026.8, and the user
  compiles it.

## Component boundary

| Keep in YAML | Move into C++ |
|---|---|
| Board, framework and logging | Startup, idle breathing and timeout |
| Pins, LED chipset/count and color order | Spin animation and tick timing |
| Button input, inversion and debounce | Short/long press interpretation |
| LEDC output and accessory power | Verdict selection, lights and sounds |
| Component references and `yes_percent` | Cancellation and fades |

Reuse ESPHome's hardware drivers and filtered binary sensor. Do not recreate
GPIO debounce, LED transmission or PWM generation inside the component.
The light and buzzer are exclusively controlled by `fortune_button`; they
must not have other effects, automations or sound players writing to them.

## Layout

```
fortune-button.yaml                 # hardware + fortune_button: block
components/fortune_button/
  __init__.py                       # 3 required references + optional odds, codegen
  fortune_button.h                  # tuning constants, class
  fortune_button.cpp                # state machine, pixels, sound
  contour_data.h                    # moved from pwm_speech, namespace fortune_button
components/pwm_speech/              # deleted
audio/build_from_ref.py             # emit path + namespace updated
README.md                           # updated
```

## YAML after the change

```yaml
fortune_button:
  light: led_ring      # addressable light
  output: buzzer       # LEDC output with adjustable frequency
  button: ask_button   # binary_sensor
  yes_percent: 50      # chance of yes, 0-100
```

`light`, `output` and `button` are required. `yes_percent` is optional, a whole
number from 0 to 100 with a default of 50. That's the whole config.

What stays in the top level:
- `esphome:`, with the name plus the existing
  `on_shutdown: switch.turn_off: accessory_power`; `on_boot` goes
- `esp32:`, `logger:` and `external_components:`
- the pin substitutions
- the `switch` gpio `accessory_power`
- the `output` ledc `buzzer` (renamed from `buzzer_out`)
- the `light` `led_ring` (renamed from `pixels`), with **no effects** and no `default_transition_length`
- the `binary_sensor` `ask_button`, with `delayed_on: 20ms` kept and
  `on_click` removed

Removed: `rtttl:`, `pwm_speech:`, `globals:`, every `script:` and every tuning
substitution.

Keep the ring and button internal, as they are today. Accessory power and
its shutdown automation remain hardware configuration in YAML.

## `__init__.py`

`DEPENDENCIES = ["esp32", "light", "output", "binary_sensor"]`. Import
`LEDCOutput` from `esphome.components.ledc.output`. The schema has:
- generated component ID and `cv.COMPONENT_SCHEMA`
- `light`: `cv.use_id(light.AddressableLightState)`
- `output`: `cv.use_id(LEDCOutput)`
- `button`: `cv.use_id(binary_sensor.BinarySensor)`
- `yes_percent`: `cv.Optional(..., default=50)`, `cv.int_range(min=0, max=100)`

`to_code` registers the component and calls four setters. There are no
actions and no triggers.

Restricting the output reference to LEDC rejects incompatible outputs during
validation. A generic `FloatOutput` may silently ignore `update_frequency()`.
LEDC and the `esp32` dependency intentionally make this component ESP32-only.
ESP8266 and LibreTiny PWM outputs can also change frequency, but supporting
them is outside this project's scope.

## `fortune_button.h`: constants

Constants carry today's values and their YAML comments, except for the
requested change of tick pitch to E7:
- `RING_CLOCKWISE = true`
- `IDLE_TIMEOUT_MS = 60000`
- `TEMPO_BPM = 120`
- `YES_FILL_MS = 1000`
- `SPIN_LEAD_IN_MS = 150`: darkness before the full spin starts
- the spin set: ticks 55 ± 5, end speed 2, 5600 ms with a 500 ms settle,
  white 0.6, tone 2637 Hz (E7, the nearest note to the 2700 Hz buzzer resonance),
  fraction 0.5, level 0.5
- `MELODY_LEVEL = 0.6f`: today's rtttl `gain: 60%`, which ESPHome passes
  straight to `set_level()`
- `TROMBONE_LEVEL = 0.5f`

The odds come from YAML instead: the member `yes_percent_` (a `uint8_t`) gives
yes when `roll < yes_percent_`, with `roll` 0–99. That's the same integer
comparison as today's `yes_max`.

Three `static_assert`s guard the spin maths against bad edits:
- `SPIN_TICK_SPREAD < SPIN_TICKS`
- `SPIN_SETTLE_MS < SPIN_MS`
- `TEMPO_BPM > 0`

Melodies are two small fixed tables of
`struct Note { uint16_t hz; uint32_t ms; }`, where `hz == 0` is a rest,
transcribed from today's RTTTL strings:
- **yes**: 523 Hz for `30000/TEMPO_BPM` ms, then 698 Hz for
  `90000/TEMPO_BPM` ms
- **angry** (b=100): 523/300, 554/300, 523/300, 554/300, 440/600, 0/150,
  523/300, 554/300, 392/600, 0/1200, 440/2400

No two consecutive notes share a pitch, so rtttl's 10 ms repeated-note gap
isn't needed.

Read ESPHome's [tagged 2026.8.0 light source on GitHub](https://github.com/esphome/esphome/tree/2026.8.0/esphome/components/light)
when porting pulse, addressable flicker and transitions; ESPHome is not installed locally
and no test-compilation is requested. Preserve brightness ranges, curves and
flicker calculations, not just colors and durations. Use
`RENDER_INTERVAL_MS = 16` for every animation, plus `FLICKER_INTERVAL_MS = 40`
for changing flicker values. Keep the remaining fixed timings as named constants too.

## `fortune_button.cpp`

**Setup** (priority `LATE`):
- Get the strip as `static_cast<light::AddressableLight *>(light->get_output())`.
  The schema only accepts addressable lights.
- Turn the light on at full brightness and white, with no transition, then call
  `set_effect_active(true)` so normal uniform state updates do not overwrite
  the pixel buffer. Render the initial idle frame before scheduling a show.
- From then on the component writes pixels itself and calls `schedule_show()`.
  The light's gamma correction and color correction still apply. Keep the
  light state on at full brightness; render darkness and fades in the pixel
  colors rather than using competing light transitions.
- Set `pressed_ = false` and
  `armed_ = button_->has_state() && !button_->state` in setup. Register the
  callback after initializing these flags. There is no button-state polling
  in `loop()`.

**Button:**
- A released callback sets `armed_ = true`. Act on that release only if
  `pressed_` recorded a fresh press, then clear `pressed_`:
  - 30–900 ms starts a fortune (ignored while spinning or in suspense)
  - 1500–8000 ms starts the easter egg
- A pressed callback only records the time and sets `pressed_` when `armed_`
  is true. A button held at boot therefore only arms input on release.
- An unknown state never arms input. With the current GPIO sensor and
  `delayed_on: 20ms`, released initial states pass through immediately during
  GPIO setup, before this component's `LATE` setup. No polling is needed.
  See [GPIO setup](https://github.com/esphome/esphome/blob/2026.8.0/esphome/components/gpio/binary_sensor/gpio_binary_sensor.cpp)
  and [DelayedOnFilter](https://github.com/esphome/esphome/blob/2026.8.0/esphome/components/binary_sensor/filter.cpp).
- Other hold lengths do nothing. Durations use the filtered sensor state,
  preserving today's `delayed_on: 20ms` behaviour.

**`loop()`** runs one phase (an enum) from the time elapsed since the phase
began:

| Phase | Behaviour |
|---|---|
| `IDLE` | violet (0.4, 0, 1), preserving the built-in pulse brightness range and curve with 1600 ms transitions and 1800 ms updates; after 60 s, fade out over 2 s |
| `SPIN` | 150 ms dark lead-in, then the full 5600 ms `think_spin` animation including its final 500 ms settle |
| `SUSPENSE` | 900 ms dark and silent, then the roll: the same MurmurHash3 mix and log line |
| `YES` | `fill_yes` for 1000 ms, then solid green plus the chime, held for the chime + 1500 ms |
| `NO` | red, three half-beat blinks, the trombone until it ends, then a fade to 15% over 1200 ms |
| `ANGRY` | magenta flicker for 4500 ms, freeze the last frame and fade it to black over 1500 ms, then 100 ms dark; the angry melody keeps playing into idle |

Use one class with small private methods for button handling, state entry,
rendering and audio updates. Store mutable animation state per instance,
replacing the lambdas' static variables. Use unsigned elapsed-time subtraction
for wrap-safe timing. Do not use blocking delays or chains of delayed callbacks.

The main sequence is `IDLE -> SPIN -> SUSPENSE -> YES/NO -> IDLE`.
`ANGRY` interrupts any state and returns to `IDLE`. A short press is ignored
through the spin lead-in, active spin, settle and suspense; it restarts the
sequence from idle or either verdict or angry state. Use elapsed-time
boundaries for the stages within each state.

Preserve the deliberate 2 s idle fade, 1200 ms no fade and 1500 ms angry fade,
including their interpolation curves. An explicit `light.turn_off` stops the
active effect before starting its transition, as verified in ESPHome 2026.8
[LightCall](https://github.com/esphome/esphome/blob/2026.8.0/esphome/components/light/light_call.cpp).
Stop flicker updates when the angry fade begins and scale a saved copy of
the last frame toward black. Derive each fade frame from that snapshot and
elapsed time, rather than repeatedly dimming the already dimmed pixels.
The idle timeout similarly stops the pulse and fades its last frame.
Omit the incidental 80 ms default transitions and document this simplification.

**Starting a fortune or the easter egg** first stops all sound and resets the
phase state. That fixes today's bug where a verdict script kept running after a
new press. There are no old delayed actions left to change the output later.
Entering `IDLE` alone does not cancel the angry melody: its 6750 ms table
continues for about 650 ms after the angry visuals finish at 6100 ms.

**Rendering and loop cadence.** Use one 16 ms render gate for all animations,
independent of audio. During active flicker, update its random values only
when 40 ms has elapsed. Avoid retransmitting identical static frames; call
`schedule_show()` only when a frame is rendered.

The old spin effect requested 8 ms updates but ran through the normal light
loop, whose [default cadence is about 16 ms](https://github.com/esphome/esphome/blob/2026.8.0/esphome/core/application.h).
Do not introduce an 8 ms renderer
or a fast-loop request for the spin. Preserve its motion maths and tick cap.
Keep one `HighFrequencyLoopRequester` only for the 12 ms contour, releasing
it on completion or cancellation. The 16 ms render gate still applies while
that request is active. Base contour and melody position on elapsed time so
late loops skip expired steps rather than stretching the sound.

**Sound.** Everything goes to the one output:
- the contour player, moved from `pwm_speech.cpp`
- a melody player that steps through a `Note` table by elapsed time
- spin ticks

`silence()` stops all three and sets the output level to 0.

All frequency and duty writes belong to these private sound helpers. Audio
has its own start time and playback position, independent of the visual
state, so the angry melody can continue into idle. State transitions decide
explicitly whether to stop audio or let it finish. Rest notes set duty to
zero without requesting a zero PWM frequency.

**Shutdown.** Silence the buzzer and release the fast-loop request in the
component's shutdown handler. The existing YAML shutdown automation still
turns off accessory power.

## Other edits

- `audio/build_from_ref.py`: new output path
  (`../components/fortune_button/contour_data.h`), namespace
  `fortune_button`, comments updated.
- `README.md`:
  - the layout and the YAML block
  - "Settings" now points at the constants in `fortune_button.h`
  - the `pwm_speech` section is replaced by a short `fortune_button` section
  - the idle row reflects the verified pulse brightness range
  - exclusive ring/buzzer ownership, LEDC requirement, and omission of the
    incidental 80 ms transitions are documented
  - the tick pitch is documented as E7 at 2637 Hz
- Delete `PLAN.md` when done.

## Acceptance checks (by the user)

1. `esphome config fortune-button.yaml` validates.
2. On the hardware, the look and sound match today:
   - idle breathe
   - the spin ticks
   - yes: green fill and chime
   - no: blinks and trombone
   - a 1.5 s hold gives the easter egg
   - after 60 s idle the ring fades out
3. A press during the spin is ignored, and a press during a verdict restarts
   cleanly.
4. Holding the button while plugging in doesn't start anything when it's
   released, including when its delayed pressed state arrives after setup;
   booting with the button released accepts the first fresh press.
5. The 150 ms dark lead-in is followed by a full 5.6 s spin, including the
   final 500 ms settle; tick pitch is E7 at 2637 Hz as requested.
6. A short press during either verdict or the easter egg cancels the old
   sequence cleanly; a long hold during a spin starts the easter egg.
7. The angry melody finishes after the visuals return to idle, and a fresh
   accepted press cuts it off. Fades, breathing and flicker match the original
   apart from the documented omission of incidental 80 ms transitions. The
   angry fade is smooth from the last flickered frame, with no new flicker.
8. `yes_percent: 0` always gives no, `yes_percent: 100` always gives yes;
   out-of-range odds and non-LEDC outputs fail configuration validation.
9. Every animation uses the single 16 ms render cadence; flicker values change
   only every 40 ms. Rendering stays throttled while the contour's fast loop
   runs; shutdown silences the output and releases the fast-loop request.

The implementation work should include static review of schema/codegen,
timing boundaries, note tables and migrated contour lengths. Compilation,
configuration validation and hardware checks above are performed by the user.
