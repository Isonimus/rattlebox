#include "es8311.h"
#include "cardputer_i2c.h"

static constexpr uint8_t ES8311_ADDRESS = 0x18;

struct RegisterWrite {
    uint8_t reg;
    uint8_t value;
};

// The values of M5Unified 0.2.27's _speaker_enabled_cb_cardputer_adv (MIT), read on 2026-10-24 and
// unchanged from the copy in Dragon Radar (esp32-dragon-radar:ADR-0004).
static constexpr RegisterWrite SPEAKER_ENABLE_SEQUENCE[] = {
    {0x00, 0x80},  // RESET: chip state machine on
    {0x01, 0xB5},  // CLK_MANAGER1: MCLK taken from BCLK (the ADV has no MCLK line)
    // CLK_MANAGER2: multiply BCLK x8 to reach the 256·fs MCLK the DAC expects. Only right for
    // 16-bit stereo slots (BCLK = 32·fs), which CardputerAudio sends.
    {0x02, 0x18},
    {0x0D, 0x01},  // SYSTEM: power up analog circuitry
    {0x12, 0x00},  // SYSTEM: power up the DAC
    {0x13, 0x10},  // SYSTEM: enable the output driver feeding the speaker amp
    {0x32, 0xBF},  // DAC volume: 0 dB; loudness is set in software
    {0x37, 0x08},  // DAC: bypass the equaliser
};

bool es8311BeginSpeaker() {
    // The keyboard starts the bus earlier in the boot, and its failure halts the boot with the
    // clear's reason (ADR-0019), so by here the bus is up and this call does nothing.
    if (cardputer_i2c::begin() != nullptr) return false;
    for (const RegisterWrite& write : SPEAKER_ENABLE_SEQUENCE) {
        if (!cardputer_i2c::writeRegister(ES8311_ADDRESS, write.reg, write.value)) return false;
    }
    return true;
}
