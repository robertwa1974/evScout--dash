#pragma once

#include <Arduino.h>

// Call this for every standard-ID CAN frame received that ISN'T the SDO
// response ID. Looks up every broadcast param matching this frame's ID
// in kCanBroadcastParams, decodes it, and applies it to myData directly.
//
// This intentionally decodes ALL matching fields per frame in one pass -
// e.g. for canId 0x356 that means udc, idc, AND tmpm all get decoded
// together, which sidesteps the known bug in the original ZombieVerterDial
// CANData.cpp where only tmpm was wired up despite params.json declaring
// all three on that frame.
void decodeBroadcastFrame(uint32_t canId, const uint8_t *data, uint8_t length);
