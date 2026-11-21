#ifndef M5STICKC_AUDIO_H
#define M5STICKC_AUDIO_H

#include <atomic>
#include <esp_timer.h>
#include "../../audio_hal.h"

/**
 * @brief M5StickC Plus 2 sound: each clack a short click on the passive buzzer on GPIO 2.
 *
 * The buzzer has no amplitude to set, only a drive to switch, so loudness is the duty cycle of an
 * LEDC square wave. A one-shot timer ends the click, so the loop never waits for it.
 */
class M5StickCAudio : public AudioHAL {
public:
    bool begin() override;
    const char* failureReason() const override { return _failure; }
    const char* description() const override { return "LEDC clicks on the buzzer at G2"; }

    void clack(float loudness) override;
    // The LEDC peripheral makes the square wave; nothing is rendered.
    uint32_t takeLongestRenderUs() override { return 0; }

private:
    static void endClick(void* self);

    static constexpr int BUZZER_PIN = 2;
    // Feel values, tuned by ear on the buzzer (slice 0013): 4 kHz for 2 ms, eight cycles, was heard
    // as a high chirp, a pitch rather than a knock. Two and a half cycles is too short to hear as one.
    static constexpr uint32_t CLICK_HZ = 2500;
    static constexpr uint64_t CLICK_US = 1000;
    static constexpr uint8_t DUTY_BITS = 8;
    // Half on, half off: the most a square wave can drive the buzzer.
    static constexpr uint32_t LOUDEST_DUTY = 1u << (DUTY_BITS - 1);

    esp_timer_handle_t    _clickEnd = nullptr;
    // Written by the loop and by the timer's task.
    std::atomic<uint32_t> _duty{0};
    const char*           _failure = "";
};

#endif // M5STICKC_AUDIO_H
