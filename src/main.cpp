#include <Arduino.h> // Arduino core library
#include <vector>    // STL vector for note storage
#include <USB.h>     // USB stack for ESP32
#include <USBMIDI.h> // USB MIDI library

// --- CONFIGURATION ---
// Pin assignments for MIDI, LED, encoder, and button
const uint8_t midiOutTxPin = 5;
const uint8_t midiInRxPin = 4;
const uint8_t ledBuiltIn = 21;
const uint8_t clearButtonPin = 2;

const uint8_t encoderCLK = 7;
const uint8_t encoderDT = 8;
const uint8_t encoderSW = 9;
const uint8_t encoder0PinA = encoderCLK;
const uint8_t encoder0PinB = encoderDT;

// --- ENCODER STATE MACHINE ---
// Rotary encoder state transition table and state variable
const unsigned char ttable[6][4] = {
    {0x3, 0x2, 0x1, 0x0}, {0x23, 0x0, 0x1, 0x0}, {0x13, 0x2, 0x0, 0x0}, {0x3, 0x5, 0x4, 0x0}, {0x3, 0x3, 0x4, 0x10}, {0x3, 0x5, 0x3, 0x20}};
volatile unsigned char state = 0;

// Initializes rotary encoder pins as inputs with pull-ups
void rotary_init()
{
  pinMode(encoder0PinA, INPUT_PULLUP);
  pinMode(encoder0PinB, INPUT_PULLUP);
}

// Reads rotary encoder state and returns movement code
unsigned char rotary_process()
{
  unsigned char pinstate = (digitalRead(encoder0PinA) << 1) | digitalRead(encoder0PinB);
  state = ttable[state & 0xf][pinstate];
  return (state & 0x30);
}

// --- ENCODER MODES ---
// Enum for all encoder parameter modes
enum EncoderMode
{
  MODE_BPM,
  MODE_LENGTH,
  MODE_VELOCITY,
  MODE_OCTAVE,
  MODE_PATTERN,
  MODE_RESOLUTION,
  MODE_REPEAT,
  MODE_TRANSPOSE,
  MODE_DYNAMICS,
  MODE_HUMANIZE,
  MODE_LENGTH_RANDOMIZE
};
EncoderMode encoderMode = MODE_BPM; // Current encoder mode
const int encoderModeSize = 11;     // Number of encoder modes

// --- PARAMETERS ---
// Main arpeggiator parameters and their defaults
int bpm = 96;                        // Beats per minute
int noteLengthPercent = 40;          // Note length as percent of interval
int noteVelocity = 100;              // MIDI velocity
int octaveRange = 2;                 // Octave spread
int transpose = 0;                   // Semitone transpose
int velocityDynamicsPercent = 56;    // Velocity randomization percent
bool timingHumanize = false;         // Enable/disable timing humanization
int timingHumanizePercent = 4;       // Humanize amount percent
int noteLengthRandomizePercent = 20; // Note length randomization percent

const int minOctave = -3, maxOctave = 3;       // Octave range limits
const int minTranspose = -3, maxTranspose = 3; // Transpose limits

// Note resolution options and state
const int notesPerBeatOptions[] = {1, 2, 3, 4, 6, 8, 12, 16};
const int notesPerBeatOptionsSize = sizeof(notesPerBeatOptions) / sizeof(notesPerBeatOptions[0]);
int notesPerBeatIndex = 3; // Default: 4 notes per beat
int notesPerBeat = notesPerBeatOptions[notesPerBeatIndex];
int noteRepeat = 2;                                       // Number of repeats per note
unsigned long arpInterval = 60000 / (bpm * notesPerBeat); // ms per note

// --- PATTERNS ---
// Enum for all arpeggiator patterns
enum ArpPattern
{
  UP,
  DOWN,
  TRIANGLE,
  SINE,
  SQUARE,
  RANDOM,
  PLAYED,
  CHORD
};
ArpPattern currentPattern = PLAYED; // Current pattern
const char *patternNames[] = {"UP", "DOWN", "TRIANGLE", "SINE", "SQUARE", "RANDOM", "PLAYED", "CHORD"};
bool ascending = true;                                                               // Direction for TRIANGLE pattern
const uint8_t sineTable[16] = {0, 1, 2, 4, 6, 8, 10, 12, 15, 12, 10, 8, 6, 4, 2, 1}; // Sine pattern lookup

