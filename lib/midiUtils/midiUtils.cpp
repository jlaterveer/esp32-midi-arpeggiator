#include "MidiUtils.h"
#include "Constants.h"
#include <USBMIDI.h>

volatile unsigned long clockTime = 0;
volatile int clockCount = 0;
float clockBpm = 120.0;
volatile bool countTicks = false;

MidiState midiState = WaitingStatus;
uint8_t midiStatus, midiData1;

// Handle incoming MIDI note on
void handleNoteOn(uint8_t note)
{
  // Start capturing a new chord if not already capturing
  if (!capturingChord)
  {
    capturingChord = true;
    tempChord.clear();
    leadNote = note;
  }
  // Add note to tempChord if not already present
  if (std::find(tempChord.begin(), tempChord.end(), note) == tempChord.end())
  {
    tempChord.push_back(note);
  }
}

// Handle incoming MIDI note off
void handleNoteOff(uint8_t note)
{
  // Latch chord when lead note is released
  if (capturingChord && note == leadNote)
  {
    currentChord = tempChord;
    capturingChord = false;
    currentNoteIndex = 0;
    noteRepeatCounter = 0;
  }
}

// Parse incoming MIDI bytes (hardware MIDI in)
void readMidiByte(uint8_t byte)
{
  if (byte == 0xF8)
  { // MIDI Clock
    handleMidiClock();
    return;
  }
  if (byte & 0x80)
  {
    midiStatus = byte;
    midiState = WaitingData1;
  }
  else
  {
    switch (midiState)
    {
    case WaitingData1:
      midiData1 = byte;
      midiState = WaitingData2;
      break;
    case WaitingData2:
      if ((midiStatus & 0xF0) == 0x90 && byte > 0)
        handleNoteOn(midiData1);
      else if ((midiStatus & 0xF0) == 0x80 || ((midiStatus & 0xF0) == 0x90 && byte == 0))
        handleNoteOff(midiData1);
      else if ((midiStatus & 0xF0) == 0xB0) // CC
        handleMidiCC(midiData1, byte);
      midiState = WaitingData1;
      break;
    }
  }
}

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
      // Use stepsPerBar instead of notesPerBeat for arpInterval calculation
      arpInterval = 60000 / (bpm * stepsPerBar);
    }
    clockCount = 0;
  }
  else
  {
    clockCount++;
  }
}


// Function to handle incoming USB MIDI packets
void processUsbMidiPackets(USBMIDI &usbMIDI)
{
  midiEventPacket_t packet;
  while (usbMIDI.readPacket(&packet))
  {
    uint8_t cin = packet.header & 0x0F;
    if (packet.byte1 == 0xF8)
    {
      handleMidiClock();
      continue;
    }
    switch (cin)
    {
    case 0x09: // Note On
      if (packet.byte3 > 0)
        handleNoteOn(packet.byte2);
      else
        handleNoteOff(packet.byte2);
      break;
    case 0x08: // Note Off
      handleNoteOff(packet.byte2);
      break;
    case 0x0B: // Control Change (CC)
      handleMidiCC(packet.byte2, packet.byte3);
      break;
    default:
      // No action for other CIN values
      break;
    }
  }
}

// Provide access to usbMIDI object from main.cpp
extern USBMIDI usbMIDI;
extern HardwareSerial Serial2;

// Send a single MIDI byte to hardware MIDI out
void midiSendByte(uint8_t byte)
{
  Serial2.write(byte);
}

// Send MIDI note on to both hardware and USB MIDI
void sendNoteOn(uint8_t note, uint8_t velocity)
{
  midiSendByte(0x90);
  midiSendByte(note);
  midiSendByte(velocity);
  usbMIDI.noteOn(note, velocity, 1); // Channel 1
}

// Send MIDI note off to both hardware and USB MIDI
void sendNoteOff(uint8_t note)
{
  midiSendByte(0x80);
  midiSendByte(note);
  midiSendByte(0);
  usbMIDI.noteOff(note, 0, 1); // Channel 1
}
