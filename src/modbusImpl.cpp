#include <Arduino.h>
#include "common.h"
#include "keySequences.h"

extern KeyboardSequence keyboardSequence;

uint16_t onSetRefreshStatusCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onSetRefreshStatusCallback(v:%d)\n", (int)value);

    if (keyboardSequence.processKeySequence(KEY_SEQUENCE::ksRefreshStatus, 0, 10000)
        && stateData.getStatusAgeSeconds() == 0)
    {
        return value;
    } else {
        Serial.printf("ERR: failed to refresh status.\n");
        return 0xFFFF;
    }
}

uint16_t onSetPowerOnCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onSetPowerOnCallback(v:%s)\n", boolAsOnOffStr(value));

    if (keyboardSequence.processKeySequence(KEY_SEQUENCE::ksPowerOn, value, 7000)
        && ((bool)value) == stateData.isPowerOn())
    {
        return value;
    } else {
        Serial.printf("ERR: failed to switch power %s\n", boolAsOnOffStr(value));
        return 0xFFFF;
    }
}

uint16_t onSetPressKeyCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onSetPressKeyCallback(v:%d)\n", (int)value);
    KEYS key = (KEYS)(value >> 8);
    uint16_t durationMs = (value & 0xFF) * 100;
    keyboard.keyDown(key, durationMs);

    // wait for keyUp
    uint32_t startTime = stateData.getNow();
    while (keyboard.isKeyDown()) {
        delay(50);
        if (stateData.getNow() > startTime + durationMs + 500) {
            Serial.printf("ERR: press key timeout!\n");
            return 0xFFFF;;
        }
    }
    return value;
}

uint16_t onSetTempTargetCallback(TRegister* reg, uint16_t value) {
    uint8_t targetTemp = value - 128;
    Serial.printf("onSetTempTargetCallback(v:%d)\n", (int)targetTemp);
    if (targetTemp < 38 || targetTemp > 60) {
        Serial.printf("ERR: target temp %d is out of range <38;60>\n", (int)targetTemp);
    } else {
        if (keyboardSequence.processKeySequence(KEY_SEQUENCE::ksSetTargetTemp, targetTemp, 13000)
            && stateData.getTempTarget() == targetTemp)
        {
            Serial.printf("Target temp successfully set to %d\n", targetTemp);
        } else {
            Serial.printf("ERR: Failed to set target temp to %d\n", targetTemp);
        }
    }
    return value;
}

uint16_t onGetTempTargetCallback(TRegister* reg, uint16_t value) {
    return stateData.getTempTarget() + 128;
}

uint16_t onGetStatusAgeCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onGetStatusAgeCallback(a:%d)\n", (int)reg->address.address);
    return stateData.getStatusAgeSeconds();
}

uint16_t onGetStatusFlagsCallback(TRegister* reg, uint16_t value) {
    uint16_t res = 0;
    if (stateData.isPowerOn()) res += STATUS_FLAGS::sfPowerOn;
    if (stateData.isHot()) res += STATUS_FLAGS::sfHot;
    if (stateData.isEHeat()) res += STATUS_FLAGS::sfEHeat;
    if (stateData.isPump()) res += STATUS_FLAGS::sfPump;
    if (stateData.isVacation()) res += STATUS_FLAGS::sfVacation;
    Serial.printf("onGetStatusFlagsCallback(a:%d, res:%d)\n", (int)reg->address.address, res);
    return res;
}

void initializeModbus() {
    modbus.server();
    modbus.addIreg(MODBUS_REGISTERS::iregDisplayMode, 0, MODBUS_REGISTERS::iregTempTh - MODBUS_REGISTERS::iregDisplayMode + 1);
    modbus.addHreg(MODBUS_REGISTERS::hregTempTarget, 0, 1);
    modbus.addHreg(MODBUS_REGISTERS::hregPressKey, 0, 1);
    modbus.addCoil(MODBUS_REGISTERS::cregRefreshStatus, false, 1);
    modbus.addCoil(MODBUS_REGISTERS::cregPowerOn, false, 1);
    modbus.onSetHreg(MODBUS_REGISTERS::hregPressKey, onSetPressKeyCallback, 1);
    modbus.onSetHreg(MODBUS_REGISTERS::hregTempTarget, onSetTempTargetCallback, 1);
    modbus.onGetHreg(MODBUS_REGISTERS::hregTempTarget, onGetTempTargetCallback, 1);
    modbus.onGetIreg(MODBUS_REGISTERS::iregStatusAge, onGetStatusAgeCallback, 1);
    modbus.onGetIreg(MODBUS_REGISTERS::iregStatusFlags, onGetStatusFlagsCallback, 1);
    modbus.onSetCoil(MODBUS_REGISTERS::cregRefreshStatus, onSetRefreshStatusCallback, 1);
    modbus.onSetCoil(MODBUS_REGISTERS::cregPowerOn, onSetPowerOnCallback, 1);
}
