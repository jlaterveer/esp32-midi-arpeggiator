#pragma once
#include <stdint.h>

// --- CONFIGURATION ---
// Pin assignments for MIDI, LED, encoder, and buttons
const uint8_t midiOutTxPin = 5;
const uint8_t midiInRxPin = 4;
const uint8_t ledBuiltIn = 21;
const uint8_t clearButtonPin = 10;
const uint8_t encoderCLK = 9;
const uint8_t encoderDT = 8;
const uint8_t encoderSW = 7;
const uint8_t encoder0PinA = encoderCLK;
const uint8_t encoder0PinB = encoderDT;

// --- PARAMETERS ---
// All arpeggiator parameters (defaults should be set in main.cpp, only constants here)
const int maxTimingHumanizePercent = 100;
const int maxNoteLengthRandomizePercent = 100;
const int minOctave = -3;
const int maxOctave = 3;
const int minTranspose = -3;
const int maxTranspose = 3;

