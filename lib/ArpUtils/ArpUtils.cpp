#include "ArpUtils.h"
#include <Arduino.h>

const char *modeNames[] = {
    "BPM",
    "Note Length %",
    "Velocity",
    "Octave Range",
    "Pattern",
    "Pattern Playback Mode",
    "Pattern Reverse",
    "Pattern Smooth",
    //"Steps (4/4 bar)",
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
// Add more explicit instantiations if needed

// --- ENCODER STATE MACHINE ---
// Rotary encoder state table for quadrature decoding
const unsigned char ttable[6][4] = {
    {0x3, 0x2, 0x1, 0x0}, {0x23, 0x0, 0x1, 0x0}, {0x13, 0x2, 0x0, 0x0}, {0x3, 0x5, 0x4, 0x0}, {0x3, 0x3, 0x4, 0x10}, {0x3, 0x5, 0x3, 0x20}};
volatile unsigned char state = 0;
