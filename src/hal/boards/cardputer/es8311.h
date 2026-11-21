#ifndef ES8311_H
#define ES8311_H

/**
 * @brief Power up the Cardputer ADV's ES8311 codec for speaker playback.
 *
 * The ADV routes the speaker through this codec, which stays silent until its DAC is configured
 * over I2C. Call once I2S is running: the codec takes its clock from BCLK.
 *
 * @return false if the codec did not acknowledge a write.
 */
bool es8311BeginSpeaker();

#endif // ES8311_H
