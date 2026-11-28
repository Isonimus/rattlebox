#include "../../hal.h"
#include "../../nvs_storage.h"
#include "m5stickc_audio.h"
#include "m5stickc_display.h"
#include "m5stickc_input.h"
#include "m5stickc_power.h"
#include "mpu6886.h"

static M5StickCDisplay display;
static M5StickCInput   input;
static Mpu6886         imu;
static M5StickCPower   power;
static M5StickCAudio   audio;
static NvsStorage      storage;

static HardwareHAL hal {
    "M5StickC Plus2", display, input, imu, power, audio, storage
};

HardwareHAL& getHAL() {
    return hal;
}
