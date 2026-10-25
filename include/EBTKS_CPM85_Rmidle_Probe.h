#pragma once
#include <Arduino.h>

// C2a: EXPERIMENTAL HP-85 native LOADBIN console / RMIDLE probe.
// No printer, disk or CP/M forwarding here. Opt in explicitly via PIO:
//   build_flags = ... -DEBTKS_CPM85_RMIDLE_PROBE_ENABLE=1
//
// CPU/HP bus ISR: onControl, onKey only update volatile bytes.
// Ordinary Teensy main loop: onRmidle and service. Never DMA from ISR.
#ifndef EBTKS_CPM85_RMIDLE_PROBE_ENABLE
#define EBTKS_CPM85_RMIDLE_PROBE_ENABLE 0
#endif

namespace Cpm85RmidleProbe {
#if EBTKS_CPM85_RMIDLE_PROBE_ENABLE
void onControl(uint8_t value); // 2=menu attach; 0/1=menu off
bool onKey(uint8_t key);       // consumes menu keys; false if not active
void onRmidle();               // called only by existing AUXROM_RMIDLE
void service();                // call from normal EBTKS loop after AUXROM_Poll
#else
inline void onControl(uint8_t) {}
inline bool onKey(uint8_t) { return false; }
inline void onRmidle() {}
inline void service() {}
#endif
}
