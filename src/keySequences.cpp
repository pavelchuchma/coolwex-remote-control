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

bool checkDisplayMode(MODE expMode) {
    MODE currentMode = stateData.getDisplayMode();
    if (currentMode != expMode) {
        Serial.printf("ERR: %s expected, but it is: %s\n", enumToString(expMode), enumToString(currentMode));
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

bool commonGetStateSteps0to3() {
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
            // unexpected state
            return false;
        }
        return true;
    default:
        Serial.println("ERR: unexpected commonGetStateSteps position");
        return false;
    }
}


bool keySequencePowerOn() {
    Serial.printf("keySequencePowerOn(%d)\n", keyStatus.currentSequenceStep);
    bool isVacation;
    switch (keyStatus.currentSequenceStep) {
    case 0:
    case 1:
    case 2:
    case 3:
        return commonGetStateSteps0to3();
    case 4:
        if (!checkDisplayMode(MODE::unlocked)) {
            return false;
        }
        if (keyStatus.currentSequenceTargetValue == stateData.isPowerOn()) {
            Serial.printf("No change, power is already: %d\n", stateData.isPowerOn());
            return false;
        }

        keyStatus.currentSequenceStep++;
        startHoldKey(KEYS::keyOnOff, 200);
        return true;
    case 5:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyVacation, 100);
    case 6:
        keyStatus.currentSequenceStep++;
        // vacation mode is available only if power is on
        isVacation = stateData.getDisplayMode() == MODE::setVacation;

        if (isVacation == (bool)keyStatus.currentSequenceTargetValue) {
            Serial.printf("INFO: power set %s\n", (isVacation) ? "ON" : "OFF");
        } else {
            Serial.printf("ERR: failed to set power %s\n", (keyStatus.currentSequenceTargetValue) ? "ON" : "OFF");
        }
        if (isVacation) {
            startHoldKey(KEYS::keyCancel, 100);
            return true;
        }
    case 7:
        return false;
    default:
        Serial.println("ERR: unexpected status sequence step");
        return false;
    }
}

bool keySequenceRefreshStatus() {
    Serial.printf("keySequenceRefreshStatus(%d)\n", keyStatus.currentSequenceStep);
    switch (keyStatus.currentSequenceStep) {
    case 0:
    case 1:
    case 2:
    case 3:
        return commonGetStateSteps0to3();
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
        Serial.println("INFO: status read completed");
        stateData.onStatusUpdated();
        return false;
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

    bool callResult = false;
    switch (keyStatus.currentSequence) {
    case KEY_SEQUENCE::ksNone:
        return false;
    case KEY_SEQUENCE::ksRefreshStatus:
        callResult = keySequenceRefreshStatus();
        break;
    case KEY_SEQUENCE::ksPowerOn:
        callResult = keySequencePowerOn();
        break;
    default:
        Serial.printf("ERR: Unexpected key sequence\n");
    }
    if (!callResult) {
        // cancel current key sequence
        keyStatus.currentSequence = KEY_SEQUENCE::ksNone;
        keyStatus.currentSequenceStep = 0;
    }
    return callResult;
}

void startKeySequence(KEY_SEQUENCE sequence, uint16_t currentSequenceTargetValue) {
    if (keyStatus.currentSequence != KEY_SEQUENCE::ksNone) {
        Serial.printf("ERR: Another key sequence in progress\n");
    }
    keyStatus.currentSequence = sequence;
    keyStatus.currentSequenceTargetValue = currentSequenceTargetValue;
    keyStatus.currentSequenceStep = 0;
}