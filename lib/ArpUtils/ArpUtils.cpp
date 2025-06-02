#include "ArpUtils.h"
#include <Arduino.h>
#include "Constants.h"

const char *modeNames[] = {
    "BPM",
    "Note Length %",
    "Velocity",
    "Octave Range",
    "Pattern",
    "Pattern Playback Mode",
    "Pattern Reverse",
    "Pattern Smooth",
    "Bar Mode",
    "Numerator",
    "Denominator",
    "Note Repeat",
    "Transpose",
    "Velocity Dynamics Percent",
    "Timing Humanize Percent",
    "Note Length Randomize Percent",
    "Note Balance Percent",
    "Random Chord Percent",
    "Rhythm Pattern",
    "Range Shift",
    "Range Stretch"};

// Helper to convert MeterType enum to a human-readable string
const char *meterTypes[] = {
    "Simple Meter",
    "Compound Meter",
    "Irregular Meter"};

// Helper to convert PatternLoop enum to a human-readable string
const char *patternLoopOptions[] = {
    "Straight",
    "Loop"};

// Helper to convert PatternReverse enum to a human-readable string
const char *patternReverseOptions[] = {
    "Forward",
    "Reverse"};

// Helper to convert PatternSmooth enum to a human-readable string
const char *patternSmoothOptions[] = {
    "Smooth",
    "Raw"};

template <typename T>
void printIfChanged(const char *label, T &lastValue, T currentValue, T printValue)
{
    if (currentValue != lastValue)
    {
        Serial.print(label);
        Serial.println(printValue);
        lastValue = currentValue;
    }
}

// Explicit template instantiations for common types
template void printIfChanged<int>(const char *, int &, int, int);
template void printIfChanged<bool>(const char *, bool &, bool, bool);

// --- ENCODER STATE MACHINE ---
// Rotary encoder state table for quadrature decoding
const unsigned char ttable[6][4] = {
    {0x3, 0x2, 0x1, 0x0}, {0x23, 0x0, 0x1, 0x0}, {0x13, 0x2, 0x0, 0x0}, {0x3, 0x5, 0x4, 0x0}, {0x3, 0x3, 0x4, 0x10}, {0x3, 0x5, 0x3, 0x20}};
volatile unsigned char state = 0;
