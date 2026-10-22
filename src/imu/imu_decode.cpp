#include "imu_decode.h"

namespace {

constexpr float FULL_SCALE_COUNTS = 32768.0f;
constexpr size_t BYTES_PER_AXIS = 2;
constexpr size_t BMI270_GYRO_OFFSET = 6;
constexpr size_t MPU6886_GYRO_OFFSET = 8;  // after accel (6) and temperature (2)

int16_t littleEndianCount(const uint8_t* bytes) {
    return static_cast<int16_t>(static_cast<uint16_t>(bytes[0] | (bytes[1] << 8)));
}

int16_t bigEndianCount(const uint8_t* bytes) {
    return static_cast<int16_t>(static_cast<uint16_t>((bytes[0] << 8) | bytes[1]));
}

template <typename CountDecoder>
Vec3 decodeAxes(const uint8_t* bytes, float fullScale, CountDecoder decodeCount) {
    const float perCount = fullScale / FULL_SCALE_COUNTS;
    return {decodeCount(bytes) * perCount,
            decodeCount(bytes + BYTES_PER_AXIS) * perCount,
            decodeCount(bytes + 2 * BYTES_PER_AXIS) * perCount};
}

} // namespace

SensorReading decodeBmi270(const uint8_t (&burst)[BMI270_BURST_BYTES], ImuFullScale fullScale) {
    return {decodeAxes(burst, fullScale.accelG, littleEndianCount),
            decodeAxes(burst + BMI270_GYRO_OFFSET, fullScale.gyroDps, littleEndianCount)};
}

SensorReading decodeMpu6886(const uint8_t (&burst)[MPU6886_BURST_BYTES], ImuFullScale fullScale) {
    return {decodeAxes(burst, fullScale.accelG, bigEndianCount),
            decodeAxes(burst + MPU6886_GYRO_OFFSET, fullScale.gyroDps, bigEndianCount)};
}
