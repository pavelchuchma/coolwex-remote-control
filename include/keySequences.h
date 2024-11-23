#ifndef D9F5614E_AB7E_4715_9792_44B8B371911D
#define D9F5614E_AB7E_4715_9792_44B8B371911D

#include <cstdint>
#include "keyboard.h"

enum KEY_SEQUENCE {
    ksNone = 0,
    ksRefreshStatus,
    ksPowerOn,
    ksSetTargetTemp
};

extern Keyboard keyboard;

class KeyboardSequence {
    Keyboard& keyboard;

    KEY_SEQUENCE currentSequence = KEY_SEQUENCE::ksNone;
    uint16_t currentSequenceTargetValue;
    uint8_t currentSequenceStep = 0;
    unsigned long displayReadsAfterKeyUp = 0;

public:
    KeyboardSequence(Keyboard& keyboard) : keyboard(keyboard) {}

    KEY_SEQUENCE getCurrentSequence() { return currentSequence; }

    bool onLoop();
    void afterDisplayDataRead() {
        displayReadsAfterKeyUp++;
    }

    bool startKeySequence(KEY_SEQUENCE sequence, uint16_t targetValue);
    bool processKeySequence(KEY_SEQUENCE sequence, uint16_t targetValue, uint16_t timeoutMs);
    void cancelCurrentSequence();

private:
    bool checkDisplayModeAndDoNextStep(MODE expMode, KEYS key, uint16_t durationMs);
    bool commonGetStateSteps0to3();
    bool keySequencePowerOn(bool targetPowerOnValue);
    bool keySequenceSetTargetTemp(int8_t targetTemp);
    bool keySequenceRefreshStatus();
};

#endif /* D9F5614E_AB7E_4715_9792_44B8B371911D */
