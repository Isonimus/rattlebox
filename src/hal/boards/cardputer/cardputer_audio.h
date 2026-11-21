#ifndef CARDPUTER_AUDIO_H
#define CARDPUTER_AUDIO_H

#include <atomic>
#include <ESP_I2S.h>
#include <freertos/FreeRTOS.h>
#include "../../audio_hal.h"
#include "../../../sound/clack_synth.h"

/**
 * @brief Cardputer ADV sound: clacks rendered as samples and sent over I2S to the ES8311 codec.
 *
 * A task on the core the frame loop does not use renders and writes 5 ms blocks, so the speaker is
 * fed while the loop draws. The loop's clacks wait under a spinlock for the next block.
 */
class CardputerAudio : public AudioHAL {
public:
    bool begin() override;
    const char* failureReason() const override { return _failure; }
    const char* description() const override { return "ES8311 over I2S, 48 kHz 16-bit stereo"; }

    void clack(float loudness) override;
    uint32_t takeLongestRenderUs() override { return _longestRenderUs.exchange(0); }

private:
    static void synthTask(void* self);
    void renderForever();

    // M5Unified's Cardputer speaker pins (esp32-dragon-radar:ADR-0003).
    static constexpr int I2S_BCK_PIN  = 41;
    static constexpr int I2S_LRCK_PIN = 43;
    static constexpr int I2S_DATA_PIN = 42;
    // 48 kHz in 16-bit stereo slots is what M5Unified feeds this codec, so its ES8311 clock setup
    // holds unchanged.
    static constexpr uint32_t SAMPLE_RATE_HZ = 48000;
    // A feel value, tuned by ear on the speaker: a full-loudness clack's noise before its filter.
    static constexpr int16_t AMPLITUDE = INT16_MAX / 2;
    // 5 ms a block: a clack lands within that of its step, and the task wakes 200 times a second.
    static constexpr size_t FRAMES_PER_BLOCK = 240;
    static constexpr uint32_t SYNTH_TASK_STACK_BYTES = 4096;
    // Above the Arduino loop's 1, so a busy loop cannot starve the DMA; on the other core anyway.
    static constexpr UBaseType_t SYNTH_TASK_PRIORITY = 2;
    // The Arduino loop runs on core 1.
    static constexpr BaseType_t SYNTH_TASK_CORE = 0;

    I2SClass              _i2s;
    ClackSynth            _synth{SAMPLE_RATE_HZ, AMPLITUDE};
    portMUX_TYPE          _pendingLock = portMUX_INITIALIZER_UNLOCKED;
    float                 _pendingLoudness = 0;
    std::atomic<uint32_t> _longestRenderUs{0};
    int16_t               _block[2 * FRAMES_PER_BLOCK];
    const char*           _failure = "";
};

#endif // CARDPUTER_AUDIO_H
