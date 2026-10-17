#include <Arduino.h>
#include "common.h"
#include "keySequences.h"

// ---- step helpers ----

void KeyboardSequence::gotoStep(uint8_t step) {
    currentSequenceStep = step;
    stepPressCount = 0;
    waitStartedMillis = stateData.getNow();
}

void KeyboardSequence::pressKey(KEYS key, uint16_t durationMs) {
    stepPressCount++;
    modeAtLastPress = stateData.getDisplayMode();
    keyboard.keyDown(key, durationMs);
}

// true while the display still shows the mode seen before the last key press and the wait has not timed out
bool KeyboardSequence::isWaitingForKeyEffect() {
    return stepPressCount > 0
        && stateData.getDisplayMode() == modeAtLastPress
        && stateData.millisSince(waitStartedMillis) < MODE_WAIT_TIMEOUT_MS;
}

KeyboardSequence::WAIT_RESULT KeyboardSequence::waitForDisplayMode(MODE expMode, uint16_t timeoutMs) {
    MODE currentMode = stateData.getDisplayMode();
    if (currentMode == expMode) {
        return wrReached;
    }
    if (stateData.millisSince(waitStartedMillis) < timeoutMs) {
        return wrWaiting;
    }
    Serial.printf("ERR: %s expected, but it is: %s\n", enumToString(expMode), enumToString(currentMode));
    return wrTimeout;
}

// srDone once the display shows expMode, srFailed on timeout
STEP_RESULT KeyboardSequence::waitForDisplayModeStep(MODE expMode) {
    switch (waitForDisplayMode(expMode)) {
    case wrReached:
        return srDone;
    case wrWaiting:
        return srContinue;
    default:
        return srFailed;
    }
}

// waits for expMode, then presses the key and moves to the next step
STEP_RESULT KeyboardSequence::pressKeyInMode(MODE expMode, KEYS key, uint16_t durationMs) {
    STEP_RESULT result = waitForDisplayModeStep(expMode);
    if (result == srDone) {
        gotoStep(currentSequenceStep + 1);
        pressKey(key, durationMs);
        return srContinue;
    }
    return result;
}

// eHeat icon blinks in sync with the time colon, decodeDisplayData() clears the request once it has read it
STEP_RESULT KeyboardSequence::waitForEHeatFlag() {
    if (!stateDataEHeatFlagUpdateRequested) {
        return srDone;
    }
    if (stateData.millisSince(waitStartedMillis) < FLAG_WAIT_TIMEOUT_MS) {
        return srContinue;
    }
    Serial.println("ERR: eHeat flag read timeout");
    return srFailed;
}

// Brings the display to unlocked from any recoverable screen, re-reading the mode after every press.
STEP_RESULT KeyboardSequence::ensureUnlocked() {
    MODE mode = stateData.getDisplayMode();
    if (mode == MODE::unlocked) {
        return srDone;
    }
    if (isWaitingForKeyEffect()) {
        return srContinue;
    }
    if (mode == MODE::unknown || mode == MODE::invalid) {
        // no key helps here, wait for a readable frame
        return waitForDisplayModeStep(MODE::unlocked);
    }
    if (stepPressCount >= MAX_RECOVERY_PRESSES) {
        Serial.printf("ERR: display still %s after %d key presses\n", enumToString(mode), (int)stepPressCount);
        return srFailed;
    }
    switch (mode) {
    case MODE::displayOff:
    case MODE::setTemp:
    case MODE::setClock:
    case MODE::setVacation:
        pressKey(KEYS::keyCancel, 100);
        return srContinue;
    case MODE::locked:
        pressKey(KEYS::keyEnter, 3200);
        return srContinue;
    case MODE::infoT5U:
    case MODE::infoT5L:
    case MODE::infoT3:
    case MODE::infoT4:
    case MODE::infoTP:
    case MODE::infoTh:
    case MODE::infoCE:
    case MODE::infoER1:
    case MODE::infoER2:
    case MODE::infoER3:
    case MODE::infoD7F:
        pressKey(KEYS::keyEHeaterPlusDisinfect, 1100);
        return srContinue;
    default:
        Serial.printf("ERR: no recovery from display mode %s\n", enumToString(mode));
        return srFailed;
    }
}

