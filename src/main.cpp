#include <Arduino.h>
#include <vector>
#include <EEPROM.h>
#include <USB.h>
#include <USBMIDI.h>

// --- STEP PATTERN CONSTANTS (for step_patterns.h) ---
#define MIN_STEPS 2
#define MAX_STEPS 4
// --- EEPROM CONFIGURATION ---
#define EEPROM_SIZE 4096 // Make sure this is large enough for all patterns

#include "step_patterns.h" // <-- Must be included after the defines

// --- CONFIGURATION ---
// Pin assignments
const uint8_t midiOutTxPin = 5;
const uint8_t midiInRxPin = 4;
const uint8_t ledBuiltIn = 21;
const uint8_t clearButtonPin = 10;
const uint8_t encoderCLK = 9;
const uint8_t encoderDT = 8;
const uint8_t encoderSW = 7;
const uint8_t encoder0PinA = encoderCLK;
const uint8_t encoder0PinB = encoderDT;

// --- ENCODER STATE MACHINE ---
// Rotary encoder state table for quadrature decoding
const unsigned char ttable[6][4] = {
    {0x3, 0x2, 0x1, 0x0}, {0x23, 0x0, 0x1, 0x0}, {0x13, 0x2, 0x0, 0x0}, {0x3, 0x5, 0x4, 0x0}, {0x3, 0x3, 0x4, 0x10}, {0x3, 0x5, 0x3, 0x20}};
volatile unsigned char state = 0;

// Initialize rotary encoder pins
void rotary_init()
{
  pinMode(encoder0PinA, INPUT_PULLUP);
  pinMode(encoder0PinB, INPUT_PULLUP);
}

// Process rotary encoder state and return direction
unsigned char rotary_process()
{
  unsigned char pinstate = (digitalRead(encoder0PinA) << 1) | digitalRead(encoder0PinB);
  state = ttable[state & 0xf][pinstate];
  return (state & 0x30);
}

// --- ENCODER MODES ---
// List of all editable parameters
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
  MODE_LENGTH_RANDOMIZE,
  MODE_BALANCE
};
EncoderMode encoderMode = MODE_BPM;
const int encoderModeSize = 12;

// --- PARAMETERS ---
// All arpeggiator parameters
int bpm = 96;                     // Beats per minute
int noteLengthPercent = 40;       // Note length as percent of interval
int noteVelocity = 127;           // MIDI velocity
int octaveRange = 2;              // Octave spread
int transpose = 0;                // Transpose in octaves
int velocityDynamicsPercent = 56; // Velocity randomization percent
bool timingHumanize = false;      // Enable timing humanization
int timingHumanizePercent = 4;    // Humanization percent
const int maxTimingHumanizePercent = 100;
int noteLengthRandomizePercent = 20; // Note length randomization percent
const int maxNoteLengthRandomizePercent = 100;
int noteBalancePercent = 0; // Note bias percent

const int minOctave = -3, maxOctave = 3;
const int minTranspose = -3, maxTranspose = 3;

// Note resolution options (notes per beat)
const int notesPerBeatOptions[] = {1, 2, 3, 4, 6, 8, 12, 16};
const int notesPerBeatOptionsSize = sizeof(notesPerBeatOptions) / sizeof(notesPerBeatOptions[0]);
int notesPerBeatIndex = 3; // Default: 4 notes per beat
int notesPerBeat = notesPerBeatOptions[notesPerBeatIndex];
int noteRepeat = 2; // Number of repeats per note
int noteRepeatCounter = 0;
unsigned long arpInterval = 60000 / (bpm * notesPerBeat); // ms per note

// --- PATTERNS ---
// Arpeggiator patterns
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
ArpPattern currentPattern = PLAYED;
const char *patternNames[] = {"UP", "DOWN", "TRIANGLE", "SINE", "SQUARE", "RANDOM", "PLAYED", "CHORD"};
bool ascending = true; // For TRIANGLE pattern
const uint8_t sineTable[16] = {0, 1, 2, 4, 6, 8, 10, 12, 15, 12, 10, 8, 6, 4, 2, 1};

