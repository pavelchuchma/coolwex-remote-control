#ifndef ADA51EA7_5CDE_4F82_9082_4C0CF1EBAC39
#define ADA51EA7_5CDE_4F82_9082_4C0CF1EBAC39

#include <Arduino.h>
#include <ModbusIP_ESP8266.h>

#define PIN_DISPLAY_CS GPIO_NUM_5
#define PIN_DISPLAY_CLK GPIO_NUM_18
#define PIN_DISPLAY_DATA GPIO_NUM_23

#define PIN_KEYBOARD_IN_ROW_1 GPIO_NUM_27
#define PIN_KEYBOARD_IN_ROW_2 GPIO_NUM_26
#define PIN_KEYBOARD_IN_ROW_3 GPIO_NUM_25

#define PIN_KEYBOARD_OUT_COL_1 GPIO_NUM_15
#define PIN_KEYBOARD_OUT_COL_2 GPIO_NUM_16
#define PIN_KEYBOARD_OUT_COL_3 GPIO_NUM_17

extern uint8_t keyColumnPins[];

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

void decodeDisplayData();
void decodeDisplayStatusFlags(bool isPoweredOn);
void printData(uint8_t* data, uint8_t bitCount);

#define INVALID_TEMP -128

enum MODBUS_REGISTERS {
    iregDisplayMode = 100,
    iregStatusFlags = 101,
    iregStatusAge = 102,
    iregTempCurrent = 103,
    iregTempSU = 104,
    iregTempSL = 105,
    iregTempT3 = 106,
    iregTempT4 = 107,
    iregTempTP = 108,
    iregTempTh = 109,

    iregFirstRegister = iregDisplayMode,
    iregRegisterCount = iregTempTh - iregFirstRegister + 1,

    cregRefreshStatus = 200,
    cregPowerOn = 210,

    hregTempTarget = 300,
    hregPressKey = 310
};

enum MODE {
    unknown = 0,
    displayOff,
    locked,
    invalid,
    infoSU,
    infoSL,
    infoT3,
    infoT4,
    infoTP,
    infoTh,
    infoCE,
    infoER1,
    infoER2,
    infoER3,
    infoD7F,
    setClock,
    setTemp,
    unlocked,
    setVacation,
    vacation,
};

const char* enumToString(MODE value);

enum KEYS {
    // format: colum * 16 + row
    keyEHeater = 0x00,
    keyVacation = 0x01,
    keyDisinfect = 0x02,
    keyEHeaterPlusDisinfect = 0x03,
    keyUpArrow = 0x10,
    keyEnter = 0x11,
    keyDownArrow = 0x12,
    keyClockTimer = 0x20,
    keyCancel = 0x21,
    keyOnOff = 0x22
};

const char* enumToString(KEYS value);

#define STATUS_FLAG_ON  (1 << 0)
#define STATUS_FLAG_HOT (1 << 1)
#define STATUS_FLAG_EHEAT (1 << 2)
#define STATUS_FLAG_PUMP (1 << 3)
#define STATUS_FLAG_VACATION (1 << 4)

#define TEMP_ACCESSORS_IMPL(name, regType, regTypeLowercase) \
    int8_t get##name() { return (int8_t)(modbus.regType(regTypeLowercase##name) - 128);} \
    void set##name(int8_t value) { \
        if (get##name() != value) { Serial.printf("setting " #name ": %d -> %d\n", get##name(), value);} \
        modbus.regType(regTypeLowercase##name, value + 128); \
    }

#define TEMP_ACCESSORS_IREG(name) TEMP_ACCESSORS_IMPL(name, Ireg, ireg) 
#define TEMP_ACCESSORS_HREG(name) TEMP_ACCESSORS_IMPL(name, Hreg, hreg) 

class StateData {
    MODE mode = MODE::unknown;
    bool powerOnState = false;
    ModbusIP& modbus;
    uint32_t currentLoopMillis;
    uint32_t lastRefreshTime = 0;

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

    uint8_t getStatusFlags() {
        return modbus.Ireg(iregStatusFlags);
    }

    void setStatusFlags(uint8_t statusFlags) {
        uint8_t oldValue = getStatusFlags();
        if (statusFlags != oldValue) {
            Serial.printf("setStatusFlags: ");
            if (statusFlags & STATUS_FLAG_ON) Serial.print("on, ");
            if (statusFlags & STATUS_FLAG_HOT) Serial.print("hot, ");
            if (statusFlags & STATUS_FLAG_EHEAT) Serial.print("eheat, ");
            if (statusFlags & STATUS_FLAG_PUMP) Serial.print("pump, ");
            if (statusFlags & STATUS_FLAG_VACATION) Serial.print("vacation, ");
            Serial.println();
        }
        modbus.Ireg(iregStatusFlags, statusFlags);
    }
    
    bool isPowerOn() {
        return getStatusFlags() & STATUS_FLAG_ON;
    }

    TEMP_ACCESSORS_IREG(TempCurrent);
    TEMP_ACCESSORS_HREG(TempTarget);
    TEMP_ACCESSORS_IREG(TempSU);
    TEMP_ACCESSORS_IREG(TempSL);
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