// --- STATE ---
// Chord and note state variables
std::vector<uint8_t> currentChord; // Latched chord notes
std::vector<uint8_t> tempChord;    // Notes being captured
uint8_t leadNote = 0;              // First note of chord
bool capturingChord = false;       // Chord capture state
bool chordLatched = false;         // Chord latch state
size_t currentNoteIndex = 0;       // Index for arpeggio
bool noteOnActive = false;         // Note-on state
unsigned long noteOnStartTime = 0; // Note-on start time

// --- LED FLASH STATE ---
// LED feedback state
unsigned long ledFlashStart = 0;            // LED flash start time
bool ledFlashing = false;                   // LED flashing state
const unsigned long ledFlashDuration = 100; // LED flash duration (ms)

// --- MIDI I/O ---
// USB MIDI object
USBMIDI usbMIDI;

// Sends a single MIDI byte to both DIN and USB MIDI
void midiSendByte(uint8_t byte)
{
  Serial2.write(byte);
  usbMIDI.write(byte);
}

// Sends a MIDI Note On message to both DIN and USB MIDI
void sendNoteOn(uint8_t note, uint8_t velocity)
{
  midiSendByte(0x90);
  midiSendByte(note);
  midiSendByte(velocity);
  usbMIDI.noteOn(note, velocity, 1);
}

// Sends a MIDI Note Off message to both DIN and USB MIDI
void sendNoteOff(uint8_t note)
{
  midiSendByte(0x80);
  midiSendByte(note);
  midiSendByte(0);
  usbMIDI.noteOff(note, 0, 1);
}

// --- MIDI IN PARSING ---
// MIDI parser state machine for incoming bytes
enum MidiState
{
  WaitingStatus,
  WaitingData1,
  WaitingData2
};
MidiState midiState = WaitingStatus; // Current MIDI parser state
uint8_t midiStatus, midiData1;       // MIDI parser data bytes

// Handles incoming Note On events for chord capture
void handleNoteOn(uint8_t note)
{
  if (!capturingChord)
  {
    capturingChord = true;
    chordLatched = false;
    tempChord.clear();
    leadNote = note;
  }
  if (std::find(tempChord.begin(), tempChord.end(), note) == tempChord.end())
  {
    tempChord.push_back(note);
  }
}

// Handles incoming Note Off events for chord latch
void handleNoteOff(uint8_t note)
{
  if (capturingChord && note == leadNote)
  {
    currentChord = tempChord;
    capturingChord = false;
    chordLatched = true;
    currentNoteIndex = 0;
  }
}

// Reads and parses a single MIDI byte from DIN MIDI input
void readMidiByte(uint8_t byte)
{
  if (byte & 0x80) // Status byte
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
      midiState = WaitingData1;
      break;
    }
  }
}

// --- TIMING HUMANIZATION FUNCTION ---
// Returns a random offset (positive or negative) for note timing,
// based on timingHumanizePercent of the note length.
int getTimingHumanizeOffset(unsigned long noteLengthMs)
{
  int timingHumanizeAmount = (noteLengthMs * timingHumanizePercent) / 100;
  if (timingHumanizeAmount == 0)
    return 0;
  return random(-timingHumanizeAmount, timingHumanizeAmount + 1);
}

// --- NOTE LENGTH RANDOMIZATION FUNCTION ---
// Returns a randomized note length by shortening the original note length
// by up to noteLengthRandomizePercent percent.
unsigned long getRandomizedNoteLength(unsigned long noteLengthMs)
{
  unsigned long shortenAmount = (noteLengthMs * noteLengthRandomizePercent) / 100;
  if (shortenAmount == 0)
    return noteLengthMs;
  unsigned long randomShorten = random(0, shortenAmount + 1);
  return noteLengthMs - randomShorten;
}

// --- SETUP ---
// Initializes hardware, serial ports, MIDI, and sets up the initial state.
void setup()
{
  pinMode(ledBuiltIn, OUTPUT);           // Set built-in LED as output
  pinMode(clearButtonPin, INPUT_PULLUP); // Set clear button as input with pull-up
  pinMode(encoderSW, INPUT_PULLUP);      // Set encoder switch as input with pull-up
  rotary_init();                         // Initialize rotary encoder pins

  Serial.begin(115200);           // Start serial for debugging
  while (!Serial)
  {
    delay(10);
  }                               // Wait for serial to be ready
  Serial.println("Serial ready"); // Debug message

  Serial1.begin(31250, SERIAL_8N1, midiInRxPin, -1);  // MIDI IN (DIN)
  Serial2.begin(31250, SERIAL_8N1, -1, midiOutTxPin); // MIDI OUT (DIN)

  USB.begin();     // Initialize USB stack
  usbMIDI.begin(); // Initialize USB MIDI
}

