#include <cstdint>
#ifndef D9F5614E_AB7E_4715_9792_44B8B371911D
#define D9F5614E_AB7E_4715_9792_44B8B371911D

enum KEY_SEQUENCE {
    ksNone = 0,
    ksRefreshStatus,
    ksPowerOn,
    ksSetTargetTemp
};

struct KeyStatus {
    uint32_t keyDownAtMillis = 0;
    uint32_t keyDownDurationMillis = 0;

    uint8_t outPin = 0;
    uint8_t inRow = 0;
    unsigned long displayReadsAfterKeyUp = 0;

    KEY_SEQUENCE currentSequence = KEY_SEQUENCE::ksNone;
    uint16_t currentSequenceTargetValue;
    uint8_t currentSequenceStep = 0;

    void holdKeyStart(uint32_t now, uint32_t durationMs) {
        // Serial.printf("holdKeyStart(now: %d, keyDownAtMillis: %d, duration: %d)\n", stateData.getNow(), now, durationMs);
        keyDownAtMillis = now;
        keyDownDurationMillis = durationMs;
    }
    void holdKeyStop() {
        // Serial.printf("holdKeyStop(now: %d, keyDownAtMillis: %d, keyDownDurationMillis: %d)\n", stateData.getNow(), keyDownAtMillis, keyDownDurationMillis);
        keyDownDurationMillis = 0;
    }
    bool isHoldingKey() {
        return keyDownDurationMillis != 0;
    }
};

extern KeyStatus keyStatus;

void startHoldKey(KEYS key, uint16_t durationMs);
bool handleHoldKey();
void setKeyboardOutPinsAsInputs();
bool processKeySequence();

void startKeySequence(KEY_SEQUENCE, uint16_t currentSequenceTargetValue);

#endif /* D9F5614E_AB7E_4715_9792_44B8B371911D */
