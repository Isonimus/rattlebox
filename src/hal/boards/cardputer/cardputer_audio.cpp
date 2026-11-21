#include "cardputer_audio.h"
#include "es8311.h"

bool CardputerAudio::begin() {
    // I2S first: the codec takes its clock from BCLK.
    _i2s.setPins(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DATA_PIN);
    if (!_i2s.begin(I2S_MODE_STD, SAMPLE_RATE_HZ, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
        _failure = "I2S did not start";
        return false;
    }
    if (!es8311BeginSpeaker()) {
        _failure = "no ACK from the ES8311 codec at 0x18";
        return false;
    }
    if (xTaskCreatePinnedToCore(synthTask, "clack_synth", SYNTH_TASK_STACK_BYTES, this, SYNTH_TASK_PRIORITY, nullptr,
                                SYNTH_TASK_CORE) != pdPASS) {
        _failure = "no heap for the synth task";
        return false;
    }
    return true;
}

void CardputerAudio::clack(float loudness) {
    portENTER_CRITICAL(&_pendingLock);
    _pendingLoudness += loudness;
    portEXIT_CRITICAL(&_pendingLock);
}

void CardputerAudio::synthTask(void* self) { static_cast<CardputerAudio*>(self)->renderForever(); }

void CardputerAudio::renderForever() {
    for (;;) {
        portENTER_CRITICAL(&_pendingLock);
        const float loudness = _pendingLoudness;
        _pendingLoudness = 0;
        portEXIT_CRITICAL(&_pendingLock);

        const uint32_t startUs = micros();
        if (loudness > 0) _synth.clack(loudness);
        _synth.fill(_block, FRAMES_PER_BLOCK);
        const uint32_t renderUs = micros() - startUs;
        if (renderUs > _longestRenderUs.load()) _longestRenderUs.store(renderUs);

        // Blocks until the DMA has room, which paces the task to the sample rate. A short write is
        // a second's stall or a driver error: the speaker has stopped, and saying so beats silence.
        const size_t written = _i2s.write(reinterpret_cast<const uint8_t*>(_block), sizeof(_block));
        if (written != sizeof(_block)) {
            Serial.printf("[audio] FAILED: I2S wrote %u of %u bytes\n", static_cast<unsigned>(written),
                          static_cast<unsigned>(sizeof(_block)));
            abort();
        }
    }
}
