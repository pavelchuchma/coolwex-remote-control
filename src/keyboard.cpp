#include "keyboard.h"

void Keyboard::setKeyboardOutPinsAsInputs() {
    pinMode(PIN_KEYBOARD_OUT_COL_1, INPUT);
    pinMode(PIN_KEYBOARD_OUT_COL_2, INPUT);
    pinMode(PIN_KEYBOARD_OUT_COL_3, INPUT);
}
uint8_t keyColumnPins[] = { PIN_KEYBOARD_OUT_COL_1, PIN_KEYBOARD_OUT_COL_2, PIN_KEYBOARD_OUT_COL_3 };

Keyboard::Keyboard() {
    Serial.printf("Keyboard init: %ld\n", keyDownDurationMillis);
}

void Keyboard::keyDown(KEYS key, uint16_t durationMs) {
    uint8_t col = key >> 4;
    uint8_t row = key & 0x0F;
    Serial.printf("keyDown(%s for %d ms at %ld)\n", enumToString(key), (int)durationMs, stateData.getNow());
    if (col > 2 || row > 3) {
        Serial.printf("ERR: Invalid key request: col=%d, row=%d\n", (int)col, (int)row);
        return;
    }
    if (isKeyDown()) {
        Serial.printf("ERR: Still holding another key\n");
        return;
    }
    setKeyboardOutPinsAsInputs();

    outPin = keyColumnPins[col];
    inRow = row;
    pinMode(outPin, OUTPUT);
    GPIO_FAST_OUTPUT_ENABLE(outPin);

    keyDownAtMillis = stateData.getNow();
    keyDownDurationMillis = durationMs;
}

void Keyboard::onLoop() {
    // Serial.printf("Keyboard::onLoop(now: %d, keyDownAtMillis: %d, keyDownDurationMillis: %d)\n", stateData.getNow(), keyDownAtMillis, keyDownDurationMillis);
    if (!isKeyDown()) {
        // Serial.printf("no key down\n");
        return;
    }

    if (stateData.millisSince(keyDownAtMillis) > keyDownDurationMillis) {
        // stop holding a key
        setKeyboardOutPinsAsInputs();

        Serial.printf("keyUp(now: %d, keyDownAtMillis: %d, keyDownDurationMillis: %d)\n", stateData.getNow(), keyDownAtMillis, keyDownDurationMillis);
        keyDownDurationMillis = 0;
    }
}