// Steps firstStep..firstStep+2: the setVacation screen opens only when power is on, so pressing Vacation
// reveals the power state. Ends with srDone on the unlocked screen.
STEP_RESULT KeyboardSequence::probePowerOn(uint8_t firstStep) {
    uint8_t step = currentSequenceStep - firstStep;
    switch (step) {
    case 0:
        return pressKeyInMode(MODE::unlocked, KEYS::keyVacation, 100);
    case 1: {
        bool isPowerOn = stateData.getDisplayMode() == MODE::setVacation;
        stateData.setPowerOn(isPowerOn);
        gotoStep(currentSequenceStep + 1);
        if (isPowerOn) {
            pressKey(KEYS::keyCancel, 100);
        }
        return srContinue;
    }
    case 2:
        return waitForDisplayModeStep(MODE::unlocked);
    default:
        Serial.println("ERR: unexpected probePowerOn step");
        return srFailed;
    }
}

// Steps 0..3 shared by all sequences: unlocked display with a known power state.
STEP_RESULT KeyboardSequence::commonPrologue() {
    if (currentSequenceStep == 0) {
        STEP_RESULT result = ensureUnlocked();
        if (result == srDone) {
            gotoStep(1);
            return srContinue;
        }
        return result;
    }
    return probePowerOn(1);
}

// Presses Down through the cyclic info view until all six temperature screens were shown.
// decodeDisplayData() stores each temperature as soon as its screen is displayed.
STEP_RESULT KeyboardSequence::collectInfoScreens() {
    MODE mode = stateData.getDisplayMode();
    if (mode >= MODE::infoT5U && mode <= MODE::infoTh) {
        infoScreensSeen |= 1 << (mode - MODE::infoT5U);
    }
    if (infoScreensSeen == ALL_INFO_SCREENS) {
        return srDone;
    }
    if (isWaitingForKeyEffect()) {
        return srContinue;
    }
    if (mode < MODE::infoT5U || mode > MODE::infoD7F) {
        Serial.printf("ERR: info view left unexpectedly, display mode: %s\n", enumToString(mode));
        return srFailed;
    }
    if (stepPressCount >= MAX_INFO_PRESSES) {
        Serial.printf("ERR: not all info screens seen after %d key presses (mask 0x%02X)\n", (int)stepPressCount, infoScreensSeen);
        return srFailed;
    }
    pressKey(KEYS::keyDownArrow, 100);
    return srContinue;
}

// ---- sequences (steps 0..3 are commonPrologue(), handled in onLoop()) ----

STEP_RESULT KeyboardSequence::keySequencePowerOn(bool targetPowerOnValue) {
    switch (currentSequenceStep) {
    case 4:
        if (stateData.isPowerOn() == targetPowerOnValue) {
            Serial.printf("No change, power is already: %s\n", boolAsOnOffStr(stateData.isPowerOn()));
            return srDone;
        }
        return pressKeyInMode(MODE::unlocked, KEYS::keyOnOff, 100);
    case 5:
    case 6:
    case 7: {
        STEP_RESULT result = probePowerOn(5);
        if (result != srDone) {
            return result;
        }
        if (stateData.isPowerOn() == targetPowerOnValue) {
            Serial.printf("INFO: power set %s\n", boolAsOnOffStr(targetPowerOnValue));
            return srDone;
        }
        Serial.printf("ERR: failed to set power %s\n", boolAsOnOffStr(targetPowerOnValue));
        return srFailed;
    }
    default:
        Serial.println("ERR: unexpected power sequence step");
        return srFailed;
    }
}

