#include <unity.h>
#include <string.h>
#include "../../src/imu/imu_settings.h"
#include "../../src/imu/imu_settings.cpp"

// Expected register values are written out from the datasheets' tables, not computed the
// way the translation computes them, so a wrong formula cannot agree with itself.

static const ImuSettings SKELETON{8, 2000, 100};

struct RangeCase {
    uint16_t value;
    uint8_t reg;
};

// BMI270 datasheet 5.2: ACC_RANGE (0x41) and GYR_RANGE (0x43).
static const RangeCase EXPECTED_BMI270_ACCEL_RANGES[] = {{2, 0x00}, {4, 0x01}, {8, 0x02}, {16, 0x03}};
static const RangeCase EXPECTED_BMI270_GYRO_RANGES[] = {{2000, 0x00}, {1000, 0x01}, {500, 0x02}, {250, 0x03}, {125, 0x04}};
// ACC_CONF (0x40) and GYR_CONF (0x42) with filter_perf set and the normal bandwidth: the
// low nibble is the ODR code, 0x06 for 25 Hz up to 0x0C for 1600 Hz.
static const RangeCase EXPECTED_BMI270_RATES[] = {{25, 0xA6}, {50, 0xA7}, {100, 0xA8}, {200, 0xA9},
                                         {400, 0xAA}, {800, 0xAB}, {1600, 0xAC}};

// MPU6886 register map: ACCEL_CONFIG (0x1C) and GYRO_CONFIG (0x1B) FS_SEL in bits 4:3.
static const RangeCase EXPECTED_MPU6886_ACCEL_RANGES[] = {{2, 0x00}, {4, 0x08}, {8, 0x10}, {16, 0x18}};
static const RangeCase EXPECTED_MPU6886_GYRO_RANGES[] = {{250, 0x00}, {500, 0x08}, {1000, 0x10}, {2000, 0x18}};
// SMPLRT_DIV (0x19): rate = 1 kHz / (1 + divider).
static const RangeCase EXPECTED_MPU6886_RATES[] = {{1000, 0}, {500, 1}, {250, 3}, {200, 4}, {125, 7}, {100, 9},
                                          {50, 19}, {40, 24}, {25, 39}, {20, 49}, {10, 99}, {5, 199}, {4, 249}};

// What a refused translation must leave behind: a sentinel no real setting produces.
static const uint8_t UNTOUCHED = 0xEE;

void test_bmi270_translates_every_supported_range_and_rate() {
    for (const RangeCase& range : EXPECTED_BMI270_ACCEL_RANGES) {
        Bmi270Registers registers{};
        TEST_ASSERT_NULL(toBmi270Registers({range.value, 2000, 100}, registers));
        TEST_ASSERT_EQUAL_HEX8(range.reg, registers.accRange);
    }
    for (const RangeCase& range : EXPECTED_BMI270_GYRO_RANGES) {
        Bmi270Registers registers{};
        TEST_ASSERT_NULL(toBmi270Registers({8, range.value, 100}, registers));
        TEST_ASSERT_EQUAL_HEX8(range.reg, registers.gyrRange);
    }
    for (const RangeCase& rate : EXPECTED_BMI270_RATES) {
        Bmi270Registers registers{};
        TEST_ASSERT_NULL(toBmi270Registers({8, 2000, rate.value}, registers));
        TEST_ASSERT_EQUAL_HEX8(rate.reg, registers.accConf);
        TEST_ASSERT_EQUAL_HEX8(rate.reg, registers.gyrConf);
    }
}

void test_bmi270_skeleton_settings_keep_the_skeletons_registers() {
    Bmi270Registers registers{};
    TEST_ASSERT_NULL(toBmi270Registers(SKELETON, registers));
    TEST_ASSERT_EQUAL_HEX8(0xA8, registers.accConf);
    TEST_ASSERT_EQUAL_HEX8(0x02, registers.accRange);
    TEST_ASSERT_EQUAL_HEX8(0xA8, registers.gyrConf);
    TEST_ASSERT_EQUAL_HEX8(0x00, registers.gyrRange);
}

static void assertBmi270Refuses(const ImuSettings& settings, const char* field) {
    Bmi270Registers registers{UNTOUCHED, UNTOUCHED, UNTOUCHED, UNTOUCHED};
    const char* reason = toBmi270Registers(settings, registers);
    TEST_ASSERT_NOT_NULL(reason);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(reason, field), reason);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.accConf);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.accRange);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.gyrConf);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.gyrRange);
}

