#include <unity.h>
#include <initializer_list>
#include "../../src/imu/imu_decode.h"
#include "../../src/imu/imu_decode.cpp"

// The test encodes its own bytes from the datasheets' layouts rather than reusing the
// decoder's helpers, so a byte-order or offset mistake in the decoder cannot cancel out.
static const float COUNTS_AT_FULL_SCALE = 32768.0f;
static const float TOLERANCE = 1e-4f;
static const ImuFullScale SKELETON_RANGES{8.0f, 2000.0f};

// Distinct, asymmetric counts per axis: swapping two axes or the two bytes of a count
// changes the decoded value.
static const int16_t ACCEL_COUNTS[3] = {0x1234, -0x0F21, 0x0102};
static const int16_t GYRO_COUNTS[3] = {-0x2A3B, 0x0410, 0x7E01};

static void putLittleEndian(uint8_t* bytes, int16_t count) {
    const uint16_t bits = static_cast<uint16_t>(count);
    bytes[0] = static_cast<uint8_t>(bits & 0xFF);
    bytes[1] = static_cast<uint8_t>(bits >> 8);
}

static void putBigEndian(uint8_t* bytes, int16_t count) {
    const uint16_t bits = static_cast<uint16_t>(count);
    bytes[0] = static_cast<uint8_t>(bits >> 8);
    bytes[1] = static_cast<uint8_t>(bits & 0xFF);
}

static void fillBmi270(uint8_t (&burst)[BMI270_BURST_BYTES], const int16_t accel[3], const int16_t gyro[3]) {
    for (int axis = 0; axis < 3; axis++) {
        putLittleEndian(&burst[axis * 2], accel[axis]);
        putLittleEndian(&burst[6 + axis * 2], gyro[axis]);
    }
}

static const uint8_t TEMPERATURE_HIGH = 0x5A;
static const uint8_t TEMPERATURE_LOW = 0xA5;

static void fillMpu6886(uint8_t (&burst)[MPU6886_BURST_BYTES], const int16_t accel[3], const int16_t gyro[3]) {
    for (int axis = 0; axis < 3; axis++) {
        putBigEndian(&burst[axis * 2], accel[axis]);
        putBigEndian(&burst[8 + axis * 2], gyro[axis]);
    }
    // Bytes 6 and 7 are the temperature; a decoder that reads gyro from offset 6 picks these up.
    burst[6] = TEMPERATURE_HIGH;
    burst[7] = TEMPERATURE_LOW;
}

static void assertReading(const SensorReading& reading, const int16_t accel[3], const int16_t gyro[3],
                          ImuFullScale fullScale) {
    const float accelPerCount = fullScale.accelG / COUNTS_AT_FULL_SCALE;
    const float gyroPerCount = fullScale.gyroDps / COUNTS_AT_FULL_SCALE;
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, accel[0] * accelPerCount, reading.accelG.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, accel[1] * accelPerCount, reading.accelG.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, accel[2] * accelPerCount, reading.accelG.z);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, gyro[0] * gyroPerCount, reading.gyroDps.x);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, gyro[1] * gyroPerCount, reading.gyroDps.y);
    TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, gyro[2] * gyroPerCount, reading.gyroDps.z);
}

void test_bmi270_decodes_little_endian_counts_per_axis() {
    uint8_t burst[BMI270_BURST_BYTES];
    fillBmi270(burst, ACCEL_COUNTS, GYRO_COUNTS);
    assertReading(decodeBmi270(burst, SKELETON_RANGES), ACCEL_COUNTS, GYRO_COUNTS, SKELETON_RANGES);
}

void test_mpu6886_decodes_big_endian_counts_and_skips_temperature() {
    uint8_t burst[MPU6886_BURST_BYTES];
    fillMpu6886(burst, ACCEL_COUNTS, GYRO_COUNTS);
    assertReading(decodeMpu6886(burst, SKELETON_RANGES), ACCEL_COUNTS, GYRO_COUNTS, SKELETON_RANGES);
}

void test_full_scale_counts_reach_the_configured_range() {
    const int16_t accel[3] = {INT16_MIN, INT16_MAX, 0};
    const int16_t gyro[3] = {INT16_MAX, 0, INT16_MIN};
    uint8_t bmi[BMI270_BURST_BYTES];
    uint8_t mpu[MPU6886_BURST_BYTES];
    fillBmi270(bmi, accel, gyro);
    fillMpu6886(mpu, accel, gyro);

    for (const SensorReading& reading : {decodeBmi270(bmi, SKELETON_RANGES), decodeMpu6886(mpu, SKELETON_RANGES)}) {
        // The most negative count is exactly full scale, which a sign-extension bug breaks.
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, -8.0f, reading.accelG.x);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 8.0f * INT16_MAX / COUNTS_AT_FULL_SCALE, reading.accelG.y);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 0.0f, reading.accelG.z);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, 2000.0f * INT16_MAX / COUNTS_AT_FULL_SCALE, reading.gyroDps.x);
        TEST_ASSERT_FLOAT_WITHIN(TOLERANCE, -2000.0f, reading.gyroDps.z);
    }
}

void test_the_scale_follows_the_configured_range() {
    const ImuFullScale otherRanges{16.0f, 500.0f};
    uint8_t bmi[BMI270_BURST_BYTES];
    uint8_t mpu[MPU6886_BURST_BYTES];
    fillBmi270(bmi, ACCEL_COUNTS, GYRO_COUNTS);
    fillMpu6886(mpu, ACCEL_COUNTS, GYRO_COUNTS);
    assertReading(decodeBmi270(bmi, otherRanges), ACCEL_COUNTS, GYRO_COUNTS, otherRanges);
    assertReading(decodeMpu6886(mpu, otherRanges), ACCEL_COUNTS, GYRO_COUNTS, otherRanges);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bmi270_decodes_little_endian_counts_per_axis);
    RUN_TEST(test_mpu6886_decodes_big_endian_counts_and_skips_temperature);
    RUN_TEST(test_full_scale_counts_reach_the_configured_range);
    RUN_TEST(test_the_scale_follows_the_configured_range);
    return UNITY_END();
}