STEP_RESULT KeyboardSequence::keySequenceSetTargetTemp(int8_t targetTemp) {
    STEP_RESULT result;
    switch (currentSequenceStep) {
    case 4:
        return pressKeyInMode(MODE::unlocked, KEYS::keyUpArrow, 100);
    case 5: {
        result = waitForDisplayModeStep(MODE::setTemp);
        if (result != srDone) {
            return result;
        }
        int8_t currentValue = stateData.getCurrentSetTempValue();
        Serial.printf(" %d -> %d\n", currentValue, targetTemp);
        if (currentValue == targetTemp) {
            gotoStep(6);
            pressKey(KEYS::keyEnter, 100);
            return srContinue;
        }
        if (stepPressCount >= MAX_SET_TEMP_PRESSES) {
            Serial.printf("ERR: target temp not reached after %d key presses\n", (int)stepPressCount);
            return srFailed;
        }
        pressKey((currentValue < targetTemp) ? KEYS::keyUpArrow : KEYS::keyDownArrow, 100);
        return srContinue;
    }
    case 6:
        result = waitForDisplayModeStep(MODE::unlocked);
        if (result != srDone) {
            return result;
        }
        stateData.setTempTarget(targetTemp);
        Serial.printf("INFO: target temp set %d\n", (int)targetTemp);
        return srDone;
    default:
        Serial.println("ERR: unexpected target temp sequence step");
        return srFailed;
    }
}

STEP_RESULT KeyboardSequence::keySequenceRefreshStatus() {
    STEP_RESULT result;
    switch (currentSequenceStep) {
    case 4:
        result = waitForDisplayModeStep(MODE::unlocked);
        if (result != srDone) {
            return result;
        }
        stateDataEHeatFlagUpdateRequested = true;
        gotoStep(5);
        return srContinue;
    case 5:
        result = waitForEHeatFlag();
        if (result != srDone) {
            return result;
        }
        return pressKeyInMode(MODE::unlocked, KEYS::keyDownArrow, 100);
    case 6: {
        result = waitForDisplayModeStep(MODE::setTemp);
        if (result != srDone) {
            return result;
        }
        int8_t currentValue = stateData.getCurrentSetTempValue();
        if (currentValue == 38) {
            // 38 is the lowest value, the down arrow may or may not have decreased it; read it again via up arrow
            gotoStep(7);
        } else {
            stateData.setTempTarget(currentValue + 1);
            gotoStep(9);
        }
        pressKey(KEYS::keyCancel, 100);
        return srContinue;
    }
    case 7:
        return pressKeyInMode(MODE::unlocked, KEYS::keyUpArrow, 100);
    case 8:
        result = waitForDisplayModeStep(MODE::setTemp);
        if (result != srDone) {
            return result;
        }
        stateData.setTempTarget(stateData.getCurrentSetTempValue() - 1);
        gotoStep(9);
        pressKey(KEYS::keyCancel, 100);
        return srContinue;
    case 9:
        infoScreensSeen = 0;
        return pressKeyInMode(MODE::unlocked, KEYS::keyEHeaterPlusDisinfect, 1100);
    case 10:
        result = collectInfoScreens();
        if (result != srDone) {
            return result;
        }
        gotoStep(11);
        return srContinue;
    case 11:
        result = ensureUnlocked();
        if (result != srDone) {
            return result;
        }
        Serial.println("INFO: status read completed");
        stateData.onStatusUpdated();
        return srDone;
    default:
        Serial.println("ERR: unexpected status sequence step");
        return srFailed;
    }
}

STEP_RESULT KeyboardSequence::keySequenceTurnEHeatOn() {
    STEP_RESULT result;
    switch (currentSequenceStep) {
    case 4:
        return pressKeyInMode(MODE::unlocked, KEYS::keyEHeater, 100);
    case 5:
        return pressKeyInMode(MODE::unlocked, KEYS::keyEnter, 100);
    case 6:
        result = waitForDisplayModeStep(MODE::unlocked);
        if (result != srDone) {
            return result;
        }
        stateDataEHeatFlagUpdateRequested = true;
        gotoStep(7);
        return srContinue;
    case 7:
        result = waitForEHeatFlag();
        if (result != srDone) {
            return result;
        }
        if (stateData.isEHeat()) {
            Serial.println("INFO: TurnOnEHeat completed");
            return srDone;
        }
        Serial.println("ERR: keySequenceTurnOnEHeat failed");
        return srFailed;
    default:
        Serial.println("ERR: unexpected eHeat sequence step");
        return srFailed;
    }
}