void test_bmi270_refuses_what_it_cannot_run_exactly() {
    assertBmi270Refuses({3, 2000, 100}, "accel range");
    assertBmi270Refuses({32, 2000, 100}, "accel range");
    assertBmi270Refuses({8, 4000, 100}, "gyro range");
    assertBmi270Refuses({8, 2000, 120}, "output rate");
    // The gyro's slowest rate is 25 Hz and the accelerometer's fastest is 1600 Hz, so
    // rates the other sensor has are out.
    assertBmi270Refuses({8, 2000, 12}, "output rate");
    assertBmi270Refuses({8, 2000, 3200}, "output rate");
    assertBmi270Refuses({8, 2000, 0}, "output rate");
}

void test_mpu6886_translates_every_supported_range_and_rate() {
    for (const RangeCase& range : EXPECTED_MPU6886_ACCEL_RANGES) {
        Mpu6886Registers registers{};
        TEST_ASSERT_NULL(toMpu6886Registers({range.value, 2000, 100}, registers));
        TEST_ASSERT_EQUAL_HEX8(range.reg, registers.accelConfig);
    }
    for (const RangeCase& range : EXPECTED_MPU6886_GYRO_RANGES) {
        Mpu6886Registers registers{};
        TEST_ASSERT_NULL(toMpu6886Registers({8, range.value, 100}, registers));
        TEST_ASSERT_EQUAL_HEX8(range.reg, registers.gyroConfig);
    }
    for (const RangeCase& rate : EXPECTED_MPU6886_RATES) {
        Mpu6886Registers registers{};
        TEST_ASSERT_NULL(toMpu6886Registers({8, 2000, rate.value}, registers));
        TEST_ASSERT_EQUAL_UINT8(rate.reg, registers.sampleRateDivider);
    }
}

void test_mpu6886_skeleton_settings_keep_the_skeletons_registers() {
    Mpu6886Registers registers{};
    TEST_ASSERT_NULL(toMpu6886Registers(SKELETON, registers));
    TEST_ASSERT_EQUAL_HEX8(0x10, registers.accelConfig);
    TEST_ASSERT_EQUAL_HEX8(0x18, registers.gyroConfig);
    TEST_ASSERT_EQUAL_UINT8(9, registers.sampleRateDivider);
}

static void assertMpu6886Refuses(const ImuSettings& settings, const char* field) {
    Mpu6886Registers registers{UNTOUCHED, UNTOUCHED, UNTOUCHED};
    const char* reason = toMpu6886Registers(settings, registers);
    TEST_ASSERT_NOT_NULL(reason);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(reason, field), reason);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.accelConfig);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.gyroConfig);
    TEST_ASSERT_EQUAL_HEX8(UNTOUCHED, registers.sampleRateDivider);
}

void test_mpu6886_refuses_what_it_cannot_run_exactly() {
    assertMpu6886Refuses({12, 2000, 100}, "accel range");
    assertMpu6886Refuses({8, 125, 100}, "gyro range");
    // 1 kHz / 400 is 2.5, not a whole divider: the chip would run at 333 or 500 Hz.
    assertMpu6886Refuses({8, 2000, 400}, "output rate");
    assertMpu6886Refuses({8, 2000, 800}, "output rate");
    assertMpu6886Refuses({8, 2000, 2000}, "output rate");
    // A whole divider, but over the register's 255.
    assertMpu6886Refuses({8, 2000, 2}, "output rate");
    assertMpu6886Refuses({8, 2000, 0}, "output rate");
}

void test_the_decode_scale_is_the_configured_range() {
    const ImuFullScale scale = fullScaleOf({16, 500, 800});
    TEST_ASSERT_EQUAL_FLOAT(16.0f, scale.accelG);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, scale.gyroDps);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bmi270_translates_every_supported_range_and_rate);
    RUN_TEST(test_bmi270_skeleton_settings_keep_the_skeletons_registers);
    RUN_TEST(test_bmi270_refuses_what_it_cannot_run_exactly);
    RUN_TEST(test_mpu6886_translates_every_supported_range_and_rate);
    RUN_TEST(test_mpu6886_skeleton_settings_keep_the_skeletons_registers);
    RUN_TEST(test_mpu6886_refuses_what_it_cannot_run_exactly);
    RUN_TEST(test_the_decode_scale_is_the_configured_range);
    return UNITY_END();
}
