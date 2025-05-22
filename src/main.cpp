#include <Arduino.h>
#include <vector>
#include <EEPROM.h>
#include <USB.h>
#include <USBMIDI.h>
#include "PatternGenerators.h"
#include "Constants.h"

// EEPROM_SIZE is not used, but left for reference
#define EEPROM_SIZE 4096 // Make sure this is large enough for all patterns

// --- CONFIGURATION ---
// Pin assignments for MIDI, LED, encoder, and buttons
// (moved to Constants.h)

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

// --- RHYTHM PATTERNS ---
// Use pattern generators for rhythm accents instead of static arrays
int selectedRhythmPattern = 0;                // Index into pattern generators for rhythm
const int rhythmPatternCount = PAT_COUNT - 1; // Use all except PAT_ASPLAYED

const char *rhythmPatternNames[rhythmPatternCount] = {
    "Up", "Down", "Up-Down", "Down-Up", "Outer-In", "Inward Bounce", "Zigzag", "Spiral", "Mirror", "Saw", "Saw Reverse",
    "Bounce", "Reverse Bounce", "Ladder", "Skip Up", "Jump Step", "Crossover", "Random", "Even-Odd", "Odd-Even",
    "Edge Loop", "Center Bounce", "Up Double", "Skip Reverse", "Snake", "Pendulum", "Asymmetric Loop", "Short Long",
    "Backward Jump", "Inside Bounce", "Staggered Rise"};

EncoderMode encoderMode = MODE_BPM;

const int encoderModeSize = MODE_COUNT; // Use enum count for size

// --- STRAIGHT/LOOP mode for pattern playback ---
enum PatternPlaybackMode
{
  STRAIGHT,
  LOOP
};
PatternPlaybackMode patternPlaybackMode = LOOP;

// --- REVERSE mode for pattern playback ---
bool patternReverse = false;

// --- SMOOTH mode for pattern playback ---
bool patternSmooth = true;

// --- PARAMETERS ---
// All arpeggiator parameters
// (moved to Constants.h)

// --- DEFAULTS ---
int bpm = 96;                        // Beats per minute
int noteLengthPercent = 40;          // Note length as percent of interval
int noteVelocity = 127;              // MIDI velocity
int octaveRange = 0;                 // Octave spread
int transpose = 0;                   // Transpose in octaves
int velocityDynamicsPercent = 56;    // Velocity randomization percent
bool timingHumanize = false;         // Enable timing humanization
int timingHumanizePercent = 4;       // Humanization percent
int noteLengthRandomizePercent = 20; // Note length randomization percent
int noteBalancePercent = 0;          // Note bias percent
int randomChordPercent = 0;          // Percentage of steps to replace with random 3-note chords
int noteRangeShift = 0;              // Range shift for lowest/highest note, -24..24 (or -127..127 if you want)
int noteRangeStretch = 0;            // Range stretch for lowest/highest note, -8..8
int notesPerBeatIndex = 4;           // 4 notes per beat
int noteRepeat = 1;                  // Number of repeats per note

int notesPerBeat = notesPerBeatOptions[notesPerBeatIndex];

int noteRepeatCounter = 0;
unsigned long arpInterval = 60000 / (bpm * notesPerBeat); // ms per note

// --- RANDOM CHORD FUNCTION ---
// At random steps, replace the note with a 3-note chord (from playedChord, close together)
// Each chord is played as a single step (all 3 notes at once)
struct StepNotes
{
  std::vector<uint8_t> notes;
};

