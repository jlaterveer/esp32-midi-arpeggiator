#pragma once

#include <Arduino.h>

// MIDI clock sync state
extern volatile unsigned long clockTime;
extern volatile int clockCount;
extern float clockBpm;
extern volatile bool countTicks;

// --- MIDI parser state machine and variables ---
enum MidiState
{
    WaitingStatus,
    WaitingData1,
    WaitingData2
};

extern MidiState midiState;
extern uint8_t midiStatus;
extern uint8_t midiData1;


// MIDI byte parser
void readMidiByte(uint8_t byte);

// MIDI note handlers (must be visible to midiUtils)
void handleNoteOn(uint8_t note);
void handleNoteOff(uint8_t note);
void handleMidiCC(uint8_t cc, uint8_t value);

// MIDI clock sync handler
void handleMidiClock();