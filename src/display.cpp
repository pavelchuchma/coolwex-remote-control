#include <Arduino.h>
#include "common.h"

WORD_ALIGNED_ATTR uint8_t displayBuff[32];
bool powerOnState;

void printData(uint8_t* data, uint8_t bitCount) {
    char buff[500];
    int w = 0;
    for (int i = 0; i < bitCount; i++) {
        if (!(i & 0x7)) {
            if (i) {
                buff[w++] = ' ';
            }
            buff[w++] = '0' + (i >> 3) / 10;
            buff[w++] = '0' + (i >> 3) % 10;
            buff[w++] = ':';
        }
        if ((i & 0x7) == 4) {
            buff[w++] = ' ';
        }
        buff[w++] = (bitRead(data[i >> 3], 7 - (i & 0x7))) ? '1' : '0';
    }
    buff[w++] = '\0';

    Serial.println(buff);
}

char decodeBcd(uint8_t c) {
    switch (c & 0b11111110) {
    case 0:
        return ' ';
    case 0b00000100:
        return '-';
    case 0b11111010:
        return '0';
    case 0b01100000:
        return '1';
    case 0b10111100:
        return '2';
    case 0b11110100:
        return '3';
    case 0b01100110:
        return '4';
    case 0b11010110:
        return '5';
    case 0b11011110:
        return '6';
    case 0b01110000:
        return '7';
    case 0b11111110:
        return '8';
    case 0b11110110:
        return '9';
    default:
        Serial.printf("ERR: decodeBcd: ");
        printData(&c, 8);
        return 'E';
    }
}

int8_t decodeTemp() {
    // digit 12.3
    uint8_t d1 = decodeBcd((displayBuff[3] << 4) + ((displayBuff[4] & 0b11100000) >> 4));
    uint8_t d10 = decodeBcd((displayBuff[4] << 4) + ((displayBuff[5] & 0b11100000) >> 4));

    int8_t res = 0;
    if (!isdigit(d1)) {
        return INVALID_TEMP;
    }
    res = (d1 - '0');
    if (isdigit(d10)) {
        res += (d10 - '0') * 10;
    } else if (d10 == ' ') {
        return res;
    } else if (d10 == '-') {
        return -res;
    } else {
        return INVALID_TEMP;
    }
    return res;
}

uint64_t lastDisplayValues[2] = { 0, 0 };

MODE decodeDisplayMode() {
    // if (((int64_t*)displayBuff)[0] != lastDisplayValues[0] || ((int64_t*)displayBuff)[1] != lastDisplayValues[1]) {
    //     lastDisplayValues[0] = ((int64_t*)displayBuff)[0];
    //     lastDisplayValues[1] = ((int64_t*)displayBuff)[1];
    //     printData(displayBuff, 128);
    // }

    if (((int64_t*)displayBuff)[0] == 0) return MODE::displayOff;
    if (displayBuff[13] & (1 << 7)) return MODE::setClock;
    if (displayBuff[15] & (1 << 0)) return MODE::locked;
    if (displayBuff[6] & (1 << 4)) return MODE::setTemp;
    if (displayBuff[7] & (1 << 4)) return MODE::setVacation;

    int32_t td = (*(int32_t*)(displayBuff + 10)) & 0b01110000111111101111111011111110;
    switch (td) {
    case 0b00000000100011101101011011101010: return infoSU;
    case 0b00000000100011101101011010001010: return infoSL;
    case 0b00000000000000001000111011110100: return infoT3;
    case 0b00000000000000001000111001100110: return infoT4;
    case 0b00000000000000001000111000111110: return infoTP;
    case 0b00000000000000001000111001001110: return infoTh;
    case 0b00000000000000001001101010011110: return infoCE;
    case 0b00000000011000000000000000000000: return infoER1;
    case 0b00000000101111000000000000000000: return infoER2;
    case 0b00000000111101000000000000000000: return infoER3;
    case 0b00000000111011000111000000011110: return infoD7F;
        // default:
        //     Serial.print("td:");
        //     printData((uint8_t*)&td, 32);
    }

    if (displayBuff[7] & (1 << 4)) return MODE::vacation;
    return MODE::unlocked;
}

void decodeDisplayData() {
    MODE mode = decodeDisplayMode();
    stateData.setDisplayMode(mode);

    switch (mode) {
    case MODE::displayOff:
        break;
    case MODE::unlocked:
    case MODE::locked:
        stateData.setHot(displayBuff[15] & (1 << 4));
        stateData.setEHeat(displayBuff[14] & (1 << 3));
        stateData.setPump(displayBuff[14] & (1 << 6));
        stateData.setVacation(displayBuff[14] & (1 << 4));
        stateData.setTempCurrent(decodeTemp());
        break;
    case MODE::setTemp:
        stateData.setTempTarget(decodeTemp());
        break;
    case MODE::infoSU:
        stateData.setTempSU(decodeTemp());
        break;
    case MODE::infoSL:
        stateData.setTempSL(decodeTemp());
        break;
    case MODE::infoT3:
        stateData.setTempT3(decodeTemp());
        break;
    case MODE::infoT4:
        stateData.setTempT4(decodeTemp());
        break;
    case MODE::infoTP:
        stateData.setTempTP(decodeTemp());
        break;
    case MODE::infoTh:
        stateData.setTempTh(decodeTemp());
        break;
    }
}
