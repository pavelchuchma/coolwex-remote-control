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
    bool isSetVacationMode;
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
        // setVacation mode is available only if power is on
        isSetVacationMode = stateData.getDisplayMode() == MODE::setVacation;
        stateData.setPowerOn(isSetVacationMode);

        if (isSetVacationMode) {
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

inline const char* boolAsOnOffStr(bool value) {
    return (value) ? "ON" : "OFF";
}

bool keySequencePowerOn(bool targetPowerOnValue) {
    Serial.printf("keySequencePowerOn(s:%d, v:%s)\n", keyStatus.currentSequenceStep, boolAsOnOffStr(targetPowerOnValue));
    bool isSetVacationMode;
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
        if (stateData.isPowerOn() == targetPowerOnValue) {
            Serial.printf("No change, power is already: %s\n", boolAsOnOffStr(stateData.isPowerOn()));
            return false;
        }

        keyStatus.currentSequenceStep++;
        startHoldKey(KEYS::keyOnOff, 100);
        return true;
    case 5:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyVacation, 100);
    case 6:
        keyStatus.currentSequenceStep++;
        // vacation mode is available only if power is on
        isSetVacationMode = stateData.getDisplayMode() == MODE::setVacation;
        stateData.setPowerOn(isSetVacationMode);

        if (isSetVacationMode == targetPowerOnValue) {
            Serial.printf("INFO: power set %s\n", boolAsOnOffStr(isSetVacationMode));
        } else {
            Serial.printf("ERR: failed to set power %s\n", boolAsOnOffStr(targetPowerOnValue));
        }
        if (isSetVacationMode) {
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
bool keySequenceSetTargetTemp(int8_t targetTemp) {
    Serial.printf("keySequenceSetTargetTemp(s:%d, v:%d)\n", keyStatus.currentSequenceStep, targetTemp);
    switch (keyStatus.currentSequenceStep) {
    case 0:
    case 1:
    case 2:
    case 3:
        return commonGetStateSteps0to3();
    case 4:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyUpArrow, 100);
    case 5:
        if (!checkDisplayMode(MODE::setTemp)) {
            return false;
        }

        Serial.printf(" %d -> %d\n", stateData.getCurrentSetTempValue(), targetTemp);
        if (stateData.getCurrentSetTempValue() == targetTemp) {
            startHoldKey(KEYS::keyEnter, 100);
            keyStatus.currentSequenceStep++;
        } else {
            startHoldKey((stateData.getCurrentSetTempValue() < targetTemp) ? KEYS::keyUpArrow : KEYS::keyDownArrow, 100);
        }
        return true;
    case 6:
        if (!checkDisplayMode(MODE::unlocked)) {
            return false;
        }
        stateData.setTempTarget(targetTemp);
        Serial.printf("INFO: target temp set %d\n", (int)targetTemp);
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
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyDownArrow, 100);
    case 5:
        if (checkDisplayModeAndDoNextStep(MODE::setTemp, KEYS::keyCancel, 100)) {
            if (stateData.getCurrentSetTempValue() == 38) {
                // 38 is the lowest possible value. Not sure if the real target value is 38 or 39 decreased by down arrow
                // try to get value via up arrow in next 2 steps
            } else {
                stateData.setTempTarget(stateData.getCurrentSetTempValue() + 1);
                // skip next 2 steps
                keyStatus.currentSequenceStep += 2;
            }
            return true;
        }
        return false;
    case 6:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyUpArrow, 100);
    case 7:
        if (checkDisplayModeAndDoNextStep(MODE::setTemp, KEYS::keyCancel, 100)) {
            stateData.setTempTarget(stateData.getCurrentSetTempValue() - 1);
            return true;
        }
        return false;
    case 8:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyEHeaterPlusDisinfect, 1100);
    case 9:
        return checkDisplayModeAndDoNextStep(MODE::infoSU, KEYS::keyDownArrow, 100);
    case 10:
        return checkDisplayModeAndDoNextStep(MODE::infoSL, KEYS::keyDownArrow, 100);
    case 11:
        return checkDisplayModeAndDoNextStep(MODE::infoT3, KEYS::keyDownArrow, 100);
    case 12:
        return checkDisplayModeAndDoNextStep(MODE::infoT4, KEYS::keyDownArrow, 100);
    case 13:
        return checkDisplayModeAndDoNextStep(MODE::infoTP, KEYS::keyDownArrow, 100);
    case 14:
        return checkDisplayModeAndDoNextStep(MODE::infoTh, KEYS::keyEHeaterPlusDisinfect, 1100);
    case 15:
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
        callResult = keySequencePowerOn(keyStatus.currentSequenceTargetValue);
        break;
    case KEY_SEQUENCE::ksSetTargetTemp:
        callResult = keySequenceSetTargetTemp(keyStatus.currentSequenceTargetValue);
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
        return;
    }
    keyStatus.currentSequence = sequence;
    keyStatus.currentSequenceTargetValue = currentSequenceTargetValue;
    keyStatus.currentSequenceStep = 0;
}