void buildRandomChordSteps(
    std::vector<StepNotes> &stepNotes,
    const std::vector<uint8_t> &playingChord,
    const std::vector<uint8_t> &playedChord,
    int percent)
{
  stepNotes.clear();
  if (playingChord.empty() || playedChord.size() < 3 || percent <= 0)
  {
    // Default: each step is a single note
    for (uint8_t n : playingChord)
      stepNotes.push_back({std::vector<uint8_t>{n}});
    return;
  }

  // Sort playedChord for adjacency and deduplicate
  std::vector<uint8_t> sortedPlayed = playedChord;
  std::sort(sortedPlayed.begin(), sortedPlayed.end());
  sortedPlayed.erase(std::unique(sortedPlayed.begin(), sortedPlayed.end()), sortedPlayed.end());

  size_t steps = playingChord.size();
  int numChords = (steps * percent + 99) / 100;
  if (numChords == 0)
  {
    for (uint8_t n : playingChord)
      stepNotes.push_back({std::vector<uint8_t>{n}});
    return;
  }

  // Pick random steps to replace
  std::vector<size_t> indices(steps);
  for (size_t i = 0; i < steps; ++i)
    indices[i] = i;
  std::random_shuffle(indices.begin(), indices.end());

  std::vector<bool> isChordStep(steps, false);
  for (int i = 0; i < numChords && i < (int)steps; ++i)
    isChordStep[indices[i]] = true;

  for (size_t i = 0; i < steps; ++i)
  {
    if (isChordStep[i])
    {
      // The root note is the pattern note
      uint8_t root = playingChord[i];
      // Find the next two higher notes from sortedPlayed, transposed up if needed
      std::vector<uint8_t> chord{root};
      // Find all notes in sortedPlayed that are higher than root
      std::vector<uint8_t> higher;
      for (uint8_t n : sortedPlayed)
      {
        if (n > root)
          higher.push_back(n);
      }
      // If not enough higher notes, transpose up by octaves
      int octave = 1;
      while (higher.size() < 2)
      {
        for (uint8_t n : sortedPlayed)
        {
          uint8_t candidate = n + 12 * octave;
          if (candidate > root && candidate <= 127)
            higher.push_back(candidate);
          if (higher.size() >= 2)
            break;
        }
        ++octave;
        if (octave > 10)
          break; // avoid infinite loop
      }
      // Add up to 2 higher notes to the chord
      for (size_t k = 0; k < 2 && k < higher.size(); ++k)
        chord.push_back(higher[k]);
      // If still not enough, fill with root transposed up
      while (chord.size() < 3)
        chord.push_back(root + 12 * (int)chord.size());
      stepNotes.push_back({chord});
    }
    else
    {
      stepNotes.push_back({std::vector<uint8_t>{playingChord[i]}});
    }
  }
}

// --- PATTERNS ---
// Index of currently selected pattern
int selectedPatternIndex = 0;
// (All pattern generator functions, enums, arrays, and function pointers have been moved to PatternGenerators.h/.cpp)

// --- STATE ---
// Chord and note state
// baseChord: The chord as currently being played or captured (raw input, possibly with duplicates, order preserved).
// playedChord: The chord after removing duplicates, preserving order (used for PAT_ASPLAYED).
// orderedChord: The chord after sorting and deduplication (used for most patterns, and for range shifting).
// playingChord: The final note sequence for the current arpeggio, after applying pattern, octave, reverse, smooth, bias, and range shift.
// stepNotes: The notes (possibly chords) to be played at each arpeggiator step, after all processing.
std::vector<uint8_t> currentChord; // Latched chord
std::vector<uint8_t> tempChord;    // Chord being captured
uint8_t leadNote = 0;              // First note of chord
bool capturingChord = false;       // Are we capturing a chord?
size_t currentNoteIndex = 0;       // Step in pattern
bool noteOnActive = false;         // Is a note currently on?
unsigned long noteOnStartTime = 0; // When was note on sent
uint8_t lastPlayedNote = 0;        // Last note played

// --- LED FLASH STATE ---
unsigned long ledFlashStart = 0;            // When did LED flash start
bool ledFlashing = false;                   // Is LED currently flashing
const unsigned long ledFlashDuration = 100; // ms

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

