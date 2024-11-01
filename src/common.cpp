#include <Arduino.h>
#include "common.h"

#define CASE_ENTRY(e) case e: return #e

const char* enumToString(MODE value) {
    switch (value) {
        CASE_ENTRY(MODE::unknown);
        CASE_ENTRY(MODE::displayOff);
        CASE_ENTRY(MODE::locked);
        CASE_ENTRY(MODE::invalid);
        CASE_ENTRY(MODE::infoSU);
        CASE_ENTRY(MODE::infoSL);
        CASE_ENTRY(MODE::infoT3);
        CASE_ENTRY(MODE::infoT4);
        CASE_ENTRY(MODE::infoTP);
        CASE_ENTRY(MODE::infoTh);
        CASE_ENTRY(MODE::infoCE);
        CASE_ENTRY(MODE::infoER1);
        CASE_ENTRY(MODE::infoER2);
        CASE_ENTRY(MODE::infoER3);
        CASE_ENTRY(MODE::infoD7F);
        CASE_ENTRY(MODE::setClock);
        CASE_ENTRY(MODE::setTemp);
        CASE_ENTRY(MODE::unlocked);
        CASE_ENTRY(MODE::setVacation);
        CASE_ENTRY(MODE::vacation);
    default:
        return "???";
    }
}

const char* enumToString(KEYS value) {
    switch (value) {
        CASE_ENTRY(KEYS::keyEHeater);
        CASE_ENTRY(KEYS::keyVacation);
        CASE_ENTRY(KEYS::keyDisinfect);
        CASE_ENTRY(KEYS::keyEHeaterPlusDisinfect);
        CASE_ENTRY(KEYS::keyUpArrow);
        CASE_ENTRY(KEYS::keyEnter);
        CASE_ENTRY(KEYS::keyDownArrow);
        CASE_ENTRY(KEYS::keyClockTimer);
        CASE_ENTRY(KEYS::keyCancel);
        CASE_ENTRY(KEYS::keyOnOff);
    default:
        return "???";
    }
}
