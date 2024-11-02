#include <Arduino.h>
#include <WiFi.h>
#include <ModbusIP_ESP8266.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/spi_slave.h>
#include <driver/gpio.h>
#include <ESPmDNS.h>

#include "common.h"
#include "keySequences.h"

#define WIFI_SSID "XXXX"
#define WIFI_PASSWORD "YYYY"


ModbusIP modbus;
StateData stateData(modbus);
uint32_t lastWiFiReconnectMillis = 0;
uint32_t lastTransactionStartedMillis = 0;

bool displayDataReady = false;
bool spiTransactionStared = false;

KeyStatus keyStatus;

void initializeSpiSlave();
bool handleDisplayDataReady();

uint8_t keyColumnPins[] = { PIN_KEYBOARD_OUT_COL_1, PIN_KEYBOARD_OUT_COL_2, PIN_KEYBOARD_OUT_COL_3 };

void IRAM_ATTR keyboadPulseInt() {
    if (!keyStatus.isHoldingKey() || GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) {
        // delayed interrupt processing, ignore
        return;
    }
    switch (keyStatus.inRow) {
    case 0:
        // row 1
        GPIO_FAST_SET_0(keyStatus.outPin);
        while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) NOP();
        GPIO_FAST_SET_1(keyStatus.outPin);
        break;
    case 1:
        // row 2
        while (GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_2)) NOP();
        GPIO_FAST_SET_0(keyStatus.outPin);
        while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_2)) NOP();
        GPIO_FAST_SET_1(keyStatus.outPin);
        break;
    case 2:
        // row 3
        while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3)) NOP();
        GPIO_FAST_SET_1(keyStatus.outPin);
        while (GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3)) NOP();
        GPIO_FAST_SET_0(keyStatus.outPin);
        break;
    case 3:
        while (!GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3) || !GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_1)) NOP();
        GPIO_FAST_SET_1(keyStatus.outPin);
        while (GPIO_FAST_GET_LEVEL(PIN_KEYBOARD_IN_ROW_3)) NOP();
        GPIO_FAST_SET_0(keyStatus.outPin);
        break;
    }
}

void initializeWiFi() {
    // WiFi.enableLongRange(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint16_t connectWait = 240;
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.printf(".");
        if (!connectWait--) {
            Serial.printf("Failed to connect, restarting!\n");
            ESP.restart();
        }
    }

    Serial.printf("\nWiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
    if (!MDNS.begin("boiler")) {   // Set the hostname to "esp32.local"
        Serial.println("Error setting up MDNS responder!");
    }
    Serial.println("mDNS responder started");
}

void setup() {
    pinMode(PIN_KEYBOARD_IN_ROW_1, INPUT);
    pinMode(PIN_KEYBOARD_IN_ROW_2, INPUT);
    pinMode(PIN_KEYBOARD_IN_ROW_3, INPUT);

    setKeyboardOutPinsAsInputs();

    Serial.begin(921600);
    Serial.println("\nStarted, connecting WiFi");

    initializeWiFi();

    initializeModbus();

    attachInterrupt(PIN_KEYBOARD_IN_ROW_1, keyboadPulseInt, FALLING);
    initializeSpiSlave();
}

spi_slave_transaction_t spiReadTransaction = {
    .length = 137,
    .rx_buffer = displayBuff,
};

void IRAM_ATTR displayDataReceived(spi_slave_transaction_t* t) {
    spiTransactionStared = false;
    displayDataReady = true;
}

void initializeSpiSlave() {
    pinMode(PIN_DISPLAY_CS, INPUT_PULLDOWN);
    pinMode(PIN_DISPLAY_CLK, INPUT_PULLDOWN);
    pinMode(PIN_DISPLAY_DATA, INPUT_PULLDOWN);

    spi_bus_config_t bcfg = {
        .mosi_io_num = PIN_DISPLAY_DATA,
        .miso_io_num = -1,
        .sclk_io_num = PIN_DISPLAY_CLK,
    };
    spi_slave_interface_config_t scfg = {
        .spics_io_num = PIN_DISPLAY_CS,
        .flags = 0,
        .queue_size = 1,
        .mode = 2,
        .post_trans_cb = displayDataReceived,
    };

    esp_err_t spi_state = spi_slave_initialize(VSPI_HOST, &bcfg, &scfg, SPI_DMA_DISABLED);
    if (spi_state != ESP_OK) {
        Serial.printf("SPI initialsation failed!\n");
    }
}

void verifyWiFiConnected() {
    if ((WiFi.status() != WL_CONNECTED) && (stateData.millisSince(lastWiFiReconnectMillis) >= 30000)) {
        Serial.printf("Reconnecting to WiFi...\n");
        WiFi.disconnect();
        WiFi.reconnect();
        lastWiFiReconnectMillis = stateData.getNow();
    }
}

void loop() {
    stateData.onLoopStart();
    if (processKeySequence()) {
        // key is down, no extra action
        return;
    }
    verifyWiFiConnected();
    modbus.task();

    // process data from display
    if (displayDataReady) {
        displayDataReady = false;
        if (!handleDisplayDataReady()) {
            return;
        }
        keyStatus.displayReadsAfterKeyUp++;
    }

    if (stateData.millisSince(lastTransactionStartedMillis) > 5000 && spiTransactionStared) {
        spiTransactionStared = false;
        Serial.printf("Read display SPI transaction time out!\n");
    }

    // start new read display transaction
    if (!spiTransactionStared && GPIO_FAST_GET_LEVEL(PIN_DISPLAY_CS)) {
        esp_err_t spi_state = spi_slave_queue_trans(VSPI_HOST, &spiReadTransaction, portMAX_DELAY);
        if (spi_state == ESP_OK) {
            spiTransactionStared = true;
            lastTransactionStartedMillis = stateData.getNow();
        } else {
            Serial.printf("SPI trans failed!\n");
        }
    }
}

bool handleDisplayDataReady() {
    spi_slave_transaction_t* trans;
    if (spi_slave_get_trans_result(VSPI_HOST, &trans, portMAX_DELAY) != ESP_OK) {
        Serial.printf("ERR: Failed to get transaction result\n");
        return false;
    }
    uint8_t* data = (uint8_t*)spiReadTransaction.rx_buffer;
    if (spiReadTransaction.trans_len == 137 && data[0] == 0b10100000) {
        // printData(data, 18 * 8);
        for (int i = 0; i < 16; i++) {
            data[i] = (data[i + 1] << 1);
            if (data[i + 2] & 0x80) {
                data[i] += 1;
            }
        }
        decodeDisplayData();
    } else {
        Serial.printf("SPI receive failed. Len=%d; header=%d\n", spiReadTransaction.trans_len, (int)data[0]);
        printData(data, 18 * 8);
        return false;
    }
    return true;
}