// ---- driver ----

bool KeyboardSequence::onLoop() {
    if (keyboard.isKeyDown()) {
        displayReadsAfterKeyUp = 0;
        waitStartedMillis = stateData.getNow();
        return true;
    }
    if (currentSequence == KEY_SEQUENCE::ksNone) {
        return false;
    }
    if (displayReadsAfterKeyUp < 3) {
        // let the display catch up after key up
        if (stateData.millisSince(waitStartedMillis) < MODE_WAIT_TIMEOUT_MS) {
            return false;
        }
        Serial.println("ERR: no display frames received");
        finishSequence(srFailed);
        return false;
    }

    if (currentSequenceStep != lastLoggedStep) {
        lastLoggedStep = currentSequenceStep;
        Serial.printf("%s step %d, display: %s\n", enumToString(currentSequence), (int)currentSequenceStep,
            enumToString(stateData.getDisplayMode()));
    }

    STEP_RESULT result;
    if (currentSequenceStep <= 3) {
        result = commonPrologue();
        if (result == srDone) {
            gotoStep(4);
            result = srContinue;
        }
    } else {
        switch (currentSequence) {
        case KEY_SEQUENCE::ksRefreshStatus:
            result = keySequenceRefreshStatus();
            break;
        case KEY_SEQUENCE::ksPowerOn:
            result = keySequencePowerOn(currentSequenceTargetValue);
            break;
        case KEY_SEQUENCE::ksSetTargetTemp:
            result = keySequenceSetTargetTemp(currentSequenceTargetValue);
            break;
        case KEY_SEQUENCE::ksEHeatOn:
            result = keySequenceTurnEHeatOn();
            break;
        default:
            Serial.printf("ERR: Unexpected key sequence\n");
            result = srFailed;
        }
    }
    if (result != srContinue) {
        finishSequence(result);
    }
    return result == srContinue;
}

bool KeyboardSequence::startKeySequence(KEY_SEQUENCE sequence, uint16_t targetValue) {
    Serial.printf("startKeySequence(%s, val: %d)\n", enumToString(sequence), targetValue);
    if (currentSequence != KEY_SEQUENCE::ksNone) {
        Serial.printf("ERR: Another key sequence in progress\n");
        return false;
    }
    currentSequenceTargetValue = targetValue;
    infoScreensSeen = 0;
    lastLoggedStep = 0xFF;
    gotoStep(0);
    currentSequence = sequence;
    return true;
}

// Called from the Modbus task; blocks until onLoop() finishes the sequence or the timeout expires.
bool KeyboardSequence::processKeySequence(KEY_SEQUENCE sequence, uint16_t targetValue, uint16_t timeoutMs) {
    if (!startKeySequence(sequence, targetValue)) {
        return false;
    }
    uint32_t startTime = stateData.getNow();
    while (getCurrentSequence() == sequence) {
        delay(50);
        if (stateData.getNow() > startTime + timeoutMs) {
            Serial.printf("ERR: Sequence timeout!\n");
            cancelCurrentSequence();
            return false;
        }
    }
    return lastSequenceResult == srDone;
}

void KeyboardSequence::finishSequence(STEP_RESULT result) {
    if (result == srFailed) {
        stateData.setLastFailure(currentSequence, currentSequenceStep, stateData.getDisplayMode());
    }
    lastSequenceResult = result;
    currentSequenceStep = 0;
    currentSequence = KEY_SEQUENCE::ksNone;
}

void KeyboardSequence::cancelCurrentSequence() {
    finishSequence(srFailed);
}
