#pragma once
#include <stdint.h>
#include <vector>

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

// Note resolution options (notes per beat)
//const int notesPerBeatOptions[] = {1, 2, 3, 4, 6, 8, 12, 16};
//const int notesPerBeatOptionsSize = sizeof(notesPerBeatOptions) / sizeof(notesPerBeatOptions[0]);

// Steps per bar options for a 4-beat bar
const int stepsPerBarOptions[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16};
const int stepsPerBarOptionsSize = sizeof(stepsPerBarOptions) / sizeof(stepsPerBarOptions[0]);

// extern variables
extern int bpm;
extern unsigned long arpInterval;
//extern int notesPerBeat;
extern int stepsPerBar;

// --- Extern declarations for arpeggiator/chord state (needed by midiUtils.cpp) ---
extern bool capturingChord;
extern std::vector<uint8_t> tempChord;
extern uint8_t leadNote;
extern std::vector<uint8_t> currentChord;
extern size_t currentNoteIndex;
extern int noteRepeatCounter;

// --- ENCODER MODES ---
// List of all editable parameters for the encoder
enum EncoderMode
{
    MODE_BPM,
    MODE_LENGTH,
    MODE_VELOCITY,
    MODE_OCTAVE,
    MODE_PATTERN,
    MODE_PATTERN_PLAYBACK,
    MODE_REVERSE,
    MODE_SMOOTH, // Pattern smooth mode
    MODE_STEPS,  // Number of steps in a 4-beat bar
    MODE_BAR,   // Limit or repeat playingChord to match steps
    MODE_REPEAT,
    MODE_TRANSPOSE,
    MODE_DYNAMICS,
    MODE_HUMANIZE,
    MODE_LENGTH_RANDOMIZE,
    MODE_BALANCE,
    MODE_RANDOM_CHORD, // New mode: random steps replaced by 3-note chords
    MODE_RHYTHM,       // Rhythm accent pattern selection
    MODE_RANGE,        // Range shift for lowest/highest note
    MODE_STRETCH,
    MODE_COUNT // Stretch pattern up/down by adding notes
};