// --- STATE ---
// Chord and note state
std::vector<uint8_t> currentChord;
std::vector<uint8_t> tempChord;
uint8_t leadNote = 0;
bool capturingChord = false;
bool chordLatched = false;
size_t currentNoteIndex = 0;
bool noteOnActive = false;
unsigned long noteOnStartTime = 0;
uint8_t lastPlayedNote = 0;

// tempChord is the first array where new played notes are stored as they are received (via handleNoteOn).
// When the chord is latched (handleNoteOff for the lead note), tempChord is copied to currentChord.
// So, currentChord is the next-level array that holds the latched chord notes for arpeggiation.

// Later in the loop, you will see:
//   std::vector<uint8_t> baseChord = capturingChord ? tempChord : currentChord;
//   std::vector<uint8_t> playedChord; // deduplicated, order-preserving
//   std::vector<uint8_t> orderedChord; // sorted version of playedChord
//   std::vector<uint8_t> playingChord; // final notes to be played, with octave shifts and pattern applied

// So the progression is:
// tempChord (collecting notes) -> currentChord (latched chord) -> baseChord -> playedChord/orderedChord -> playingChord

// --- LED FLASH STATE ---
unsigned long ledFlashStart = 0;
bool ledFlashing = false;
const unsigned long ledFlashDuration = 100;

// --- MIDI I/O ---
USBMIDI usbMIDI; // USB MIDI object

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

// --- MIDI IN PARSING ---
// MIDI parser state machine
enum MidiState
{
  WaitingStatus,
  WaitingData1,
  WaitingData2
};
MidiState midiState = WaitingStatus;
uint8_t midiStatus, midiData1;

// Handle incoming MIDI note on
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

// Handle incoming MIDI note off
void handleNoteOff(uint8_t note)
{
  if (capturingChord && note == leadNote)
  {
    currentChord = tempChord;
    capturingChord = false;
    chordLatched = true;
    currentNoteIndex = 0;
    noteRepeatCounter = 0;
  }
}

// Parse incoming MIDI bytes (hardware MIDI in)
void readMidiByte(uint8_t byte)
{
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
      midiState = WaitingData1;
      break;
    }
  }
}

// --- TIMING HUMANIZATION FUNCTION ---
// Returns a random offset for note timing (ms)
int getTimingHumanizeOffset(unsigned long noteLengthMs)
{
  int maxHumanize = noteLengthMs;
  int timingHumanizeAmount = (maxHumanize * timingHumanizePercent) / 100;
  if (timingHumanizeAmount == 0)
    return 0;
  return random(-timingHumanizeAmount, timingHumanizeAmount + 1);
}

// --- NOTE LENGTH RANDOMIZATION FUNCTION ---
// Returns a randomized note length (ms)
unsigned long getRandomizedNoteLength(unsigned long noteLengthMs)
{
  unsigned long maxShorten = noteLengthMs;
  unsigned long shortenAmount = (maxShorten * noteLengthRandomizePercent) / 100;
  if (shortenAmount == 0)
    return noteLengthMs;
  unsigned long randomShorten = random(0, shortenAmount + 1);
  return noteLengthMs - randomShorten;
}

// --- NOTE BIAS FUNCTION ---
// Applies note bias to a chord vector based on percentage.
// Negative: replace notes with lowest note, Positive: with highest note.
void applyNoteBiasToChord(std::vector<uint8_t> &chord, int percent)
{
  if (chord.empty() || percent == 0)
    return;
  size_t chordSize = chord.size();
  uint8_t targetNote = (percent < 0) ? chord.front() : chord.back();
  int absPercent = abs(percent);
  size_t numToReplace = (chordSize > 1) ? ((chordSize * absPercent + 99) / 100) : 0;
  if (numToReplace == 0)
    return;
  std::vector<size_t> indices;
  for (size_t i = 0; i < chordSize; ++i)
  {
    if (chord[i] != targetNote)
      indices.push_back(i);
  }
  std::random_shuffle(indices.begin(), indices.end());
  for (size_t i = 0; i < numToReplace && i < indices.size(); ++i)
  {
    chord[indices[i]] = targetNote;
  }
}

