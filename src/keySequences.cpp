#include <Arduino.h>
#include "common.h"
#include "keySequences.h"

void Keyboard::setKeyboardOutPinsAsInputs() {
    pinMode(PIN_KEYBOARD_OUT_COL_1, INPUT);
    pinMode(PIN_KEYBOARD_OUT_COL_2, INPUT);
    pinMode(PIN_KEYBOARD_OUT_COL_3, INPUT);
}

void Keyboard::pressKey(KEYS key, uint16_t durationMs) {
    uint8_t col = key >> 4;
    uint8_t row = key & 0x0F;
    Serial.printf("setKeyHoldStart(%s for %d ms)\n", enumToString(key), (int)durationMs);
    if (col > 2 || row > 3) {
        Serial.printf("ERR: Invalid key request: col=%d, row=%d\n", (int)col, (int)row);
        return;
    }
    if (isHoldingKey()) {
        Serial.printf("ERR: Still holding another key\n");
        return;
    }
    setKeyboardOutPinsAsInputs();

    outPin = keyColumnPins[col];
    inRow = row;
    pinMode(outPin, OUTPUT);
    GPIO_FAST_OUTPUT_ENABLE(outPin);

    holdKeyStart(stateData.getNow(), durationMs);
}

bool checkDisplayMode(MODE expMode) {
    MODE currentMode = stateData.getDisplayMode();
    if (currentMode != expMode) {
        Serial.printf("ERR: %s expected, but it is: %s\n", enumToString(expMode), enumToString(currentMode));
        return false;
    }
    return true;
}

bool Keyboard::checkDisplayModeAndDoNextStep(MODE expMode, KEYS key, uint16_t durationMs) {
    if (!checkDisplayMode(expMode)) {
        return false;
    }
    currentSequenceStep++;
    pressKey(key, durationMs);
    return true;
}

bool Keyboard::commonGetStateSteps0to3() {
    bool isSetVacationMode;
    switch (currentSequenceStep) {
    case 0:
        currentSequenceStep++;
        if (stateData.getDisplayMode() == MODE::displayOff) {
            // press cancel to turn display on
            pressKey(KEYS::keyCancel, 100);
            return true;
        }
    case 1:
        currentSequenceStep++;
        if (stateData.getDisplayMode() == MODE::locked) {
            pressKey(KEYS::keyEnter, 3200);
            return true;
        }
    case 2:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyVacation, 100);
    case 3:
        currentSequenceStep++;
        // setVacation mode is available only if power is on
        isSetVacationMode = stateData.getDisplayMode() == MODE::setVacation;
        stateData.setPowerOn(isSetVacationMode);

        if (isSetVacationMode) {
            pressKey(KEYS::keyCancel, 100);
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

bool Keyboard::keySequencePowerOn(bool targetPowerOnValue) {
    Serial.printf("keySequencePowerOn(s:%d, v:%s)\n", currentSequenceStep, boolAsOnOffStr(targetPowerOnValue));
    bool isSetVacationMode;
    switch (currentSequenceStep) {
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

        currentSequenceStep++;
        pressKey(KEYS::keyOnOff, 100);
        return true;
    case 5:
        return checkDisplayModeAndDoNextStep(MODE::unlocked, KEYS::keyVacation, 100);
    case 6:
        currentSequenceStep++;
        // vacation mode is available only if power is on
        isSetVacationMode = stateData.getDisplayMode() == MODE::setVacation;
        stateData.setPowerOn(isSetVacationMode);

        if (isSetVacationMode == targetPowerOnValue) {
            Serial.printf("INFO: power set %s\n", boolAsOnOffStr(isSetVacationMode));
        } else {
            Serial.printf("ERR: failed to set power %s\n", boolAsOnOffStr(targetPowerOnValue));
        }
        if (isSetVacationMode) {
            pressKey(KEYS::keyCancel, 100);
            return true;
        }
    case 7:
        return false;
    default:
        Serial.println("ERR: unexpected status sequence step");
        return false;
    }
}
bool Keyboard::keySequenceSetTargetTemp(int8_t targetTemp) {
    Serial.printf("keySequenceSetTargetTemp(s:%d, v:%d)\n", currentSequenceStep, targetTemp);
    switch (currentSequenceStep) {
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
            pressKey(KEYS::keyEnter, 100);
            currentSequenceStep++;
        } else {
            pressKey((stateData.getCurrentSetTempValue() < targetTemp) ? KEYS::keyUpArrow : KEYS::keyDownArrow, 100);
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

bool Keyboard::keySequenceRefreshStatus() {
    Serial.printf("keySequenceRefreshStatus(%d)\n", currentSequenceStep);
    switch (currentSequenceStep) {
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
                currentSequenceStep += 2;
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

bool Keyboard::handleHoldKey() {
    if (!isHoldingKey()) {
        return false;
    }

    if (stateData.millisSince(keyDownAtMillis) > keyDownDurationMillis) {
        // stop holding a key
        setKeyboardOutPinsAsInputs();
        holdKeyStop();
    }

    // clear display counter
    displayReadsAfterKeyUp = 0;
    // keep holding a key
    return true;
}

bool Keyboard::onLoop() {
    if (handleHoldKey()) {
        return true;
    }
    if (displayReadsAfterKeyUp < 3) {
        // let few loops pause after key up to refresh display
        return false;
    }

    bool callResult = false;
    switch (currentSequence) {
    case KEY_SEQUENCE::ksNone:
        return false;
    case KEY_SEQUENCE::ksRefreshStatus:
        callResult = keySequenceRefreshStatus();
        break;
    case KEY_SEQUENCE::ksPowerOn:
        callResult = keySequencePowerOn(currentSequenceTargetValue);
        break;
    case KEY_SEQUENCE::ksSetTargetTemp:
        callResult = keySequenceSetTargetTemp(currentSequenceTargetValue);
        break;
    default:
        Serial.printf("ERR: Unexpected key sequence\n");
    }
    if (!callResult) {
        // cancel current key sequence
        currentSequence = KEY_SEQUENCE::ksNone;
        currentSequenceStep = 0;
    }
    return callResult;
}

bool Keyboard::startKeySequence(KEY_SEQUENCE sequence, uint16_t currentSequenceTargetValue) {
    if (currentSequence != KEY_SEQUENCE::ksNone) {
        Serial.printf("ERR: Another key sequence in progress\n");
        return false;
    }
    currentSequence = sequence;
    currentSequenceTargetValue = currentSequenceTargetValue;
    currentSequenceStep = 0;
    return true;
}