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
    "Steps (4/4 bar)",
    "Bar Mode",
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
