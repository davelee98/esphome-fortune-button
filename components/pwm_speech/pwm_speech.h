#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/output/float_output.h"

namespace esphome {
namespace pwm_speech {

struct Contour;

/// Owns the buzzer for contours: a frequency and level per fixed-length
/// step (e.g. the sad trombone). These start and return immediately; loop()
/// replays them from elapsed time, so the total length is exact however
/// often loop() runs. Only one contour plays at a time.
class PwmSpeech : public Component {
 public:
  void set_output(output::FloatOutput *output) { this->output_ = output; }
  void set_contour_level(float level) { this->contour_level_ = level; }

  void dump_config() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  /// Start contour `index` from CONTOURS. Returns at once; loop() plays it.
  void play_contour(uint8_t index);

  /// Silence whatever is playing and park the output at 0% duty.
  void stop();

  /// True while a contour is playing.
  bool is_playing() const { return this->contour_ != nullptr; }

 protected:
  output::FloatOutput *output_{nullptr};
  float contour_level_{0.5f};

  const Contour *contour_{nullptr};
  uint32_t contour_start_ms_{0};
  int32_t contour_step_{-1};
  /// Keeps the main loop running continuously while a contour plays, so
  /// each 12 ms step lands on time instead of on the ~16 ms default tick.
  HighFrequencyLoopRequester high_freq_;
};

/// pwm_speech.play - start a contour. Returns at once; wait with
/// pwm_speech.is_playing.
template<typename... Ts> class PlayAction : public Action<Ts...>, public Parented<PwmSpeech> {
 public:
  void set_contour(uint8_t index) { this->index_ = index; }

  void play(const Ts &...x) override { this->parent_->play_contour(this->index_); }

 protected:
  uint8_t index_{0};
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<PwmSpeech> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

template<typename... Ts> class IsPlayingCondition : public Condition<Ts...>, public Parented<PwmSpeech> {
 public:
  bool check(const Ts &...x) override { return this->parent_->is_playing(); }
};

}  // namespace pwm_speech
}  // namespace esphome
