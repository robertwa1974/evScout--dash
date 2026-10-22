#include "twai_lib.h"

bool ESP32S3_TWAI::init(gpio_num_t txPin, gpio_num_t rxPin,
                        twai_timing_config_t timing, twai_mode_t mode,
                        bool useFilter, uint32_t acceptanceCode, uint32_t acceptanceMask, bool singleFilter)
{
  if (_initialized)
  {
    close();
  }

  // Configure TWAI driver
  twai_general_config_t g_config =
      // TWAI_GENERAL_CONFIG_DEFAULT(txPin, rxPin, mode);
      {
          .controller_id = 0,
          .mode = mode,
          .tx_io = txPin,
          .rx_io = rxPin,
          .clkout_io = TWAI_IO_UNUSED,
          .bus_off_io = TWAI_IO_UNUSED,
          .tx_queue_len = 5,
          .rx_queue_len = 64,
          .alerts_enabled = TWAI_ALERT_NONE,
          .clkout_divider = 0};

  twai_filter_config_t f_config;
  if (useFilter)
  {
    f_config = {
        .acceptance_code = acceptanceCode,
        .acceptance_mask = acceptanceMask,
        .single_filter = singleFilter};
  }
  else
  {
    f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  };

  // Install and start TWAI driver
  esp_err_t err = twai_driver_install(&g_config, &timing, &f_config);
  if (err != ESP_OK)
  {
#ifdef DEBUG
    Serial.println("Failed to install TWAI driver");
#endif
    return false;
  }

  err = twai_start();
  if (err != ESP_OK)
  {
#ifdef DEBUG
    Serial.println("Failed to start TWAI controller");
#endif
    twai_driver_uninstall();
    return false;
  }

  _initialized = true;
  return true;
}

bool ESP32S3_TWAI::alertConfigure(uint32_t alerts)
{
  // Reconfigure alerts to detect frame receive, Bus-Off error and RX queue full states
  // TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
  _alerts = alerts;
  if (twai_reconfigure_alerts(alerts, NULL) != ESP_OK)
  {
#ifdef DEBUG
    Serial.println("Failed to configure alerts");
#endif
    return false;
  }
  return true;
}

// Dead-bus handling (rewritten 2026-10-21). The original uaDASH code
// uninstalled + reinstalled the driver on EVERY bus error and then enabled
// TWAI_ALERT_ALL. With no bus attached (bench, or VCU powered off) every SDO
// poll goes unacknowledged, so that ran ~20x/second forever on core 0 -
// alongside the WiFi stack - while the controller's automatic retransmits
// raised an error interrupt per attempt. Suspected cause of phones failing
// to join the config AP on the bench (not proven).
//
// Now: an isolated bus error is left to the controller (real buses see the
// odd bit error; resetting for one would drop CAN for nothing). Only a
// genuinely dead bus - bus-off, or stuck error-passive (TX error count >=
// 128, i.e. nobody is ACKing) - uninstalls the driver, which stops the
// retransmit storm, and it's retried every BUS_RETRY_MS with the caller's
// own alert mask. send() already refuses while uninstalled.
bool ESP32S3_TWAI::getAlerts()
{
  if (!_initialized)
  {
    if (_waitingRetry && (int32_t)(millis() - _retryAtMs) >= 0)
    {
      if (init())
      {
        alertConfigure(_alerts);
        _waitingRetry = false;
      }
      else
      {
        _retryAtMs = millis() + BUS_RETRY_MS;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(POLLING_RATE_MS));
    return false;
  }

  uint32_t alerts_triggered = 0;
  twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(POLLING_RATE_MS));
  // Check if message is received
  if (alerts_triggered & TWAI_ALERT_RX_DATA)
  {
    return true;
  }

  bool dead = (alerts_triggered & TWAI_ALERT_BUS_OFF) != 0;
  if (!dead && (alerts_triggered & TWAI_ALERT_BUS_ERROR))
  {
    twai_status_info_t st;
    dead = twai_get_status_info(&st) == ESP_OK &&
           (st.state != TWAI_STATE_RUNNING || st.tx_error_counter >= 128);
  }
  if (dead)
  {
#ifdef DEBUG
    Serial.println("ESP32S3_TWAI: bus dead (no ACK / bus-off) - driver off, retrying in 2s");
#endif
    close();
    _waitingRetry = true;
    _retryAtMs = millis() + BUS_RETRY_MS;
  }
  return false;
}

void ESP32S3_TWAI::close()
{
  if (_initialized)
  {
    twai_stop();
    twai_driver_uninstall();
    _initialized = false;
  }
}

bool ESP32S3_TWAI::send(uint32_t id, const uint8_t *data, uint8_t length, bool extended)
{
  if (!_initialized || length > 8)
    return false;

  twai_message_t message;
  message.identifier = id;
  message.extd = extended;
  message.rtr = false;
  message.ss = false;
  message.self = false;
  message.dlc_non_comp = false;
  message.data_length_code = length;
  memcpy(message.data, data, length);

  esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(POLLING_RATE_MS));
  return (err == ESP_OK);
}

bool ESP32S3_TWAI::available()
{
  if (!_initialized)
    return false;

  twai_status_info_t status;
  twai_get_status_info(&status);
  return (status.msgs_to_rx > 0);
}

bool ESP32S3_TWAI::receive(uint32_t *id, uint8_t *data, uint8_t *length, bool *extended)
{
  if (!_initialized)
    return false;

  twai_message_t message;
  esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(POLLING_RATE_MS));

  if (err != ESP_OK)
  {
    return false;
  }

  *id = message.identifier;
  *length = message.data_length_code;
  memcpy(data, message.data, *length);

  if (extended != nullptr)
  {
    *extended = message.extd;
  }

  return true;
}

twai_status_info_t ESP32S3_TWAI::getStatus()
{
  twai_status_info_t status;
  if (_initialized)
  {
    twai_get_status_info(&status);
  }
  return status;
}