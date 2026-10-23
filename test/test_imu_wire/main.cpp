#include <unity.h>
#include "../../src/imu/imu_decode.h"
#include "../../src/imu/imu_decode.cpp"
#include "../../src/imu/axis_map.cpp"
#include "../../src/probes/imu/imu_wire.h"
#include "../../src/probes/imu/imu_wire.cpp"

// The round trip runs through the real decoder and remap, so the test covers the values
// the probe will actually pack, not ones a test happened to pick.

static const ImuFullScale PROBE_RANGES{16.0f, 2000.0f};
static const float COUNTS_AT_FULL_SCALE = 32768.0f;

static float decodedValue(int16_t count, float fullScale) {
    uint8_t burst[BMI270_BURST_BYTES] = {};
    const uint16_t bits = static_cast<uint16_t>(count);
    burst[0] = static_cast<uint8_t>(bits & 0xFF);
    burst[1] = static_cast<uint8_t>(bits >> 8);
    return decodeBmi270(burst, {fullScale, fullScale}).accelG.x;
}

void test_every_count_survives_decode_and_pack() {
    for (int32_t count = INT16_MIN; count <= INT16_MAX; count++) {
        const float value = decodedValue(static_cast<int16_t>(count), PROBE_RANGES.accelG);
        TEST_ASSERT_EQUAL_INT16(count, packCount(value, PROBE_RANGES.accelG));
    }
}

void test_a_sign_flipped_axis_survives_except_at_plus_full_scale() {
    const AxisMap flipped{{SensorAxis::X, AxisSign::Negative}, {SensorAxis::Y, AxisSign::Positive},
                          {SensorAxis::Z, AxisSign::Negative}};
    for (int32_t count = INT16_MIN + 1; count <= INT16_MAX; count++) {
        const float value = decodedValue(static_cast<int16_t>(count), PROBE_RANGES.accelG);
        const Vec3 device = remapToDeviceFrame({value, 0.0f, 0.0f}, flipped);
        TEST_ASSERT_EQUAL_INT16(-count, packCount(device.x, PROBE_RANGES.accelG));
    }
    // -32768 flipped is +full scale, one count past the top of an int16.
    const Vec3 device = remapToDeviceFrame({decodedValue(INT16_MIN, PROBE_RANGES.accelG), 0.0f, 0.0f}, flipped);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, packCount(device.x, PROBE_RANGES.accelG));
}

void test_values_past_full_scale_clamp_to_the_ends() {
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, packCount(40.0f, PROBE_RANGES.accelG));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, packCount(-40.0f, PROBE_RANGES.accelG));
}

void test_a_sample_packs_each_axis_against_its_own_range() {
    const ImuSample sample{{1.0f, -2.0f, 0.5f}, {1000.0f, -250.0f, 0.0f}, 123456789u};
    const PackedSample packed = packSample(sample, PROBE_RANGES);
    TEST_ASSERT_EQUAL_UINT32(123456789u, packed.timestampUs);
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(1.0f / 16.0f * COUNTS_AT_FULL_SCALE), packed.accel[0]);
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(-2.0f / 16.0f * COUNTS_AT_FULL_SCALE), packed.accel[1]);
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(0.5f / 16.0f * COUNTS_AT_FULL_SCALE), packed.accel[2]);
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(1000.0f / 2000.0f * COUNTS_AT_FULL_SCALE), packed.gyro[0]);
    TEST_ASSERT_EQUAL_INT16(static_cast<int16_t>(-250.0f / 2000.0f * COUNTS_AT_FULL_SCALE), packed.gyro[1]);
    TEST_ASSERT_EQUAL_INT16(0, packed.gyro[2]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_count_survives_decode_and_pack);
    RUN_TEST(test_a_sign_flipped_axis_survives_except_at_plus_full_scale);
    RUN_TEST(test_values_past_full_scale_clamp_to_the_ends);
    RUN_TEST(test_a_sample_packs_each_axis_against_its_own_range);
    return UNITY_END();
}
