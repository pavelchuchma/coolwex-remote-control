#include <Arduino.h>
#include "common.h"
#include "keySequences.h"

uint16_t onSetRefreshStatusCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onSetRefreshStatusCallback(a:%d, v:%d)\n", (int)reg->address.address, (int)value);
    startKeySequence(KEY_SEQUENCE::ksRefreshStatus, 0);
    return value;
}

uint16_t onSetPowerOnCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onSetPowerOnCallback(a:%d, v:%d)\n", (int)reg->address.address, (int)value);
    startKeySequence(KEY_SEQUENCE::ksPowerOn, value);
    return value;
}

uint16_t onSetHregCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onSetHregCallback(a:%d, v:%d)\n", (int)reg->address.address, (int)value);
    KEYS key = (KEYS)(value >> 8);
    uint16_t duration = (value & 0xFF) * 100;
    startHoldKey(key, duration);
    return value;
}

uint16_t onGetStatusAgeCallback(TRegister* reg, uint16_t value) {
    Serial.printf("onGetStatusAgeCallback(a:%d)\n", (int)reg->address.address);
    return stateData.getStatusAgeSeconds();
}

uint16_t onGetStatusFlagsCallback(TRegister* reg, uint16_t value) {
    uint16_t res = 0;
    if (stateData.isPowerOn()) res += STATUS_FLAG_ON;
    if (stateData.isHot()) res += STATUS_FLAG_HOT;
    if (stateData.isEHeat()) res += STATUS_FLAG_EHEAT;
    if (stateData.isPump()) res += STATUS_FLAG_PUMP;
    if (stateData.isVacation()) res += STATUS_FLAG_VACATION;
    Serial.printf("onGetStatusFlagsCallback(a:%d, res:%d)\n", (int)reg->address.address, res);
    return res;
}

void initializeModbus() {
    modbus.server();
    modbus.addIreg(MODBUS_REGISTERS::iregFirstRegister, 0, MODBUS_REGISTERS::iregRegisterCount);
    modbus.addHreg(MODBUS_REGISTERS::hregTempTarget, 0, 1);
    modbus.addHreg(MODBUS_REGISTERS::hregPressKey, 0, 1);
    modbus.addCoil(MODBUS_REGISTERS::cregRefreshStatus, false, 1);
    modbus.addCoil(MODBUS_REGISTERS::cregPowerOn, false, 1);
    modbus.onSetHreg(MODBUS_REGISTERS::hregPressKey, onSetHregCallback, 1);
    modbus.onGetIreg(MODBUS_REGISTERS::iregStatusAge, onGetStatusAgeCallback, 1);
    modbus.onGetIreg(MODBUS_REGISTERS::iregStatusFlags, onGetStatusFlagsCallback, 1);
    modbus.onSetCoil(MODBUS_REGISTERS::cregRefreshStatus, onSetRefreshStatusCallback, 1);
    modbus.onSetCoil(MODBUS_REGISTERS::cregPowerOn, onSetPowerOnCallback, 1);
}