// --- LOOP ---
// Main loop: handles button/encoder input, MIDI I/O, arpeggiator logic, and LED feedback.
void loop()
{
  unsigned long now = millis(); // Current time in ms

  // --- Handle clear button for chord reset and LED feedback ---
  static bool lastClear = HIGH;
  bool currentClear = digitalRead(clearButtonPin);
  if (lastClear == HIGH && currentClear == LOW)
  {
    currentChord.clear();
    chordLatched = false;
    currentNoteIndex = 0;
    neopixelWrite(ledBuiltIn, 0, 0, 127);
    ledFlashStart = millis();
    ledFlashing = true;
  }
  lastClear = currentClear;

  // --- Handle encoder switch for mode change and LED feedback ---
  static bool lastSW = HIGH;
  bool currentSW = digitalRead(encoderSW);
  if (lastSW == HIGH && currentSW == LOW)
  {
    encoderMode = static_cast<EncoderMode>((encoderMode + 1) % encoderModeSize);
    neopixelWrite(ledBuiltIn, 0, 0, 127);
    ledFlashStart = millis();
    ledFlashing = true;
  }
  lastSW = currentSW;

  // --- Handle rotary encoder for parameter adjustment ---
  unsigned char result = rotary_process();
  static int stepCounter = 0;
  int delta = 0;
  if (result == 0x10)
    stepCounter++;
  else if (result == 0x20)
    stepCounter--;
  if (abs(stepCounter) >= 2)
  {
    delta = (stepCounter > 0) ? 1 : -1;
    stepCounter = 0;
  }

  // --- Apply encoder delta to current parameter ---
  if (delta != 0)
  {
    switch (encoderMode)
    {
    case MODE_BPM:
      bpm = constrain(bpm + delta, 40, 240);
      break;
    case MODE_LENGTH:
      noteLengthPercent = constrain(noteLengthPercent + delta * 5, 5, 100);
      break;
    case MODE_VELOCITY:
      noteVelocity = constrain(noteVelocity + delta, 1, 127);
      break;
    case MODE_OCTAVE:
      octaveRange = constrain(octaveRange + delta, minOctave, maxOctave);
      break;
    case MODE_PATTERN:
      currentPattern = static_cast<ArpPattern>((currentPattern + delta + 8) % 8);
      break;
    case MODE_RESOLUTION:
      notesPerBeatIndex = constrain(notesPerBeatIndex + delta, 0, notesPerBeatOptionsSize - 1);
      notesPerBeat = notesPerBeatOptions[notesPerBeatIndex];
      break;
    case MODE_REPEAT:
      noteRepeat = constrain(noteRepeat + delta, 1, 4);
      break;
    case MODE_TRANSPOSE:
      transpose = constrain(transpose + delta, minTranspose, maxTranspose);
      break;
    case MODE_DYNAMICS:
      velocityDynamicsPercent = constrain(velocityDynamicsPercent + delta, 0, 100);
      break;
    case MODE_HUMANIZE:
      timingHumanizePercent = constrain(timingHumanizePercent + delta, 0, 100);
      timingHumanize = (timingHumanizePercent > 0);
      break;
    case MODE_LENGTH_RANDOMIZE:
      noteLengthRandomizePercent = constrain(noteLengthRandomizePercent + delta, 0, 100);
      break;
    }
    arpInterval = 60000 / (bpm * notesPerBeat);
  }

  // --- Handle incoming MIDI from DIN ---
  while (Serial1.available())
    readMidiByte(Serial1.read());

  // --- Handle incoming MIDI from USB ---
  midiEventPacket_t packet;
  while (usbMIDI.readPacket(&packet))
  {
    uint8_t cin = packet.header & 0x0F;
    switch (cin)
    {
    case 0x09:
      if (packet.byte3 > 0)
        handleNoteOn(packet.byte2);
      else
        handleNoteOff(packet.byte2);
      break;
    case 0x08:
      handleNoteOff(packet.byte2);
      break;
    }
  }

  // ...existing code for handling patterns and notes...
}