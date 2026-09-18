#include "pwm_speech.h"
#include "contour_data.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pwm_speech {

static const char *const TAG = "pwm_speech";

void PwmSpeech::dump_config() {
  ESP_LOGCONFIG(TAG, "PWM Speech:");
  ESP_LOGCONFIG(TAG, "  Contour level: %.0f%%", this->contour_level_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Contours: %u", CONTOUR_COUNT);
}

void PwmSpeech::loop() {
  const Contour *contour = this->contour_;
  if (contour == nullptr) {
    return;
  }

  // Step from elapsed time, so the total length is exact even when loop()
  // runs late. Only touch LEDC when the step changes.
  const uint32_t step = (millis() - this->contour_start_ms_) / contour->step_ms;
  if (step >= contour->steps) {
    this->stop();
    return;
  }
  if ((int32_t) step == this->contour_step_) {
    return;
  }
  this->contour_step_ = step;
  this->output_->update_frequency(contour->freq[step]);
  this->output_->set_level(contour->level[step] / 255.0f * this->contour_level_);
}

void PwmSpeech::play_contour(uint8_t index) {
  if (this->output_ == nullptr) {
    ESP_LOGW(TAG, "No output configured");
    return;
  }
  if (index >= CONTOUR_COUNT) {
    ESP_LOGW(TAG, "Contour %u out of range", index);
    return;
  }

  this->stop();
  ESP_LOGD(TAG, "Playing contour %u (%u steps)", index, CONTOURS[index].steps);
  this->contour_ = &CONTOURS[index];
  this->contour_start_ms_ = millis();
  this->contour_step_ = -1;
  this->high_freq_.start();
}

void PwmSpeech::stop() {
  this->contour_ = nullptr;
  this->contour_step_ = -1;
  this->high_freq_.stop();
  if (this->output_ != nullptr) {
    // Park the pin low so the drive transistor is off between sounds.
    this->output_->set_level(0.0f);
  }
}

}  // namespace pwm_speech
}  // namespace esphome
