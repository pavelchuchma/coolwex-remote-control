#ifndef D9F5614E_AB7E_4715_9792_44B8B371911D
#define D9F5614E_AB7E_4715_9792_44B8B371911D

#include <cstdint>
#include "keyboard.h"

/**
 * Result of one key sequence step: keep going, finished successfully, or failed.
 */
enum STEP_RESULT {
    srContinue,
    srDone,
    srFailed
};

extern Keyboard keyboard;

class KeyboardSequence {
    enum WAIT_RESULT {
        wrReached,
        wrWaiting,
        wrTimeout
    };

    // how long a step waits for the expected display mode (counted from step entry or last key up)
    static const uint16_t MODE_WAIT_TIMEOUT_MS = 1500;
    // how long a step waits for the blinking eHeat icon to be read
    static const uint16_t FLAG_WAIT_TIMEOUT_MS = 3000;
    static const uint8_t MAX_RECOVERY_PRESSES = 4;
    static const uint8_t MAX_INFO_PRESSES = 12;
    static const uint8_t MAX_SET_TEMP_PRESSES = 30;
    // bit i set = info screen MODE::infoT5U + i was seen (T5U, T5L, T3, T4, TP, Th)
    static const uint8_t ALL_INFO_SCREENS = 0b111111;

    Keyboard& keyboard;

    volatile KEY_SEQUENCE currentSequence = KEY_SEQUENCE::ksNone;
    volatile STEP_RESULT lastSequenceResult = STEP_RESULT::srFailed;
    uint16_t currentSequenceTargetValue = 0;
    uint8_t currentSequenceStep = 0;
    uint8_t lastLoggedStep = 0xFF;
    unsigned long displayReadsAfterKeyUp = 0;
    uint32_t waitStartedMillis = 0;
    uint8_t stepPressCount = 0;
    MODE modeAtLastPress = MODE::unknown;
    uint8_t infoScreensSeen = 0;

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
    void gotoStep(uint8_t step);
    void pressKey(KEYS key, uint16_t durationMs);
    void finishSequence(STEP_RESULT result);
    bool isWaitingForKeyEffect();
    WAIT_RESULT waitForDisplayMode(MODE expMode, uint16_t timeoutMs = MODE_WAIT_TIMEOUT_MS);
    STEP_RESULT waitForDisplayModeStep(MODE expMode);
    STEP_RESULT pressKeyInMode(MODE expMode, KEYS key, uint16_t durationMs);
    STEP_RESULT waitForEHeatFlag();
    STEP_RESULT ensureUnlocked();
    STEP_RESULT probePowerOn(uint8_t firstStep);
    STEP_RESULT commonPrologue();
    STEP_RESULT collectInfoScreens();
    STEP_RESULT keySequencePowerOn(bool targetPowerOnValue);
    STEP_RESULT keySequenceSetTargetTemp(int8_t targetTemp);
    STEP_RESULT keySequenceRefreshStatus();
    STEP_RESULT keySequenceTurnEHeatOn();
};

#endif /* D9F5614E_AB7E_4715_9792_44B8B371911D */