// --- MIDI CC PARAMETER CONTROL ---
// Map MIDI CC numbers to parameter pointers or setters
void handleMidiCC(uint8_t cc, uint8_t value)
{
  switch (cc)
  {
  case 1: // Mod Wheel -> BPM (scaled 40-240)
    bpm = map(value, 0, 127, 40, 240);
    break;
  case 2: // Breath -> Note Length %
    noteLengthPercent = map(value, 0, 127, 5, 100);
    break;
  case 3: // CC3 -> Velocity
    noteVelocity = map(value, 0, 127, 1, 127);
    break;
  case 4: // CC4 -> Octave Range
    octaveRange = map(value, 0, 127, -3, 3);
    break;
  case 5: // CC5 -> Pattern
    selectedPatternIndex = constrain(map(value, 0, 127, 0, PAT_COUNT - 1), 0, PAT_COUNT - 1);
    break;
  case 6: // CC6 -> Pattern Playback Mode
    patternPlaybackMode = (value < 64) ? STRAIGHT : LOOP;
    break;
  case 7: // CC7 -> Pattern Reverse
    patternReverse = (value >= 64);
    break;
  case 8: // CC8 -> Pattern Smooth
    patternSmooth = (value >= 64);
    break;
  case 9: // CC9 -> Note Repeat
    noteRepeat = constrain(map(value, 0, 127, 1, 4), 1, 4);
    break;
  case 10: // CC10 -> Transpose
    transpose = map(value, 0, 127, minTranspose, maxTranspose);
    break;
  case 11: // CC11 -> Velocity Dynamics
    velocityDynamicsPercent = map(value, 0, 127, 0, 100);
    break;
  case 12: // CC12 -> Timing Humanize
    timingHumanizePercent = map(value, 0, 127, 0, maxTimingHumanizePercent);
    timingHumanize = (timingHumanizePercent > 0);
    break;
  case 13: // CC13 -> Note Length Randomize
    noteLengthRandomizePercent = map(value, 0, 127, 0, maxNoteLengthRandomizePercent);
    break;
  case 14: // CC14 -> Note Balance
    noteBalancePercent = map(value, 0, 127, -100, 100);
    break;
  case 15: // CC15 -> Notes Per Beat (Resolution)
    notesPerBeatIndex = constrain(map(value, 0, 127, 0, notesPerBeatOptionsSize - 1), 0, notesPerBeatOptionsSize - 1);
    notesPerBeat = notesPerBeatOptions[notesPerBeatIndex];
    break;
  case 16: // CC16 -> Random Chord Percent
    randomChordPercent = map(value, 0, 127, 0, 100);
    break;
  case 17: // CC17 -> Rhythm Pattern
    selectedRhythmPattern = constrain(map(value, 0, 127, 0, rhythmPatternCount - 1), 0, rhythmPatternCount - 1);
    break;
  case 18: // CC18 -> Range Shift
    noteRangeShift = map(value, 0, 127, -24, 24);
    break;
  case 19: // CC19 -> Range Stretch
    noteRangeStretch = map(value, 0, 127, -24, 24);
    break;
  }
  arpInterval = 60000 / (bpm * notesPerBeat);
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

// --- MIDI CLOCK SYNC ---
#include "midiUtils.h"

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

  capturingChord = true;
  tempChord.clear();
  leadNote = 55;
  handleNoteOn(55);
  handleNoteOn(58);
  handleNoteOn(60);
  handleNoteOn(62);
  handleNoteOn(65);
  handleNoteOn(67);
  handleNoteOff(55);
}

// --- LOOP ---
// Main loop: handle input, arpeggiator, and output
template <typename T>
void printIfChanged(const char *label, T &lastValue, T currentValue)
{
  if (currentValue != lastValue)
  {
    Serial.print(label);
    Serial.println(currentValue);
    lastValue = currentValue;
  }
}

