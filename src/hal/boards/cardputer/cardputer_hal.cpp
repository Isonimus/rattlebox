#include "../../hal.h"
#include "bmi270.h"
#include "cardputer_display.h"
#include "cardputer_input.h"
#include "cardputer_power.h"

static CardputerDisplay display;
static CardputerInput   input;
static Bmi270           imu;
static CardputerPower   power;

static HardwareHAL hal {
    "Cardputer ADV", display, input, imu, power
};

HardwareHAL& getHAL() {
    return hal;
}
