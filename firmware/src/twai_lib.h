#ifndef TWAI_LIB_H
#define TWAI_LIB_H

#include <Arduino.h>
#include "config.h"
#include <driver/twai.h>

// Pins used to connect to CAN bus transceiver:
// Waveshare ESP32-S3-Touch-LCD-7 (the only supported board - see
// display_driver.h).
#define CAN_RX_PIN GPIO_NUM_19
#define CAN_TX_PIN GPIO_NUM_20

#define CAN_BAUDRATE    TWAI_TIMING_CONFIG_500KBITS()

#define CAN_FILTER_CODE 0x20000000U << 3
#define CAN_FILTER_MASK 0x007FFFFFU << 3

// CAN Interval:
#define POLLING_RATE_MS 20

// Dead-bus backoff (2026-10-21): how long the driver stays uninstalled
// after the bus is found dead before trying again. See getAlerts().
#define BUS_RETRY_MS    2000

#define TWAI_MODE       TWAI_MODE_NORMAL
#define TWAI_FILTER_ONE false

class ESP32S3_TWAI
{
public:
    // Initialize TWAI controller
    bool init(gpio_num_t txPin = (gpio_num_t)CAN_TX_PIN,
              gpio_num_t rxPin = (gpio_num_t)CAN_RX_PIN,
              twai_timing_config_t timing = CAN_BAUDRATE,
              twai_mode_t mode = TWAI_MODE,
              bool useFilter = false,
              uint32_t acceptanceCode = CAN_FILTER_CODE,
              uint32_t acceptanceMask = CAN_FILTER_MASK,
              bool singleFilter = TWAI_FILTER_ONE);

    // Configure alerts to detect TWAI controller
    bool alertConfigure(uint32_t alerts = TWAI_ALERT_RX_DATA);

    // Get detects alerts TWAI controller
    bool getAlerts();

    // Deinitialize TWAI controller
    void close();

    // Send a CAN frame
    bool send(uint32_t id, const uint8_t *data, uint8_t length, bool extended = false);

    // Check if a frame is available
    bool available();

    // Receive a CAN frame
    bool receive(uint32_t *id, uint8_t *data, uint8_t *length, bool *extended = nullptr);

    // Get TWAI status
    twai_status_info_t getStatus();

    // Driver installed and running - false while backed off on a dead bus
    // (see getAlerts()). Read-only, safe from any task.
    bool isInitialized() const { return _initialized; }

private:
    bool _initialized = false;
    uint32_t _alerts = TWAI_ALERT_RX_DATA;  // last alertConfigure() mask - restored after a recovery
    bool _waitingRetry = false;              // driver uninstalled because the bus is dead
    uint32_t _retryAtMs = 0;
};

#endif