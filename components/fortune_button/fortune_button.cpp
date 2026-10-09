#include "fortune_button.h"
#include "contour_data.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace fortune_button {

static const char *const TAG = "fortune_button";
static constexpr Color IDLE_COLOR(102, 0, 255);
static constexpr Color YES_COLOR(0, 255, 38);
static constexpr Color NO_COLOR(255, 0, 0);
static constexpr Color ANGRY_COLOR(255, 0, 255);

// ESPHome LightTransformer's quintic interpolation, applied before gamma.
static float smooth_progress(uint32_t elapsed, uint32_t duration) {
  const float x = fminf(elapsed / float(duration), 1.0f);
  return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

void FortuneButton::setup() {
  this->strip_ = static_cast<light::AddressableLight *>(this->light_->get_output());
  if (this->strip_->size() <= 0) {
    ESP_LOGE(TAG, "The ring must have at least one LED");
    this->mark_failed();
    return;
  }
  this->frame_.resize(this->strip_->size());
  this->fade_frame_.resize(this->strip_->size());
  this->silence_();

  if (!this->share_light_) {
    auto call = this->light_->turn_on();
    call.set_brightness(1.0f);
    call.set_color_brightness(1.0f);
    call.set_rgb(1.0f, 1.0f, 1.0f);
    call.set_white_if_supported(0.0f);
    call.set_transition_length(uint32_t{0});
    call.set_save(false);
    call.perform();
    this->strip_->set_effect_active(true);
    this->strip_->all() = Color::BLACK;
    this->frame_dirty_ = true;
  }

  this->pressed_ = false;
  this->armed_ = this->button_->has_state() && !this->button_->state;
  this->button_->add_on_state_callback([this](bool pressed) { this->on_button_(pressed); });
  this->enter_phase_(Phase::IDLE, millis());
  if (!this->share_light_)
    this->render_(millis());  // Overwrite setup's white buffer before its queued transmission.
}

void FortuneButton::dump_config() {
  ESP_LOGCONFIG(TAG, "Fortune Button:");
  ESP_LOGCONFIG(TAG, "  Yes probability: %u%%", unsigned(this->yes_percent_));
  ESP_LOGCONFIG(TAG, "  Ring: %d LEDs", int(this->strip_->size()));
  ESP_LOGCONFIG(TAG, "  Tick pitch: %u Hz", unsigned(SPIN_TONE_HZ));
  ESP_LOGCONFIG(TAG, "  Share light: %s", this->share_light_ ? "YES" : "NO");
}

void FortuneButton::on_button_(bool pressed) {
  const uint32_t now = millis();
  if (pressed) {
    if (this->armed_ && !this->pressed_) {
      this->press_start_ms_ = now;
      this->pressed_ = true;
    }
    return;
  }
  this->armed_ = true;
  if (!this->pressed_)
    return;
  this->pressed_ = false;
  const uint32_t held = now - this->press_start_ms_;
  if (held >= LONG_PRESS_MIN_MS && held <= LONG_PRESS_MAX_MS) {
    this->silence_();
    this->enter_phase_(Phase::ANGRY, now);
  } else if (held >= SHORT_PRESS_MIN_MS && held <= SHORT_PRESS_MAX_MS && this->phase_ != Phase::SPIN &&
             this->phase_ != Phase::SUSPENSE) {
    this->silence_();
    this->enter_phase_(Phase::SPIN, now);
  }
}

void FortuneButton::enter_phase_(Phase phase, uint32_t now) {
  const bool was_active = this->phase_ != Phase::IDLE && this->phase_ != Phase::IDLE_OFF;
  const bool active = phase != Phase::IDLE && phase != Phase::IDLE_OFF;
  if (!was_active && active) {
    this->redraw_ = true;
    this->start_trigger_.trigger();  // Hand off before ANGRY's immediate pixel fill.
  }
  this->phase_ = phase;
  this->phase_start_ms_ = now;
  this->sound_started_ = false;
  this->fade_started_ = false;
  this->render_pending_ = true;
  if (phase == Phase::IDLE) {
    // Match idle_glow's initial 10%; the pulse itself ranges from 0 to 100%.
    this->pulse_start_level_ = this->pulse_target_level_ = IDLE_INITIAL_LEVEL;
    this->pulse_transition_start_ms_ = now;
  } else if (phase == Phase::SPIN) {
    const int ticks = SPIN_TICKS - SPIN_TICK_SPREAD + int(random_uint32() % (2 * SPIN_TICK_SPREAD + 1));
    this->spin_final_step_ = ticks - 1;
    const float t = (SPIN_MS - SPIN_SETTLE_MS) / 1000.0f;
    this->spin_pos_ = 0.0f;
    this->spin_speed_ = 2.0f * this->spin_final_step_ / t - SPIN_END_SPEED;
    this->spin_decel_ = (this->spin_speed_ - SPIN_END_SPEED) / t;
    this->spin_step_ = -1;
    this->spin_last_ms_ = now + SPIN_LEAD_IN_MS;
    ESP_LOGD(TAG, "%d ticks, start %.1f LED/s", ticks, this->spin_speed_);
  } else if (phase == Phase::ANGRY) {
    this->start_melody_(ANGRY_NOTES, sizeof(ANGRY_NOTES) / sizeof(Note), now);
    this->last_flicker_ms_ = now;
    this->fill_(ANGRY_COLOR);
  }
  if (was_active && !active)
    this->finish_trigger_.trigger();
}

void FortuneButton::loop() {
  const uint32_t now = millis();
  this->update_phase_(now);
  this->update_audio_(now);
  // Allow 1 ms of loop jitter so a 16 ms main loop renders every pass.
  if (this->render_pending_ || now - this->last_render_ms_ >= RENDER_INTERVAL_MS - 1)
    this->render_(now);
}

void FortuneButton::update_phase_(uint32_t now) {
  const uint32_t elapsed = now - this->phase_start_ms_;
  switch (this->phase_) {
    case Phase::SPIN:
      if (elapsed >= SPIN_LEAD_IN_MS + SPIN_MS) {
        this->silence_();
        this->enter_phase_(Phase::SUSPENSE, now);
      } else if (elapsed >= SPIN_LEAD_IN_MS) {
        this->update_spin_(now);
      }
      break;
    case Phase::SUSPENSE:
      if (elapsed >= SUSPENSE_MS) {
        uint32_t z = random_uint32() ^ micros();
        z = (z ^ (z >> 16)) * 0x85ebca6bu;
        z = (z ^ (z >> 13)) * 0xc2b2ae35u;
        z ^= z >> 16;
        const uint32_t roll = z % 100;
        const bool yes = roll < this->yes_percent_;
        ESP_LOGI("fortune", "Roll %u: %s", unsigned(roll), yes ? "yes" : "no");
        this->enter_phase_(yes ? Phase::YES : Phase::NO, now);
      }
      break;
    case Phase::YES:
      if (elapsed >= YES_FILL_MS + YES_CHIME_MS + YES_HOLD_MS) {
        this->silence_();
        this->enter_phase_(Phase::IDLE, now);
      } else if (elapsed >= YES_FILL_MS && !this->sound_started_) {
        this->start_melody_(YES_NOTES, sizeof(YES_NOTES) / sizeof(Note), this->phase_start_ms_ + YES_FILL_MS);
        this->sound_started_ = true;
      }
      break;
    case Phase::NO:
      if (elapsed >= NO_BLINK_MS + TROMBONE_MS + NO_FADE_MS) {
        this->silence_();
        this->enter_phase_(Phase::IDLE, now);
      } else if (elapsed >= NO_BLINK_MS && !this->sound_started_) {
        this->start_contour_(this->phase_start_ms_ + NO_BLINK_MS);
        this->sound_started_ = true;
      }
      break;
    case Phase::ANGRY:
      if (elapsed >= ANGRY_FLICKER_MS + ANGRY_FADE_MS + ANGRY_DARK_MS)
        this->enter_phase_(Phase::IDLE, now);  // Let the angry melody finish in idle.
      break;
    case Phase::IDLE:
      if (elapsed >= IDLE_TIMEOUT_MS + IDLE_FADE_MS)
        this->enter_phase_(Phase::IDLE_OFF, now);  // Stay dark across millis() rollover.
      break;
    case Phase::IDLE_OFF:
      break;
  }
}

void FortuneButton::update_spin_(uint32_t now) {
  if (this->spin_step_ < 0)
    this->output_->update_frequency(SPIN_TONE_HZ);
  const float dt = (now - this->spin_last_ms_) / 1000.0f;
  this->spin_last_ms_ = now;
  const float speed = fmaxf(SPIN_END_SPEED, this->spin_speed_ - this->spin_decel_ * dt);
  this->spin_pos_ += 0.5f * (this->spin_speed_ + speed) * dt;
  this->spin_pos_ = fminf(this->spin_pos_, this->spin_final_step_);
  this->spin_speed_ = speed;
  const int step = int(this->spin_pos_);
  if (step != this->spin_step_) {
    this->spin_step_ = step;
    const float duration = fminf(1000.0f / speed * SPIN_TONE_FRACTION, 30000.0f / TEMPO_BPM);
    this->output_->set_level(SPIN_TONE_LEVEL);
    this->tone_off_ms_ = now + uint32_t(duration);
    this->tone_on_ = true;
  } else if (this->tone_on_ && int32_t(now - this->tone_off_ms_) >= 0) {
    this->output_->set_level(0.0f);
    this->tone_on_ = false;
  }
}

float FortuneButton::pulse_level_(uint32_t now) const {
  const float progress = smooth_progress(now - this->pulse_transition_start_ms_, PULSE_TRANSITION_MS);
  return this->pulse_start_level_ + (this->pulse_target_level_ - this->pulse_start_level_) * progress;
}

void FortuneButton::render_(uint32_t now) {
  this->render_pending_ = false;
  this->last_render_ms_ = now;
  if (this->share_light_ && (this->phase_ == Phase::IDLE || this->phase_ == Phase::IDLE_OFF))
    return;
  const uint32_t elapsed = now - this->phase_start_ms_;
  switch (this->phase_) {
    case Phase::IDLE:
      if (elapsed < IDLE_TIMEOUT_MS) {
        if (now - this->pulse_last_change_ms_ >= PULSE_INTERVAL_MS) {
          this->pulse_start_level_ = this->pulse_level_(now);
          this->pulse_target_level_ = this->pulse_up_next_ ? 1.0f : 0.0f;
          this->pulse_up_next_ = !this->pulse_up_next_;
          this->pulse_last_change_ms_ = this->pulse_transition_start_ms_ = now;
        }
        this->fill_(IDLE_COLOR * light::to_uint8_scale(this->pulse_level_(now)));
      } else {
        if (!this->fade_started_) {
          this->fade_frame_ = this->frame_;
          this->fade_started_ = true;
        }
        this->fade_(1.0f - smooth_progress(elapsed - IDLE_TIMEOUT_MS, IDLE_FADE_MS));
      }
      break;
    case Phase::SPIN:
      if (elapsed < SPIN_LEAD_IN_MS) {
        this->fill_(Color::BLACK);
      } else {
        const uint8_t white = uint8_t(255 * SPIN_WHITE_LEVEL);
        const int n = this->strip_->size();
        int pointer = (this->spin_step_ < 0 ? 0 : this->spin_step_) % n;
        if (!RING_CLOCKWISE)
          pointer = (n - pointer) % n;
        // Set the final color once per pixel, avoiding false dirty frames.
        for (int i = 0; i < n; i++)
          this->set_pixel_(i, i == pointer ? Color(0, 0, 255) : Color(white, white, white));
      }
      break;
    case Phase::IDLE_OFF:
    case Phase::SUSPENSE:
      this->fill_(Color::BLACK);
      break;
    case Phase::YES: {
      const int n = this->strip_->size();
      const int lit = elapsed >= YES_FILL_MS ? n : 1 + int(elapsed * (n - 1) / YES_FILL_MS);
      for (int k = 0; k < n; k++) {
        const int index = RING_CLOCKWISE ? k : (n - k) % n;
        this->set_pixel_(index, k < lit ? YES_COLOR : Color::BLACK);
      }
      break;
    }
    case Phase::NO: {
      const uint32_t fade_start = NO_BLINK_MS + TROMBONE_MS;
      if (elapsed < NO_BLINK_MS) {
        this->fill_((elapsed / NO_HALF_BEAT_MS) % 2 == 0 ? NO_COLOR : Color::BLACK);
      } else if (elapsed < fade_start) {
        this->fill_(NO_COLOR);
      } else {
        // The contour holds solid red; derive the fade from that fixed frame.
        const float level = 1.0f + (NO_FINAL_LEVEL - 1.0f) * smooth_progress(elapsed - fade_start, NO_FADE_MS);
        this->fill_(NO_COLOR * light::to_uint8_scale(level));
      }
      break;
    }
    case Phase::ANGRY:
      if (elapsed < ANGRY_FLICKER_MS) {
        if (now - this->last_flicker_ms_ >= FLICKER_INTERVAL_MS) {
          this->last_flicker_ms_ = now;
          const uint8_t intensity = light::to_uint8_scale(FLICKER_INTENSITY);
          uint32_t rng = random_uint32();
          // Same random dimming and recovery as AddressableFlickerEffect.
          for (size_t i = 0; i < this->frame_.size(); i++) {
            rng = rng * 0x9E3779B9u + 0x9E37u;
            const uint8_t flicker = (rng & 0xFF) % intensity;
            auto pixel = (*this->strip_)[i];
            pixel = pixel.get() * (255 - flicker);
            this->frame_[i] = (pixel.get() * (255 - intensity)) + (ANGRY_COLOR * intensity);
            pixel = this->frame_[i];
          }
          this->frame_dirty_ = true;
        }
      } else {
        if (!this->fade_started_) {
          this->fade_frame_ = this->frame_;
          this->fade_started_ = true;
        }
        this->fade_(1.0f - smooth_progress(elapsed - ANGRY_FLICKER_MS, ANGRY_FADE_MS));
      }
      break;
  }
  if (this->frame_dirty_) {
    this->strip_->schedule_show();
    this->frame_dirty_ = false;
  }
  this->redraw_ = false;
}

void FortuneButton::set_pixel_(size_t index, Color color) {
  if (this->redraw_ || this->frame_[index] != color) {
    this->frame_[index] = color;
    (*this->strip_)[index] = color;
    this->frame_dirty_ = true;
  }
}

void FortuneButton::fill_(Color color) {
  for (size_t i = 0; i < this->frame_.size(); i++)
    this->set_pixel_(i, color);
}

void FortuneButton::fade_(float level) {
  const uint8_t scale = light::to_uint8_scale(level);
  for (size_t i = 0; i < this->frame_.size(); i++)
    this->set_pixel_(i, this->fade_frame_[i] * scale);
}

void FortuneButton::silence_() {
  this->melody_ = nullptr;
  this->melody_note_ = -1;
  this->contour_ = nullptr;
  this->contour_step_ = -1;
  this->tone_on_ = false;
  this->high_freq_.stop();
  if (this->output_ != nullptr)
    this->output_->set_level(0.0f);
}

void FortuneButton::start_melody_(const Note *notes, size_t count, uint32_t start_ms) {
  this->silence_();
  this->melody_ = notes;
  this->melody_count_ = count;
  this->melody_start_ms_ = start_ms;
}

void FortuneButton::start_contour_(uint32_t start_ms) {
  this->silence_();
  this->contour_ = &TROMBONE;
  this->contour_start_ms_ = start_ms;
  this->high_freq_.start();
}

void FortuneButton::update_audio_(uint32_t now) {
  if (this->contour_ != nullptr) {
    const uint32_t step = (now - this->contour_start_ms_) / this->contour_->step_ms;
    if (step >= this->contour_->steps) {
      this->silence_();
    } else if (int32_t(step) != this->contour_step_) {
      this->contour_step_ = step;
      const uint16_t hz = this->contour_->freq[step];
      if (hz != 0)
        this->output_->update_frequency(hz);
      this->output_->set_level(hz == 0 ? 0.0f : this->contour_->level[step] / 255.0f * TROMBONE_LEVEL);
    }
  } else if (this->melody_ != nullptr) {
    uint32_t elapsed = now - this->melody_start_ms_;
    size_t note = 0;
    while (note < this->melody_count_ && elapsed >= this->melody_[note].ms) {
      elapsed -= this->melody_[note].ms;
      note++;
    }
    if (note == this->melody_count_) {
      this->silence_();
    } else if (int32_t(note) != this->melody_note_) {
      this->melody_note_ = note;
      const uint16_t hz = this->melody_[note].hz;
      if (hz != 0)
        this->output_->update_frequency(hz);
      this->output_->set_level(hz == 0 ? 0.0f : MELODY_LEVEL);
    }
  }
}

}  // namespace fortune_button
}  // namespace esphome