void loop()
{
  unsigned long now = millis();

  // --- Clear button handling ---
  static bool lastClear = HIGH;
  bool currentClear = digitalRead(clearButtonPin);
  // If clear button pressed, clear chord and reset state
  if (lastClear == HIGH && currentClear == LOW)
  {
    currentChord.clear();
    currentNoteIndex = 0;
    noteRepeatCounter = 0;
    neopixelWrite(ledBuiltIn, 0, 0, 64); // Blue LED flash
    ledFlashStart = millis();
    ledFlashing = true;
  }
  lastClear = currentClear;

  // --- Encoder switch shift-register debounce ---
  encoderSWDebounce = (encoderSWDebounce << 1) | !digitalRead(encoderSW);
  bool swDebounced = (encoderSWDebounce == 0xFFFF);

  static bool swHandled = false;
  // Encoder switch short press: cycle encoder mode
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
  // Count encoder steps
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
      selectedPatternIndex += delta;
      selectedPatternIndex = constrain(selectedPatternIndex, 0, PAT_COUNT - 1);
      Serial.print("Pattern: ");
      Serial.print(customPatternNames[selectedPatternIndex]);
      Serial.print(" [");
      {
        std::vector<uint8_t> baseChord = capturingChord ? tempChord : currentChord;
        std::vector<uint8_t> playedChord = baseChord;
        std::vector<uint8_t> orderedChord = playedChord;
        std::sort(orderedChord.begin(), orderedChord.end());
        int n = (selectedPatternIndex == PAT_ASPLAYED) ? playedChord.size() : orderedChord.size();
        std::vector<uint8_t> pat;
        if (selectedPatternIndex == PAT_ASPLAYED)
        {
          pat = patternAsPlayed(n, playedChord);
        }
        else
        {
          pat = customPatternFuncs[selectedPatternIndex](n);
        }
        // Apply LOOP mode preview
        std::vector<uint8_t> patPreview = pat;
        if (patternPlaybackMode == LOOP && pat.size() > 2)
        {
          for (int i = pat.size() - 2; i > 0; --i)
            patPreview.push_back(pat[i]);
        }
        // Apply REVERSE mode preview
        if (patternReverse && !patPreview.empty())
        {
          std::reverse(patPreview.begin(), patPreview.end());
        }
        for (size_t i = 0; i < patPreview.size(); ++i)
        {
          Serial.print((int)patPreview[i]);
          if (i < patPreview.size() - 1)
            Serial.print(",");
        }
      }
      Serial.print("] ");
      Serial.println(patternPlaybackMode == STRAIGHT ? "STRAIGHT" : "LOOP");
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
    case MODE_PATTERN_PLAYBACK:
      patternPlaybackMode = (patternPlaybackMode == STRAIGHT) ? LOOP : STRAIGHT;
      Serial.print("Pattern Playback Mode: ");
      Serial.println(patternPlaybackMode == STRAIGHT ? "STRAIGHT" : "LOOP");
      break;
    case MODE_REVERSE:
      patternReverse = !patternReverse;
      Serial.print("Pattern Reverse: ");
      Serial.println(patternReverse ? "ON" : "OFF");
      break;
    case MODE_SMOOTH:
      patternSmooth = !patternSmooth;
      Serial.print("Pattern Smooth: ");
      Serial.println(patternSmooth ? "ON" : "OFF");
      break;
    case MODE_RANDOM_CHORD:
      randomChordPercent = constrain(randomChordPercent + delta * 10, 0, 100);
      break;
    case MODE_RHYTHM:
      selectedRhythmPattern = constrain(selectedRhythmPattern + delta, 0, rhythmPatternCount - 1);
      // Serial.print("Rhythm Pattern: ");
      // Serial.println(rhythmPatternNames[selectedRhythmPattern]);
      break;
    case MODE_RANGE:
      noteRangeShift = constrain(noteRangeShift + delta, -24, 24);
      break;
    case MODE_STRETCH:
      noteRangeStretch = constrain(noteRangeStretch + delta, -24, 24);
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
    if (packet.byte1 == 0xF8)
    { // MIDI Clock
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
    }
  }

  // --- Chord processing ---
  // baseChord: The chord as currently being played or captured (raw input, possibly with duplicates, order preserved).
  std::vector<uint8_t> baseChord = capturingChord ? tempChord : currentChord;

  // playedChord: The chord after removing duplicates, preserving order (used for PAT_ASPLAYED).
  std::vector<uint8_t> playedChord = baseChord;

  // orderedChord: The chord after sorting and deduplication (used for most patterns, and for range shifting).
  std::vector<uint8_t> orderedChord = playedChord;
  std::sort(orderedChord.begin(), orderedChord.end());

  // Remove duplicates from orderedChord
  orderedChord.erase(std::unique(orderedChord.begin(), orderedChord.end()), orderedChord.end());

  // --- Apply range shift to orderedChord before building pattern indices ---
  // When noteRangeShift > 0, shift up by removing the lowest note and adding oldLowest+12 (clamped), for each step.
  // When noteRangeShift < 0, shift down by removing the highest note and adding oldHighest-12 (clamped), for each step.
  if (noteRangeShift > 0)
  {
    for (int i = 0; i < noteRangeShift; ++i)
    {
      if (!orderedChord.empty())
      {
        std::sort(orderedChord.begin(), orderedChord.end());
        uint8_t oldLowest = orderedChord.front();
        orderedChord.erase(orderedChord.begin());
        uint8_t newNote = constrain(oldLowest + 12, 0, 127);
        orderedChord.push_back(newNote);
        std::sort(orderedChord.begin(), orderedChord.end());
        // Remove duplicates again in case newNote already exists
        // orderedChord.erase(std::unique(orderedChord.begin(), orderedChord.end()), orderedChord.end());
      }
    }
  }
  else if (noteRangeShift < 0)
  {
    for (int i = 0; i < -noteRangeShift; ++i)
    {
      if (!orderedChord.empty())
      {
        std::sort(orderedChord.begin(), orderedChord.end());
        uint8_t oldHighest = orderedChord.back();
        orderedChord.pop_back();
        int newNote = constrain(static_cast<int>(oldHighest) - 12, 0, 127);
        orderedChord.insert(orderedChord.begin(), newNote);
        std::sort(orderedChord.begin(), orderedChord.end());
        // Remove duplicates again in case newNote already exists
        // orderedChord.erase(std::unique(orderedChord.begin(), orderedChord.end()), orderedChord.end());
      }
    }
  }

  // --- Apply range stretch to orderedChord before building pattern indices ---
  // When noteRangeStretch > 0, add extra notes up by one octave above the lowest notes.
  // When noteRangeStretch < 0, add extra notes down by one octave below the highest notes.
  if (noteRangeStretch > 0)
  {
    for (int i = 0; i < noteRangeStretch; ++i)
    {
      if (!orderedChord.empty())
      {
        // Always use the i-th lowest note for each stretch step
        std::sort(orderedChord.begin(), orderedChord.end());
        uint8_t baseNote = orderedChord[i % orderedChord.size()];
        uint8_t newNote = constrain(baseNote + 12, 0, 127);
        orderedChord.push_back(newNote);
        std::sort(orderedChord.begin(), orderedChord.end());
        orderedChord.erase(std::unique(orderedChord.begin(), orderedChord.end()), orderedChord.end());
      }
    }
  }
  else if (noteRangeStretch < 0)
  {
    for (int i = 0; i < -noteRangeStretch; ++i)
    {
      if (!orderedChord.empty())
      {
        // Always use the i-th highest note for each stretch step
        std::sort(orderedChord.begin(), orderedChord.end());
        uint8_t baseNote = orderedChord[orderedChord.size() - 1 - (i % orderedChord.size())];
        int newNote = constrain(static_cast<int>(baseNote) - 12, 0, 127);
        orderedChord.insert(orderedChord.begin(), newNote);
        std::sort(orderedChord.begin(), orderedChord.end());
        orderedChord.erase(std::unique(orderedChord.begin(), orderedChord.end()), orderedChord.end());
      }
    }
  }

  // --- Build the playingChord with octave shifts and no duplicates using selected pattern
  // playingChord: The final note sequence for the current arpeggio, after applying pattern, octave, reverse, smooth, bias, and range shift.
  std::vector<uint8_t> patternIndices;
  // Always use the selected pattern, regardless of encoder mode
  if (selectedPatternIndex >= 0 && selectedPatternIndex < PAT_COUNT)
  {
    if (selectedPatternIndex == PAT_ASPLAYED)
    {
      patternIndices = patternAsPlayed(playedChord.size(), playedChord);
    }
    else
    {
      patternIndices = customPatternFuncs[selectedPatternIndex](orderedChord.size());
    }
  }
  else
  {
    patternIndices = patternUp(orderedChord.size());
  }

  // Apply LOOP mode to patternIndices
  std::vector<uint8_t> patternIndicesFinal = patternIndices;
  if (patternPlaybackMode == LOOP && patternIndices.size() > 2)
  {
    for (int i = patternIndices.size() - 2; i > 0; --i)
      patternIndicesFinal.push_back(patternIndices[i]);
  }
  // Apply REVERSE mode
  if (patternReverse && !patternIndicesFinal.empty())
  {
    std::reverse(patternIndicesFinal.begin(), patternIndicesFinal.end());
  }

  std::vector<uint8_t> playingChord;
  if (patternSmooth && octaveRange != 0 && !patternIndicesFinal.empty())
  {
    // SMOOTH: deduplicate last note of one octave and first note of next octave if equal
    int octStart, octEnd, octStep;
    if (octaveRange > 0)
    {
      octStart = 0;
      octEnd = octaveRange;
      octStep = 1;
    }
    else if (octaveRange < 0)
    {
      octStart = octaveRange;
      octEnd = 0;
      octStep = 1;
    }
    else
    {
      octStart = 0;
      octEnd = 0;
      octStep = 1;
    }
    int prevNote = -1;
    bool first = true;
    for (int oct = octStart; (octStep > 0) ? (oct <= octEnd) : (oct >= octEnd); oct += octStep)
    {
      for (size_t i = 0; i < patternIndicesFinal.size(); ++i)
      {
        uint8_t idx = patternIndicesFinal[i];
        int note = (selectedPatternIndex == PAT_ASPLAYED)
                       ? (idx < playedChord.size() ? playedChord[idx] + 12 * oct : -1)
                       : (idx < orderedChord.size() ? orderedChord[idx] + 12 * oct : -1);
        if (note < 0 || note > 127)
          continue;
        if (!first && note == prevNote)
          continue; // skip duplicate at octave boundary
        playingChord.push_back(note);
        prevNote = note;
        first = false;
      }
    }
  }
  else
  {
    // Normal (non-smooth) playingChord construction
    for (int oct = -abs(octaveRange); oct <= abs(octaveRange); ++oct)
    {
      if ((octaveRange >= 0 && oct < 0) || (octaveRange < 0 && oct > 0))
        continue;
      for (uint8_t idx : patternIndicesFinal)
      {
        if (selectedPatternIndex == PAT_ASPLAYED)
        {
          if (idx < playedChord.size())
          {
            int shifted = playedChord[idx] + 12 * oct;
            if (shifted >= 0 && shifted <= 127)
              playingChord.push_back(shifted);
          }
        }
        else
        {
          if (idx < orderedChord.size())
          {
            int shifted = orderedChord[idx] + 12 * oct;
            if (shifted >= 0 && shifted <= 127)
              playingChord.push_back(shifted);
          }
        }
      }
    }
  }

  // --- Apply note bias based on noteBalancePercent ---
  applyNoteBiasToChord(playingChord, noteBalancePercent);

  // --- Build stepNotes for random chord steps ---
  // stepNotes: The notes (possibly chords) to be played at each arpeggiator step, after all processing.
  std::vector<StepNotes> stepNotes;
  buildRandomChordSteps(stepNotes, playingChord, playedChord, randomChordPercent);

  // --- Arpeggiator timing and note scheduling ---
  static int timingOffset = 0;
  static unsigned long nextNoteTime = 0;

  unsigned long noteLengthMs = arpInterval * noteLengthPercent / 100;
  unsigned long randomizedNoteLengthMs = getRandomizedNoteLength(noteLengthMs);

  if (nextNoteTime == 0)
    nextNoteTime = now;

  uint8_t velocityToSend = noteVelocity;

  // --- Note scheduling: play next note/chord if ready ---
  static std::vector<uint8_t> notesOn;
  if (!noteOnActive && !stepNotes.empty() && now >= nextNoteTime)
  {
    size_t chordSize = stepNotes.size();
    size_t noteIndex = currentNoteIndex % chordSize;
    notesOn = stepNotes[noteIndex].notes;

    // --- Rhythm velocity calculation using pattern generator ---
    std::vector<uint8_t> rhythmPatternIndices = customPatternFuncs[selectedRhythmPattern](chordSize);
    // Invert mapping: 0 is loudest (1.0), max is softest (0.1)
    float rhythmMult = 1.0f;
    if (!rhythmPatternIndices.empty())
    {
      uint8_t minIdx = *std::min_element(rhythmPatternIndices.begin(), rhythmPatternIndices.end());
      uint8_t maxIdx = *std::max_element(rhythmPatternIndices.begin(), rhythmPatternIndices.end());
      uint8_t idx = rhythmPatternIndices[noteIndex % rhythmPatternIndices.size()];
      if (maxIdx > minIdx)
      {
        // Inverted: 0 -> 1.0, max -> 0.1
        rhythmMult = 1.0f - 0.9f * (float)(idx - minIdx) / (float)(maxIdx - minIdx);
        rhythmMult = std::max(0.1f, rhythmMult); // Clamp to at least 0.1
      }
      else
      {
        rhythmMult = 1.0f;
      }
    }
    uint8_t rhythmVelocity = constrain((int)(noteVelocity * rhythmMult), 64, 127);

    // Send all notes in this step (chord or single note)
    for (uint8_t n : notesOn)
    {
      int transposedNote = constrain(n + 12 * transpose, 0, 127);
      uint8_t v = rhythmVelocity;
      if (velocityDynamicsPercent > 0)
      {
        int maxAdjustment = (v * velocityDynamicsPercent) / 100;
        v = constrain(v - random(0, maxAdjustment + 1), 64, 127);
      }
      sendNoteOn(transposedNote, v);
    }

    timingOffset = (timingHumanize ? getTimingHumanizeOffset(noteLengthMs) : 0);
    noteOnStartTime = now + timingOffset;
    noteOnActive = true;
    nextNoteTime += arpInterval;
  }

  // Send note off after note duration for all notes in the step
  if (noteOnActive && now >= noteOnStartTime + randomizedNoteLengthMs)
  {
    for (uint8_t n : notesOn)
    {
      int transposedNote = constrain(n + 12 * transpose, 0, 127);
      sendNoteOff(transposedNote);
    }
    noteOnActive = false;
    if (++noteRepeatCounter >= noteRepeat)
    {
      noteRepeatCounter = 0;
      currentNoteIndex = (currentNoteIndex + 1) % stepNotes.size();
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
  static EncoderMode lastMode = encoderMode;
  static int lastUseVelocityDynamics = velocityDynamicsPercent;
  static bool lastTimingHumanize = timingHumanize;
  static int lastTimingHumanizePercent = timingHumanizePercent;
  static int lastNoteLengthRandomizePercent = noteLengthRandomizePercent;
  static int lastNoteBalancePercent = noteBalancePercent;
  static int lastRandomChordPercent = randomChordPercent;
  static int lastRhythmPattern = selectedRhythmPattern;
  static int lastNoteRangeShift = noteRangeShift;
  static int lastNoteRangeStretch = noteRangeStretch;

  printIfChanged("BPM: ", lastBPM, bpm);
  printIfChanged("Note Length %: ", lastLength, noteLengthPercent);
  printIfChanged("Velocity: ", lastVelocity, noteVelocity);
  printIfChanged("Octave Range: ", lastOctave, octaveRange);
  printIfChanged("Notes Per Beat: ", lastResolutionIndex, notesPerBeatIndex);
  printIfChanged("Note Repeat: ", lastNoteRepeat, noteRepeat);
  printIfChanged("Transpose: ", lastTranspose, transpose);
  printIfChanged("Velocity Dynamics Percent: ", lastUseVelocityDynamics, velocityDynamicsPercent);
  printIfChanged("Timing Humanize Percent: ", lastTimingHumanizePercent, timingHumanizePercent);
  printIfChanged("Note Length Randomize Percent: ", lastNoteLengthRandomizePercent, noteLengthRandomizePercent);
  printIfChanged("Note Balance Percent: ", lastNoteBalancePercent, noteBalancePercent);
  printIfChanged("Random Chord Percent: ", lastRandomChordPercent, randomChordPercent);
  printIfChanged("Rhythm Pattern: ", lastRhythmPattern, selectedRhythmPattern);
  printIfChanged("Range Shift: ", lastNoteRangeShift, noteRangeShift);
  printIfChanged("Range Stretch: ", lastNoteRangeStretch, noteRangeStretch);

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
    case MODE_PATTERN_PLAYBACK:
      Serial.println("Pattern Playback Mode");
      break;
    case MODE_REVERSE:
      Serial.println("Pattern Reverse");
      break;
    case MODE_SMOOTH:
      Serial.println("Pattern Smooth");
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
    case MODE_RANDOM_CHORD:
      Serial.println("Random Chord Percent");
      break;
    case MODE_RHYTHM:
      Serial.println("Rhythm Pattern");
      break;
    case MODE_RANGE:
      Serial.println("Range Shift");
      break;
    case MODE_STRETCH:
      Serial.println("Range Stretch");
      break;
    }
    lastMode = encoderMode;
  }
}