// --- Encoder switch shift-register debounce ---
// Debounce state for encoder switch
static uint16_t encoderSWDebounce = 0;

// --- Add storage for loaded step patterns from EEPROM ---
std::vector<std::vector<std::vector<uint8_t>>> allStepPatternGroups;

// --- Pattern permutation selection state ---
int permutationIndex = 0;
int availablePatternCount = 0;

// --- Helper: Read a single pattern directly from EEPROM ---
// Reads a single permutation pattern for a given chord size and pattern index directly from EEPROM using the address table.
// chordSize: number of notes in the chord (e.g. 2, 3, 4, ...)
// patternIndex: which permutation pattern to read (0-based index)
// pattern: output vector that will be filled with the pattern (indices into the chord)
void readPatternFromEEPROM(int chordSize, int patternIndex, std::vector<uint8_t> &pattern)
{
  // Use the address table for fast lookup (see step_patterns.h for implementation)
  int numPatterns = 1;
  for (int i = 2; i <= chordSize; ++i)
    numPatterns *= i;
  if (chordSize < MIN_STEPS || chordSize > MAX_STEPS || patternIndex < 0 || patternIndex >= numPatterns)
  {
    pattern.clear();
    return;
  }
  int groupStart = readPatternGroupStartAddr(chordSize); // Fast O(1) lookup
  int addr = groupStart + patternIndex * chordSize;
  pattern.clear();
  for (int i = 0; i < chordSize; ++i)
  {
    pattern.push_back(EEPROM.read(addr + i));
  }
}

// --- SETUP ---
// Initialize all hardware and state
void setup()
{
  pinMode(ledBuiltIn, OUTPUT);
  pinMode(clearButtonPin, INPUT_PULLUP);
  pinMode(encoderSW, INPUT_PULLUP);
  rotary_init();

  Serial.begin(115200); // Debug
  unsigned long serialStart = millis();
  while (!Serial && millis() - serialStart < 3000)
  {
    delay(10);
  }
  Serial1.begin(31250, SERIAL_8N1, midiInRxPin, -1);  // MIDI IN
  Serial2.begin(31250, SERIAL_8N1, -1, midiOutTxPin); // MIDI OUT

  USB.begin();
  usbMIDI.begin();

  delay(1000);

  // --- EEPROM: Generate and store all step patterns if not already done ---
  EEPROM.begin(EEPROM_SIZE);

  // Use the last byte as a flag to check if EEPROM is initialized
  if (EEPROM.read(EEPROM_SIZE - 1) != 0xA5)
  {
    writeAllStepPatternsToEEPROM();
    EEPROM.write(EEPROM_SIZE - 1, 0xA5);
    EEPROM.commit();
    Serial.println("Step patterns written to EEPROM.");
  }
  else
  {
    Serial.println("Step patterns already in EEPROM.");
  }

  // --- Load all step patterns from EEPROM into RAM for fast access ---
  readAllStepPatternsFromEEPROM(allStepPatternGroups);

  capturingChord = true;
  tempChord.clear();
  leadNote = 48;
  handleNoteOn(48);
  handleNoteOn(55);
  handleNoteOn(52);
  handleNoteOn(60);
  handleNoteOff(48);
}

