#ifndef D1041C14_9E57_40EF_85D0_1305BC6D1DDB
#define D1041C14_9E57_40EF_85D0_1305BC6D1DDB

/**
 * Represents invalid temperature.
 */
#define INVALID_TEMP -128

enum MODBUS_REGISTERS {
    /**
     * @brief Current mode of display, updated immediately. Value is on of MODE enum values.
     */
    iregDisplayMode = 100,
    /**
     * @brief Amount of seconds since last successful update of status. Status means all Input and Coil registers except iregDisplayMode.
     *
     * Value 65535 means unknow or older than 65535 seconds.
     */
    iregStatusAge = 101,
    /**
     * Bit mask of STATUS_FLAGS enum values.
     */
    iregStatusFlags = 102,
    /**
     * T5U: Storage tank temperature sensor (Upper)
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    iregTempT5U = 103,
    /**
     * T5L: Storage tank temperature sensor (Lower)
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    iregTempT5L = 104,
    /**
     * T3: Evaporator temperature sensor
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    iregTempT3 = 105,
    /**
     * T4: Ambient temperature sensor
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    iregTempT4 = 106,
    /**
     * TP: Discharge temperature sensor
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    iregTempTP = 107,
    /**
     * TH: Suction temperature sensor
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    iregTempTh = 108,


    /**
     * Any write to this register enforces status refresh of all other values to be get. Operation can take up to 9 seconds.
     */
    cregRefreshStatus = 200,

    /**
     * Gets and sets heatpump power ON or OFF. Operation can take up to 7 seconds.
     */
    cregPowerOn = 210,

    /**
     * Gets or sets target temperature. Acceptable target values are 38 - 60. (166 - 188 after increasing by 128)
     * All values representing temperatures are entered increased by 128 to allow for negative values to be transferred.
     */
    hregTempTarget = 300,

    /**
     * Writing value presses key for specified time. Value is: `(Key id from KEYS enum) << 8 + (press duration in ms) / 100`
     */
    hregPressKey = 310
};

enum MODE {
    unknown = 0,
    displayOff,
    locked,
    invalid,
    infoT5U,
    infoT5L,
    infoT3,
    infoT4,
    infoTP,
    infoTh,
    infoCE,
    infoER1,
    infoER2,
    infoER3,
    infoD7F,
    setClock,
    setTemp,
    unlocked,
    setVacation,
    vacation,
};

enum KEYS {
    // format: colum * 16 + row
    keyEHeater = 0x00,
    keyVacation = 0x01,
    keyDisinfect = 0x02,
    keyEHeaterPlusDisinfect = 0x03,
    keyUpArrow = 0x10,
    keyEnter = 0x11,
    keyDownArrow = 0x12,
    keyClockTimer = 0x20,
    keyCancel = 0x21,
    keyOnOff = 0x22
};

const char* enumToString(KEYS value);

enum STATUS_FLAGS {
    sfPowerOn = 1 << 0,
    sfHot = 1 << 1,
    sfEHeat = 1 << 2,
    sfPump = 1 << 3,
    sfVacation = 1 << 4
};

#endif /* D1041C14_9E57_40EF_85D0_1305BC6D1DDB */
