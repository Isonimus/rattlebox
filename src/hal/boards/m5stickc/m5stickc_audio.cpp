#include "m5stickc_audio.h"
#include <Arduino.h>

bool M5StickCAudio::begin() {
    if (!ledcAttach(BUZZER_PIN, CLICK_HZ, DUTY_BITS)) {
        _failure = "no LEDC channel for the buzzer";
        return false;
    }
    if (!ledcWrite(BUZZER_PIN, 0)) {
        _failure = "LEDC write to the buzzer failed";
        return false;
    }
    const esp_timer_create_args_t args{endClick, this, ESP_TIMER_TASK, "click_end", false};
    if (esp_timer_create(&args, &_clickEnd) != ESP_OK) {
        _failure = "no heap for the click timer";
        return false;
    }
    return true;
}

void M5StickCAudio::clack(float loudness) {
    // At least one step on: a hit too quiet to hear still clicks, as the Cardputer still plays it.
    uint32_t duty = static_cast<uint32_t>(loudness * LOUDEST_DUTY);
    if (duty == 0) duty = 1;
    // A click ending on the timer's task while this runs can still drop this clack: the end's two
    // writes and these are not under one lock. Left so on purpose (slice 0013's review): it loses
    // one 1 ms click in a rattle of up to a hundred a second, and a lock around LEDC calls from two
    // tasks costs more than an inaudible click.
    // Stopped first, so the timer cannot end the click between the duty and the restart. Stopping a
    // timer that already fired is no error; it reports only that nothing was running.
    const esp_err_t stopped = esp_timer_stop(_clickEnd);
    if (stopped != ESP_OK && stopped != ESP_ERR_INVALID_STATE) abort();
    // A clack during a click carries on at the louder of the two.
    if (duty > _duty.load()) {
        _duty.store(duty);
        ledcWrite(BUZZER_PIN, duty);
    }
    // Fails only on a broken timer, which would leave the click on for good.
    if (esp_timer_start_once(_clickEnd, CLICK_US) != ESP_OK) abort();
}

void M5StickCAudio::endClick(void* self) {
    M5StickCAudio& audio = *static_cast<M5StickCAudio*>(self);
    audio._duty.store(0);
    ledcWrite(BUZZER_PIN, 0);
}
