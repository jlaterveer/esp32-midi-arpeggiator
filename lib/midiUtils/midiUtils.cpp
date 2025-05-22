#include "midiUtils.h"
#include "Constants.h"

volatile unsigned long clockTime = 0;
volatile int clockCount = 0;
float clockBpm = 120.0;
volatile bool countTicks = false;

void handleMidiClock()
{
  // Only update clockTime and LED on first tick of cycle
  if (!countTicks)
  {
    clockTime = millis();
    countTicks = true;
    neopixelWrite(ledBuiltIn, 0, 64, 0); // Red blink
  }

  // Turn off LED after 6 ticks
  if (clockCount == 6)
    neopixelWrite(ledBuiltIn, 0, 0, 0);

  // Every 24 ticks (one quarter note), update BPM and reset
  if (clockCount >= 24)
  {
    countTicks = false;
    unsigned long interval = millis() - clockTime;
    if (interval > 0)
    {
      float newBpm = 60000.0f / interval;
      bpm = constrain((int)newBpm, 40, 240);
      arpInterval = 60000 / (bpm * notesPerBeat);
    }
    clockCount = 0;
  }
  else
  {
    clockCount++;
  }
}
