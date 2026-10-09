#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ledc/ledc_output.h"
#include "esphome/components/light/addressable_light.h"

namespace esphome {
namespace fortune_button {

// Hardware references and odds live in YAML; all behaviour tuning lives here.
constexpr bool RING_CLOCKWISE = true;
constexpr uint32_t IDLE_TIMEOUT_MS = 60000;
constexpr uint32_t IDLE_FADE_MS = 2000;
constexpr uint32_t PULSE_TRANSITION_MS = 1600;
constexpr uint32_t PULSE_INTERVAL_MS = 1800;
constexpr float IDLE_INITIAL_LEVEL = 0.1f;
constexpr uint32_t TEMPO_BPM = 120;
constexpr uint32_t YES_FILL_MS = 1000;
constexpr uint32_t YES_HOLD_MS = 1500;
constexpr uint32_t SHORT_PRESS_MIN_MS = 30;
constexpr uint32_t SHORT_PRESS_MAX_MS = 900;
constexpr uint32_t LONG_PRESS_MIN_MS = 1500;
constexpr uint32_t LONG_PRESS_MAX_MS = 8000;

// A 150 ms dark lead-in precedes the full spin, including its final settle.
constexpr uint32_t SPIN_LEAD_IN_MS = 150;
constexpr int SPIN_TICKS = 55;
constexpr int SPIN_TICK_SPREAD = 5;
constexpr float SPIN_END_SPEED = 2.0f;
constexpr uint32_t SPIN_MS = 5600;
constexpr uint32_t SPIN_SETTLE_MS = 500;
constexpr float SPIN_WHITE_LEVEL = 0.6f;
constexpr uint16_t SPIN_TONE_HZ = 2637;  // E7, nearest note to the buzzer's 2700 Hz resonance.
constexpr float SPIN_TONE_FRACTION = 0.5f;
constexpr float SPIN_TONE_LEVEL = 0.5f;  // 50% duty gives the loudest square wave.
constexpr uint32_t SUSPENSE_MS = 900;
constexpr uint32_t NO_BLINKS = 3;
constexpr uint32_t NO_FADE_MS = 1200;
constexpr float NO_FINAL_LEVEL = 0.15f;
constexpr uint32_t ANGRY_FLICKER_MS = 4500;
constexpr uint32_t ANGRY_FADE_MS = 1500;
constexpr uint32_t ANGRY_DARK_MS = 100;
constexpr float FLICKER_INTENSITY = 0.7f;
constexpr uint32_t RENDER_INTERVAL_MS = 16;
constexpr uint32_t FLICKER_INTERVAL_MS = 40;
constexpr float MELODY_LEVEL = 0.6f;  // Same duty as the previous RTTTL gain: 60%.
constexpr float TROMBONE_LEVEL = 0.5f;

static_assert(SPIN_TICK_SPREAD < SPIN_TICKS, "Spin must have at least one tick");
static_assert(SPIN_SETTLE_MS < SPIN_MS, "Spin must have a moving period");
static_assert(TEMPO_BPM > 0, "Tempo must be positive");

struct Note {
  uint16_t hz;  // Zero means a rest; never request zero PWM frequency.
  uint32_t ms;
};

// Transcribed from yes:d=8,o=5,b=120:c,4f. and the original angry RTTTL.
constexpr Note YES_NOTES[] = {{523, 30000 / TEMPO_BPM}, {698, 90000 / TEMPO_BPM}};
constexpr Note ANGRY_NOTES[] = {{523, 300}, {554, 300}, {523, 300}, {554, 300}, {440, 600}, {0, 150},
                              {523, 300}, {554, 300}, {392, 600}, {0, 1200}, {440, 2400}};
constexpr uint32_t YES_CHIME_MS = YES_NOTES[0].ms + YES_NOTES[1].ms;
constexpr uint32_t NO_HALF_BEAT_MS = 30000 / TEMPO_BPM;
constexpr uint32_t NO_BLINK_MS = NO_BLINKS * 2 * NO_HALF_BEAT_MS;

struct Contour;

class FortuneButton : public Component {
 public:
  void set_light(light::AddressableLightState *light) { this->light_ = light; }
  void set_output(ledc::LEDCOutput *output) { this->output_ = output; }
  void set_button(binary_sensor::BinarySensor *button) { this->button_ = button; }
  void set_yes_percent(uint8_t percent) { this->yes_percent_ = percent; }
  void set_brightness(float brightness) { this->brightness_ = light::to_uint8_scale(brightness); }
  void set_share_light(bool share) { this->share_light_ = share; }
  Trigger<> *get_start_trigger() { return &this->start_trigger_; }
  Trigger<> *get_finish_trigger() { return &this->finish_trigger_; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override { this->silence_(); }
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  enum class Phase : uint8_t { IDLE, IDLE_OFF, SPIN, SUSPENSE, YES, NO, ANGRY };
  void on_button_(bool pressed);
  void enter_phase_(Phase phase, uint32_t now);
  void update_phase_(uint32_t now);
  void update_spin_(uint32_t now);
  void render_(uint32_t now);
  void set_pixel_(size_t index, Color color);
  void fill_(Color color);
  void fade_(float level);
  float pulse_level_(uint32_t now) const;
  void silence_();
  void start_melody_(const Note *notes, size_t count, uint32_t start_ms);
  void start_contour_(uint32_t start_ms);
  void update_audio_(uint32_t now);

  light::AddressableLightState *light_{nullptr};
  light::AddressableLight *strip_{nullptr};
  ledc::LEDCOutput *output_{nullptr};
  binary_sensor::BinarySensor *button_{nullptr};
  uint8_t yes_percent_{50};
  uint8_t brightness_{255};  // Scales every pixel written; 255 leaves colours unchanged.
  bool share_light_{false};
  Trigger<> start_trigger_;
  Trigger<> finish_trigger_;
  bool armed_{false};
  bool pressed_{false};
  uint32_t press_start_ms_{0};

  Phase phase_{Phase::IDLE};
  uint32_t phase_start_ms_{0};
  uint32_t last_render_ms_{0};
  bool render_pending_{false};
  bool frame_dirty_{false};
  bool redraw_{false};
  bool sound_started_{false};
  bool fade_started_{false};
  std::vector<Color> frame_;
  std::vector<Color> fade_frame_;

  // Like PulseLightEffect, direction and last change survive idle restarts.
  bool pulse_up_next_{false};
  uint32_t pulse_last_change_ms_{0};
  uint32_t pulse_transition_start_ms_{0};
  float pulse_start_level_{IDLE_INITIAL_LEVEL};
  float pulse_target_level_{IDLE_INITIAL_LEVEL};
  uint32_t last_flicker_ms_{0};

  float spin_pos_{0.0f};
  float spin_speed_{0.0f};
  float spin_decel_{0.0f};
  int spin_final_step_{0};
  int spin_step_{-1};
  uint32_t spin_last_ms_{0};
  bool tone_on_{false};
  uint32_t tone_off_ms_{0};

  const Note *melody_{nullptr};
  size_t melody_count_{0};
  uint32_t melody_start_ms_{0};
  int32_t melody_note_{-1};
  const Contour *contour_{nullptr};
  uint32_t contour_start_ms_{0};
  int32_t contour_step_{-1};
  HighFrequencyLoopRequester high_freq_;
};

}  // namespace fortune_button
}  // namespace esphome
