#pragma once

#include <Arduino.h>

// MIDI clock sync state
extern volatile unsigned long clockTime;
extern volatile int clockCount;
extern float clockBpm;
extern volatile bool countTicks;

// MIDI clock sync handler
void handleMidiClock();