// --- LOOP ---
// Main loop: handle input, arpeggiator, and output
void loop()
{
  unsigned long now = millis();

  // --- Clear button handling ---
  static bool lastClear = HIGH;
  bool currentClear = digitalRead(clearButtonPin);
  if (lastClear == HIGH && currentClear == LOW)
  {
    currentChord.clear();
    chordLatched = false;
    currentNoteIndex = 0;
    noteRepeatCounter = 0;
    neopixelWrite(ledBuiltIn, 0, 0, 127);
    ledFlashStart = millis();
    ledFlashing = true;
  }
  lastClear = currentClear;

  // --- Encoder switch shift-register debounce ---
  encoderSWDebounce = (encoderSWDebounce << 1) | !digitalRead(encoderSW);
  bool swDebounced = (encoderSWDebounce == 0xFFFF);

  static bool swHandled = false;
  if (swDebounced && !swHandled)
  {
    encoderMode = static_cast<EncoderMode>((encoderMode + 1) % encoderModeSize);
    neopixelWrite(ledBuiltIn, 0, 0, 127);
    ledFlashStart = millis();
    ledFlashing = true;
    swHandled = true;
  }
  if (!swDebounced)
  {
    swHandled = false;
  }

  // --- Rotary encoder processing ---
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

  // --- Parameter adjustment via encoder ---
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
      timingHumanizePercent = constrain(timingHumanizePercent + delta, 0, maxTimingHumanizePercent);
      timingHumanize = (timingHumanizePercent > 0);
      break;
    case MODE_LENGTH_RANDOMIZE:
      noteLengthRandomizePercent = constrain(noteLengthRandomizePercent + delta, 0, maxNoteLengthRandomizePercent);
      break;
    case MODE_BALANCE:
      noteBalancePercent = constrain(noteBalancePercent + delta * 10, -100, 100);
      break;
    }
    arpInterval = 60000 / (bpm * notesPerBeat);
  }

  // --- MIDI IN (hardware) ---
  while (Serial1.available())
    readMidiByte(Serial1.read());

  // --- MIDI IN (USB) ---
  midiEventPacket_t packet;
  while (usbMIDI.readPacket(&packet))
  {
    uint8_t cin = packet.header & 0x0F;
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
    }
  }

  // --- Chord processing ---
  std::vector<uint8_t> baseChord = capturingChord ? tempChord : currentChord;

  // Remove duplicates, preserving order for PLAYED pattern
  std::vector<uint8_t> playedChord;
  for (uint8_t note : baseChord)
  {
    if (std::find(playedChord.begin(), playedChord.end(), note) == playedChord.end())
    {
      playedChord.push_back(note);
    }
  }

  // For other patterns, create a sorted, deduplicated chord
  std::vector<uint8_t> orderedChord = playedChord;
  std::sort(orderedChord.begin(), orderedChord.end());

  // --- Print EEPROM patterns for baseChord size only when baseChord changes ---
  static std::vector<uint8_t> lastPrintedChord;
  if (orderedChord != lastPrintedChord)
  {
    int baseChordSize = orderedChord.size();
    if (baseChordSize >= MIN_STEPS && baseChordSize <= MAX_STEPS)
    {
      // Calculate number of patterns (factorial)
      int numPatterns = 1;
      for (int i = 2; i <= baseChordSize; ++i)
        numPatterns *= i;
      Serial.print("EEPROM patterns for ");
      Serial.print(baseChordSize);
      Serial.println(" notes:");
      std::vector<uint8_t> pattern;
      for (int i = 0; i < numPatterns; ++i)
      {
        readPatternFromEEPROM(baseChordSize, i, pattern);
        Serial.print("Pattern ");
        Serial.print(i);
        Serial.print(": ");
        for (size_t j = 0; j < pattern.size(); ++j)
        {
          Serial.print((int)pattern[j]);
          if (j < pattern.size() - 1)
            Serial.print(",");
        }
        Serial.println();
      }
    }
    lastPrintedChord = orderedChord;
  }

  // --- Select available step patterns from EEPROM based on chord size ---
  std::vector<std::vector<uint8_t>> availablePatterns;
  int chordSize = orderedChord.size();
  if (chordSize >= MIN_STEPS && chordSize <= MAX_STEPS)
  {
    availablePatterns = allStepPatternGroups[chordSize - MIN_STEPS];
    availablePatternCount = availablePatterns.size();
    // Clamp permutationIndex if chord size changes
    if (permutationIndex >= availablePatternCount)
      permutationIndex = 0;
  }
  else
  {
    availablePatterns.clear();
    availablePatternCount = 0;
    permutationIndex = 0;
  }

  // --- Encoder logic for selecting permutation pattern when in MODE_PATTERN ---
  static int lastPatternDelta = 0;
  static int lastChordSize = 0;
  static EncoderMode lastPatternMode = encoderMode;

  // Only allow permutation selection in MODE_PATTERN and for valid patterns
  if (encoderMode == MODE_PATTERN && availablePatternCount > 0)
  {
    // Use the rotary encoder to change permutationIndex
    int patternDelta = 0;
    static int patternStepCounter = 0;
    unsigned char result = rotary_process();
    if (result == 0x10)
      patternStepCounter++;
    else if (result == 0x20)
      patternStepCounter--;
    if (abs(patternStepCounter) >= 2)
    {
      patternDelta = (patternStepCounter > 0) ? 1 : -1;
      patternStepCounter = 0;
    }

    // Only update permutationIndex if encoder is in MODE_PATTERN
    if (patternDelta != 0)
    {
      permutationIndex += patternDelta;
      if (permutationIndex < 0)
        permutationIndex = availablePatternCount - 1;
      if (permutationIndex >= availablePatternCount)
        permutationIndex = 0;
      Serial.print("Permutation index: ");
      Serial.print(permutationIndex);
      Serial.print(" / ");
      Serial.println(availablePatternCount - 1);
    }

    // Reset permutationIndex if chord size changes or mode changes
    if (chordSize != lastChordSize || encoderMode != lastPatternMode)
    {
      permutationIndex = 0;
    }
    lastChordSize = chordSize;
    lastPatternMode = encoderMode;
  }
  else
  {
    permutationIndex = 0;
  }

  // --- Use the selected permutation pattern ---
  std::vector<uint8_t> permutationPattern;
  if (!availablePatterns.empty())
  {
    permutationPattern = availablePatterns[permutationIndex];
  }
  else
  {
    permutationPattern.clear();
  }

  // --- Build the playingChord with octave shifts and no duplicates, using permutation if desired ---
  std::vector<uint8_t> playingChord;
  const std::vector<uint8_t> &chordSource =
      (currentPattern == PLAYED) ? playedChord : orderedChord;

  if (currentPattern == PLAYED || permutationPattern.empty())
  {
    // Default: use playedChord or orderedChord as before
    for (int oct = -abs(octaveRange); oct <= abs(octaveRange); ++oct)
    {
      if ((octaveRange >= 0 && oct < 0) || (octaveRange < 0 && oct > 0))
        continue;
      for (uint8_t note : chordSource)
      {
        int shifted = note + 12 * oct;
        if (shifted >= 0 && shifted <= 127 && std::find(playingChord.begin(), playingChord.end(), shifted) == playingChord.end())
          playingChord.push_back(shifted);
      }
    }
  }
  else
  {
    // Use the permutation pattern from EEPROM to order the notes
    for (int oct = -abs(octaveRange); oct <= abs(octaveRange); ++oct)
    {
      if ((octaveRange >= 0 && oct < 0) || (octaveRange < 0 && oct > 0))
        continue;
      for (uint8_t idx : permutationPattern)
      {
        if (idx < chordSource.size())
        {
          int shifted = chordSource[idx] + 12 * oct;
          if (shifted >= 0 && shifted <= 127 && std::find(playingChord.begin(), playingChord.end(), shifted) == playingChord.end())
            playingChord.push_back(shifted);
        }
      }
    }
  }

  // --- Apply note bias based on noteBalancePercent ---
  applyNoteBiasToChord(playingChord, noteBalancePercent);

  // --- Arpeggiator timing and note scheduling ---
  static int timingOffset = 0;
  static unsigned long nextNoteTime = 0;

  unsigned long noteLengthMs = arpInterval * noteLengthPercent / 100;
  unsigned long randomizedNoteLengthMs = getRandomizedNoteLength(noteLengthMs);

  if (nextNoteTime == 0)
    nextNoteTime = now;

  uint8_t velocityToSend = noteVelocity;

  // --- CHORD pattern: play all notes together ---
  static bool chordNotesOn = false;
  static std::vector<uint8_t> chordNotesPlaying;
  static std::vector<uint8_t> chordVelocities;

  if (currentPattern == CHORD)
  {
    if (!chordNotesOn && !playingChord.empty() && now >= nextNoteTime)
    {
      chordNotesPlaying = playingChord;
      chordVelocities.clear();
      for (size_t i = 0; i < chordNotesPlaying.size(); ++i)
      {
        uint8_t v = noteVelocity;
        if (velocityDynamicsPercent > 0)
        {
          int maxAdjustment = (noteVelocity * velocityDynamicsPercent) / 100;
          v = constrain(noteVelocity - random(0, maxAdjustment + 1), 1, 127);
        }
        chordVelocities.push_back(v);
      }
      timingOffset = (timingHumanize ? getTimingHumanizeOffset(noteLengthMs) : 0);
      noteOnStartTime = now + timingOffset;
      noteOnActive = true;
      chordNotesOn = true;
      noteRepeatCounter = 0;
      nextNoteTime += arpInterval;
    }
    static bool chordNoteOnSent = false;
    if (chordNotesOn && !chordNoteOnSent && now >= noteOnStartTime)
    {
      for (size_t i = 0; i < chordNotesPlaying.size(); ++i)
      {
        sendNoteOff(chordNotesPlaying[i]);
        sendNoteOn(chordNotesPlaying[i], chordVelocities[i]);
      }
      chordNoteOnSent = true;
    }
    if (chordNotesOn && chordNoteOnSent && now >= noteOnStartTime + randomizedNoteLengthMs)
    {
      for (uint8_t note : chordNotesPlaying)
      {
        sendNoteOff(note);
      }
      noteRepeatCounter++;
      if (noteRepeatCounter >= noteRepeat)
      {
        chordNotesOn = false;
        chordNoteOnSent = false;
        noteOnActive = false;
      }
      else
      {
        chordNoteOnSent = false;
        noteOnStartTime = now + timingOffset;
      }
    }
  }
  else
  {
    // --- Note selection with balance ---
    auto getBalancedNoteIndex = [&](size_t chordSize, size_t step) -> size_t
    {
      if (chordSize <= 1)
      {
        return step % chordSize;
      }
      size_t idx = step % chordSize;
      return constrain(idx, 0, chordSize - 1);
    };

    if (!noteOnActive && !playingChord.empty() && now >= nextNoteTime)
    {
      uint8_t noteIndex = 0;
      size_t chordSize = playingChord.size();
      switch (currentPattern)
      {
      case UP:
        noteIndex = getBalancedNoteIndex(chordSize, currentNoteIndex);
        break;
      case DOWN:
        noteIndex = chordSize - 1 - getBalancedNoteIndex(chordSize, currentNoteIndex);
        break;
      case TRIANGLE:
        if (ascending)
        {
          noteIndex = getBalancedNoteIndex(chordSize, currentNoteIndex);
          if (noteIndex >= chordSize - 1)
            ascending = false;
        }
        else
        {
          noteIndex = chordSize - 1 - getBalancedNoteIndex(chordSize, currentNoteIndex);
          if (noteIndex == 0)
            ascending = true;
        }
        break;
      case SINE:
        noteIndex = getBalancedNoteIndex(chordSize, map(sineTable[currentNoteIndex % 16], 0, 15, 0, chordSize - 1));
        break;
      case SQUARE:
        noteIndex = getBalancedNoteIndex(chordSize, (currentNoteIndex % 2 == 0) ? 0 : chordSize - 1);
        break;
      case RANDOM:
        noteIndex = getBalancedNoteIndex(chordSize, random(chordSize));
        break;
      case PLAYED:
        noteIndex = getBalancedNoteIndex(chordSize, currentNoteIndex);
        break;
      }
      int transposedNote = constrain(playingChord[noteIndex] + 12 * transpose, 0, 127);
      lastPlayedNote = transposedNote;

      velocityToSend = noteVelocity;
      if (velocityDynamicsPercent > 0)
      {
        int maxAdjustment = (noteVelocity * velocityDynamicsPercent) / 100;
        velocityToSend = constrain(noteVelocity - random(0, maxAdjustment + 1), 1, 127);
      }

      timingOffset = (timingHumanize ? getTimingHumanizeOffset(noteLengthMs) : 0);
      noteOnStartTime = now + timingOffset;
      noteOnActive = true;
      nextNoteTime += arpInterval;
    }

    static bool noteOnSent = false;
    if (noteOnActive && !noteOnSent && now >= noteOnStartTime)
    {
      sendNoteOn(lastPlayedNote, velocityToSend);
      noteOnSent = true;
    }

    if (noteOnActive && noteOnSent && now >= noteOnStartTime + randomizedNoteLengthMs)
    {
      sendNoteOff(lastPlayedNote);
      noteOnActive = false;
      noteOnSent = false;
      if (++noteRepeatCounter >= noteRepeat)
      {
        noteRepeatCounter = 0;
        switch (currentPattern)
        {
        case UP:
        case DOWN:
          currentNoteIndex = (currentNoteIndex + 1) % playingChord.size();
          break;
        case TRIANGLE:
          if (ascending)
            currentNoteIndex++;
          else
            currentNoteIndex--;
          currentNoteIndex = constrain(currentNoteIndex, 0, playingChord.size() - 1);
          break;
        case SINE:
          currentNoteIndex = (currentNoteIndex + 1) % 16;
          break;
        case SQUARE:
          currentNoteIndex = (currentNoteIndex + 1) % 2;
          break;
        case RANDOM:
          break;
        case PLAYED:
          currentNoteIndex = (currentNoteIndex + 1) % playingChord.size();
          break;
        }
      }
    }
  }

  // --- LED flash timing ---
  if (ledFlashing && now - ledFlashStart >= ledFlashDuration)
  {
    neopixelWrite(ledBuiltIn, 0, 0, 0);
    ledFlashing = false;
  }

  // --- Serial debug output for parameter changes ---
  static int lastBPM = bpm, lastLength = noteLengthPercent, lastVelocity = noteVelocity, lastOctave = octaveRange;
  static int lastResolutionIndex = notesPerBeatIndex, lastNoteRepeat = noteRepeat, lastTranspose = transpose;
  static ArpPattern lastPattern = currentPattern;
  static EncoderMode lastMode = encoderMode;
  static int lastUseVelocityDynamics = velocityDynamicsPercent;
  static bool lastTimingHumanize = timingHumanize;
  static int lastTimingHumanizePercent = timingHumanizePercent;
  static int lastNoteLengthRandomizePercent = noteLengthRandomizePercent;
  static int lastNoteBalancePercent = noteBalancePercent;

  if (encoderMode == MODE_BPM && bpm != lastBPM)
  {
    Serial.print("BPM: ");
    Serial.println(bpm);
    lastBPM = bpm;
  }
  if (encoderMode == MODE_LENGTH && noteLengthPercent != lastLength)
  {
    Serial.print("Note Length %: ");
    Serial.println(noteLengthPercent);
    lastLength = noteLengthPercent;
  }
  if (encoderMode == MODE_VELOCITY && noteVelocity != lastVelocity)
  {
    Serial.print("Velocity: ");
    Serial.println(noteVelocity);
    lastVelocity = noteVelocity;
  }
  if (encoderMode == MODE_OCTAVE && octaveRange != lastOctave)
  {
    Serial.print("Octave Range: ");
    Serial.println(octaveRange);
    lastOctave = octaveRange;
  }
  if (encoderMode == MODE_RESOLUTION && notesPerBeatIndex != lastResolutionIndex)
  {
    Serial.print("Notes Per Beat: ");
    Serial.println(notesPerBeat);
    lastResolutionIndex = notesPerBeatIndex;
  }
  if (encoderMode == MODE_REPEAT && noteRepeat != lastNoteRepeat)
  {
    Serial.print("Note Repeat: ");
    Serial.println(noteRepeat);
    lastNoteRepeat = noteRepeat;
  }
  if (encoderMode == MODE_TRANSPOSE && transpose != lastTranspose)
  {
    Serial.print("Transpose: ");
    Serial.println(transpose);
    lastTranspose = transpose;
  }
  if (encoderMode == MODE_PATTERN && currentPattern != lastPattern)
  {
    // --- Send note off for all possible notes in playingChord when switching pattern ---
    for (uint8_t note : playingChord)
    {
      sendNoteOff(note);
    }
    Serial.print("Pattern: ");
    Serial.println(patternNames[currentPattern]);
    lastPattern = currentPattern;
  }
  if (encoderMode == MODE_DYNAMICS && velocityDynamicsPercent != lastUseVelocityDynamics)
  {
    Serial.print("Velocity Dynamics Percent: ");
    Serial.println(velocityDynamicsPercent);
    lastUseVelocityDynamics = velocityDynamicsPercent;
  }
  if (encoderMode == MODE_HUMANIZE && timingHumanizePercent != lastTimingHumanizePercent)
  {
    Serial.print("Timing Humanize Percent: ");
    Serial.println(timingHumanizePercent);
    lastTimingHumanizePercent = timingHumanizePercent;
  }
  if (encoderMode == MODE_LENGTH_RANDOMIZE && noteLengthRandomizePercent != lastNoteLengthRandomizePercent)
  {
    Serial.print("Note Length Randomize Percent: ");
    Serial.println(noteLengthRandomizePercent);
    lastNoteLengthRandomizePercent = noteLengthRandomizePercent;
  }
  if (encoderMode == MODE_BALANCE && noteBalancePercent != lastNoteBalancePercent)
  {
    Serial.print("Note Balance Percent: ");
    Serial.println(noteBalancePercent);
    lastNoteBalancePercent = noteBalancePercent;
  }

  if (encoderMode != lastMode)
  {
    Serial.print("Encoder Mode: ");
    switch (encoderMode)
    {
    case MODE_BPM:
      Serial.println("BPM");
      break;
    case MODE_LENGTH:
      Serial.println("Note Length %");
      break;
    case MODE_VELOCITY:
      Serial.println("Velocity");
      break;
    case MODE_OCTAVE:
      Serial.println("Octave Range");
      break;
    case MODE_PATTERN:
      Serial.println("Pattern");
      break;
    case MODE_RESOLUTION:
      Serial.println("Notes Per Beat");
      break;
    case MODE_REPEAT:
      Serial.println("Note Repeat");
      break;
    case MODE_TRANSPOSE:
      Serial.println("Transpose");
      break;
    case MODE_DYNAMICS:
      Serial.println("Velocity Dynamics Percent");
      break;
    case MODE_HUMANIZE:
      Serial.println("Timing Humanize Percent");
      break;
    case MODE_LENGTH_RANDOMIZE:
      Serial.println("Note Length Randomize Percent");
      break;
    case MODE_BALANCE:
      Serial.println("Note Balance Percent");
      break;
    }
    lastMode = encoderMode;
  }
}