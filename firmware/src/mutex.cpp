#include "mutex.h"
#include <Arduino.h>  // Serial - only actually compiled under DEBUG below; nothing previously defined
                       // plain DEBUG (only CAN_TRACE) for any environment that builds this file, so
                       // this missing include was a real, latent, never-before-compiled bug


SemaphoreHandle_t dataMutex = xSemaphoreCreateMutex();
SemaphoreHandle_t uiMutex = xSemaphoreCreateMutex();

void mutex_init() {
  if (dataMutex == NULL || uiMutex == NULL) {
#ifdef DEBUG
    Serial.println("Failed Mutex init");
#endif
    while (1)
      ;
  }
}