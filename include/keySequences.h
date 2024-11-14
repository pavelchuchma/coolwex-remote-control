#include <cstdint>
#ifndef D9F5614E_AB7E_4715_9792_44B8B371911D
#define D9F5614E_AB7E_4715_9792_44B8B371911D

enum KEY_SEQUENCE {
    ksNone = 0,
    ksRefreshStatus,
    ksPowerOn,
    ksSetTargetTemp
};

class Keyboard {
public:
    Keyboard() {}

private:
    uint32_t keyDownAtMillis = 0;
    uint32_t keyDownDurationMillis = 0;

    uint8_t outPin = 0;
    uint8_t inRow = 0;
    unsigned long displayReadsAfterKeyUp = 0;

    KEY_SEQUENCE currentSequence = KEY_SEQUENCE::ksNone;
    uint16_t currentSequenceTargetValue;
    uint8_t currentSequenceStep = 0;

    void holdKeyStart(uint32_t now, uint32_t durationMs) {
        // Serial.printf("holdKeyStart(now: %d, keyDownAtMillis: %d, duration: %d)\n", stateData.getNow(), now, durationMs);
        keyDownAtMillis = now;
        keyDownDurationMillis = durationMs;
    }
    void holdKeyStop() {
        // Serial.printf("holdKeyStop(now: %d, keyDownAtMillis: %d, keyDownDurationMillis: %d)\n", stateData.getNow(), keyDownAtMillis, keyDownDurationMillis);
        keyDownDurationMillis = 0;
    }
    bool isHoldingKey() {
        return keyDownDurationMillis != 0;
    }

public:
    KEY_SEQUENCE getCurrentSequence() {
        return currentSequence;
    }

    void afterDisplayDataRead() {
        displayReadsAfterKeyUp++;
    }

    void inline onKeyboardInputRow1Low() {
        if (!isHoldingKey() || GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) {
            // delayed interrupt processing, ignore
            return;
        }
        switch (inRow) {
        case 0:
            // row 1
            GPIO_FAST_SET_0(outPin);
            while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) NOP();
            GPIO_FAST_SET_1(outPin);
            break;
        case 1:
            // row 2
            while (GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_2)) NOP();
            GPIO_FAST_SET_0(outPin);
            while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_2)) NOP();
            GPIO_FAST_SET_1(outPin);
            break;
        case 2:
            // row 3
            while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3)) NOP();
            GPIO_FAST_SET_1(outPin);
            while (GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3)) NOP();
            GPIO_FAST_SET_0(outPin);
            break;
        case 3:
            while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3) || !GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) NOP();
            GPIO_FAST_SET_1(outPin);
            while (GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3)) NOP();
            GPIO_FAST_SET_0(outPin);
            break;
        }
    }

    bool onLoop();
    void pressKey(KEYS key, uint16_t durationMs);
    void setKeyboardOutPinsAsInputs();
    bool startKeySequence(KEY_SEQUENCE sequence, uint16_t currentSequenceTargetValue);

private:
    bool handleHoldKey();
    bool checkDisplayModeAndDoNextStep(MODE expMode, KEYS key, uint16_t durationMs);
    bool commonGetStateSteps0to3();
    bool keySequencePowerOn(bool targetPowerOnValue);
    bool keySequenceSetTargetTemp(int8_t targetTemp);
    bool keySequenceRefreshStatus();
};

extern Keyboard keyboard;

#endif /* D9F5614E_AB7E_4715_9792_44B8B371911D */
