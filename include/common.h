#ifndef ADA51EA7_5CDE_4F82_9082_4C0CF1EBAC39
#define ADA51EA7_5CDE_4F82_9082_4C0CF1EBAC39

#include <Arduino.h>
#include <ModbusIP_ESP8266.h>
#include "types.h"

#define PIN_DISPLAY_CS GPIO_NUM_5 // conn 4 via 10K
#define PIN_DISPLAY_CLK GPIO_NUM_18 // conn 5 via 10K
#define PIN_DISPLAY_DATA GPIO_NUM_23 // conn 3 via 10K

#define PIN_KEYBOARD_IN_ROW_1 GPIO_NUM_27 // conn 7
#define PIN_KEYBOARD_IN_ROW_2 GPIO_NUM_26 // conn 8
#define PIN_KEYBOARD_IN_ROW_3 GPIO_NUM_25 // conn 9

#define PIN_KEYBOARD_OUT_COL_1 GPIO_NUM_15 // conn 11 via 1K5
#define PIN_KEYBOARD_OUT_COL_2 GPIO_NUM_16 // conn 12 via 1K5
#define PIN_KEYBOARD_OUT_COL_3 GPIO_NUM_17 // conn 13 via 1K5

// At microsecond speeds, the functions from gpio.h are too heavy
#define GPIO_FAST_SET_1(gpio_num) GPIO.out_w1ts |= (0x1 << gpio_num)
#define GPIO_FAST_SET_0(gpio_num) GPIO.out_w1tc |= (0x1 << gpio_num)
#define GPIO_FAST_OUTPUT_ENABLE(gpio_num) GPIO.enable_w1ts |= (0x1 << gpio_num)
#define GPIO_FAST_OUTPUT_DISABLE(gpio_num) GPIO.enable_w1tc |= (0x1 << gpio_num)
#define GPIO_FAST_GET_LEVEL(gpio_num) ((GPIO.in >> gpio_num) & 0x1)

#define getCsValue() GPIO_FAST_GET_LEVEL(PIN_DISPLAY_CS) // orange
#define getClkValue() GPIO_FAST_GET_LEVEL(PIN_DISPLAY_CLK) // green
#define getDataValue() GPIO_FAST_GET_LEVEL(PIN_DISPLAY_DATA) // blue

#define DEBUG() Serial.printf(__FILE__ ":%d\n", __LINE__ )
#define DEBUG_STR(msg) Serial.printf(__FILE__ ":%d: " msg "\n", __LINE__ )
#define DEBUG_INT(i) Serial.printf(__FILE__ ":%d: %d\n", __LINE__, i)

extern uint8_t displayBuff[];
extern ModbusIP modbus;

void initializeModbus();
void decodeDisplayData();
void printData(uint8_t* data, uint8_t bitCount);
inline const char* boolAsOnOffStr(bool value) {
    return (value) ? "ON" : "OFF";
}

const char* enumToString(MODE value);

#define TEMP_ACCESSORS_IMPL(name, regType, regTypeLowercase) \
    int8_t get##name() { return (int8_t)(modbus.regType(regTypeLowercase##name) - 128);} \
    void set##name(int8_t value) { \
        if (get##name() != value) { Serial.printf("setting " #name ": %d -> %d\n", get##name(), value);} \
        modbus.regType(regTypeLowercase##name, value + 128); \
    }

#define TEMP_ACCESSORS_IREG(name) TEMP_ACCESSORS_IMPL(name, Ireg, ireg) 
#define TEMP_ACCESSORS_HREG(name) TEMP_ACCESSORS_IMPL(name, Hreg, hreg) 

#define FLAG_ACCESSORS(name) \
    void set##name(bool value) {\
        if (value != flag##name) {\
            Serial.printf("set" #name ": %d\n", (int)value);\
        }\
        flag##name = value;\
    }\
    bool is##name() { return flag##name; } \
    bool flag##name


class StateData {
    MODE mode = MODE::unknown;
    bool powerOnState = false;
    ModbusIP& modbus;
    uint32_t currentLoopMillis;
    uint32_t lastRefreshTime = 0;
    int8_t currentSetTempValue;
    int8_t tempTarget = INT8_MIN;

public:
    StateData(ModbusIP& modbus) : modbus(modbus) {
    }

    MODE getDisplayMode() {
        return (MODE)modbus.Ireg(iregDisplayMode);
    }

    void setDisplayMode(MODE mode) {
        if (mode != getDisplayMode()) {
            Serial.printf("setDisplayMode: %s\n", enumToString(mode));
        }
        modbus.Ireg(iregDisplayMode, mode);
    }

    void setCurrentSetTempValue(int8_t value) {
        currentSetTempValue = value;
    }

    int8_t getCurrentSetTempValue() {
        return currentSetTempValue;
    }

    void setTempTarget(int8_t value) {
        if (value != tempTarget) {
            Serial.printf("setTempTarget: %d\n", value);
        }
        tempTarget = value;
    }

    int8_t getTempTarget() {
        return tempTarget;
    }

    FLAG_ACCESSORS(PowerOn);
    FLAG_ACCESSORS(Hot);
    FLAG_ACCESSORS(EHeat);
    FLAG_ACCESSORS(Pump);
    FLAG_ACCESSORS(Vacation);

    TEMP_ACCESSORS_IREG(TempT5U);
    TEMP_ACCESSORS_IREG(TempT5L);
    TEMP_ACCESSORS_IREG(TempT3);
    TEMP_ACCESSORS_IREG(TempT4);
    TEMP_ACCESSORS_IREG(TempTP);
    TEMP_ACCESSORS_IREG(TempTh);

    void onLoopStart() {
        currentLoopMillis = (esp_timer_get_time() / 1000ULL);
    }

    uint32_t getNow() {
        return currentLoopMillis;
    }

    void onStatusUpdated() {
        lastRefreshTime = millis();
    }

    uint32_t millisSince(uint32_t sinceMillis) {
        if (currentLoopMillis >= sinceMillis) {
            return currentLoopMillis - sinceMillis;
        } else {
            return (UINT32_MAX - sinceMillis) + 1 + currentLoopMillis;
        }
    }

    uint16_t getStatusAgeSeconds() {
        if (lastRefreshTime == 0) {
            // not refresh yet
            return UINT16_MAX;
        }
        uint32_t diffSecs = millisSince(lastRefreshTime) / 1000;
        return (diffSecs >= UINT16_MAX) ? UINT16_MAX : diffSecs;
    }
};

#undef TEMP_ACCESSORS

extern StateData stateData;

#endif /* ADA51EA7_5CDE_4F82_9082_4C0CF1EBAC39 */
