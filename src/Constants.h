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

int bpm = 96;                     // Beats per minute
int noteLengthPercent = 40;       // Note length as percent of interval
int noteVelocity = 127;           // MIDI velocity
int octaveRange = 0;              // Octave spread
int transpose = 0;                // Transpose in octaves
int velocityDynamicsPercent = 56; // Velocity randomization percent
bool timingHumanize = false;      // Enable timing humanization
int timingHumanizePercent = 4;    // Humanization percent
int noteLengthRandomizePercent = 20; // Note length randomization percent
int noteBalancePercent = 0; // Note bias percent
int randomChordPercent = 0; // Percentage of steps to replace with random 3-note chords
int noteRangeShift = 0;     // Range shift for lowest/highest note, -24..24 (or -127..127 if you want)
int noteRangeStretch = 0;   // Range stretch for lowest/highest note, -8..8