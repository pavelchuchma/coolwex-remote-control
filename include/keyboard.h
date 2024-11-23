#ifndef A6B70F5F_9D3C_42A3_B869_58E3B7CB029E
#define A6B70F5F_9D3C_42A3_B869_58E3B7CB029E

#include <cstdint>
#include "common.h"

class Keyboard {
public:
    Keyboard();

private:
    uint32_t keyDownAtMillis = 0;
    uint32_t keyDownDurationMillis = 0;

    uint8_t outPin = 0;
    uint8_t inRow = 0;

public:
    void inline onKeyboardInputRow1Low() {
        if (!isKeyDown() || GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) {
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

    void onLoop();
    void keyDown(KEYS key, uint16_t durationMs);
    bool isKeyDown() {
        return keyDownDurationMillis != 0;
    }
    void setKeyboardOutPinsAsInputs();

    uint32_t getKeyDownDurationMillis() {
        return keyDownDurationMillis;
    }
};

#endif /* A6B70F5F_9D3C_42A3_B869_58E3B7CB029E */
