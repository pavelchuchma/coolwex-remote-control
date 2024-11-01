#include <Arduino.h>
#include "common.h"
#include "keySequences.h"

void setKeyboardOutPinsAsInputs() {
    pinMode(PIN_KEYBOARD_OUT_COL_1, INPUT);
    pinMode(PIN_KEYBOARD_OUT_COL_2, INPUT);
    pinMode(PIN_KEYBOARD_OUT_COL_3, INPUT);
}

void startHoldKey(KEYS key, uint16_t durationMs) {
    uint8_t col = key >> 4;
    uint8_t row = key & 0x0F;
    Serial.printf("setKeyHoldStart(%s for %d ms)\n", enumToString(key), (int)durationMs);
    if (col > 2 || row > 3) {
        Serial.printf("ERR: Invalid key request: col=%d, row=%d\n", (int)col, (int)row);
        return;
    }
    if (keyStatus.isHoldingKey()) {
        Serial.printf("ERR: Still holding another key\n");
        return;
    }
    setKeyboardOutPinsAsInputs();

    keyStatus.outPin = keyColumnPins[col];
    keyStatus.inRow = row;
    pinMode(keyStatus.outPin, OUTPUT);
    GPIO_FAST_OUTPUT_ENABLE(keyStatus.outPin);

    keyStatus.holdKeyStart(stateData.getNow(), durationMs);
}

inline void cancelCurrentSequence() {
    keyStatus.currentSequence = KEY_SEQUENCE::none;
    keyStatus.currentSequenceStep = 0;
}

bool checkDisplayMode(MODE expMode) {
    MODE currentMode = stateData.getDisplayMode();
    if (currentMode != expMode) {
        Serial.printf("ERR: %s expected, but it is: %s\n", enumToString(expMode), enumToString(currentMode));
        cancelCurrentSequence();
        return false;
    }
    return true;
}

bool checkDisplayModeAndDoNextStep(MODE expMode, KEYS key, uint16_t durationMs) {
    if (!checkDisplayMode(expMode)) {
        return false;
    }
    keyStatus.currentSequenceStep++;
    startHoldKey(key, durationMs);
    return true;
}

bool keySequenceRefreshStatus() {
    Serial.printf("keySequenceRefreshStatus(%d)\n", keyStatus.currentSequenceStep);
    bool isVacation;
    switch (keyStatus.currentSequenceStep) {
    case 0:
        keyStatus.currentSequenceStep++;
        if (stateData.getDisplayMode() == MODE::displayOff) {
            // press cancel to turn display on
            startHoldKey(KEYS::keyCancel, 100);
            return true;
        }
    case 1:
        keyStatus.currentSequenceStep++;
        if (stateData.getDisplayMode() == MODE::locked) {
            startHoldKey(KEYS::keyEnter, 3200);
            return true;
        }
    case 2:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyVacation, 100);
    case 3:
        keyStatus.currentSequenceStep++;
        // vacation mode is available only if power is on
        isVacation = stateData.getDisplayMode() == MODE::setVacation;
        decodeDisplayStatusFlags(isVacation);

        if (isVacation) {
            startHoldKey(KEYS::keyCancel, 100);
            return true;
        }
        if (!checkDisplayMode(MODE::unlocked)) {
            return false;
        }
    case 4:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyUpArrow, 100);
    case 5:
        return checkDisplayModeAndDoNextStep(MODE::setTemp, KEYS::keyCancel, 100);
    case 6:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyEHeaterPlusDisinfect, 1100);
    case 7:
        return checkDisplayModeAndDoNextStep(MODE::infoSU, KEYS::keyDownArrow, 100);
    case 8:
        return checkDisplayModeAndDoNextStep(MODE::infoSL, KEYS::keyDownArrow, 100);
    case 9:
        return checkDisplayModeAndDoNextStep(MODE::infoT3, KEYS::keyDownArrow, 100);
    case 10:
        return checkDisplayModeAndDoNextStep(MODE::infoT4, KEYS::keyDownArrow, 100);
    case 11:
        return checkDisplayModeAndDoNextStep(MODE::infoTP, KEYS::keyDownArrow, 100);
    case 12:
        return checkDisplayModeAndDoNextStep(MODE::infoTh, KEYS::keyEHeaterPlusDisinfect, 1100);
    case 13:
        cancelCurrentSequence();
        Serial.println("INFO: status read completed");
        stateData.onStatusUpdated();
        return true;
    default:
        Serial.println("ERR: unexpected status sequence step");
        return false;
    }
}

bool handleHoldKey() {
    if (!keyStatus.isHoldingKey()) {
        return false;
    }

    if (stateData.millisSince(keyStatus.keyDownAtMillis) > keyStatus.keyDownDurationMillis) {
        // stop holding a key
        setKeyboardOutPinsAsInputs();
        keyStatus.holdKeyStop();
    }

    // clear display counter
    keyStatus.displayReadsAfterKeyUp = 0;
    // keep holding a key
    return true;
}

bool processKeySequence() {
    if (handleHoldKey()) {
        return true;
    }
    if (keyStatus.displayReadsAfterKeyUp < 3) {
        // let few loops pause after key up to refresh display
        return false;
    }

    switch (keyStatus.currentSequence) {
    case KEY_SEQUENCE::none:
        return false;
    case KEY_SEQUENCE::refreshStatus:
        return keySequenceRefreshStatus();
    default:
        Serial.printf("ERR: Unexpected key sequence\n");
        cancelCurrentSequence();
        return false;
    }
}

void startKeySequence(KEY_SEQUENCE sequence) {
    if (keyStatus.currentSequence == KEY_SEQUENCE::none) {
        keyStatus.currentSequence = sequence;
        keyStatus.currentSequenceStep = 0;
    }
}