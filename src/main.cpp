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
  MODE_PATTERN_PLAYBACK, // <-- Add this line
  MODE_RESOLUTION,
  MODE_REPEAT,
  MODE_TRANSPOSE,
  MODE_DYNAMICS,
  MODE_HUMANIZE,
  MODE_LENGTH_RANDOMIZE,
  MODE_BALANCE
};
EncoderMode encoderMode = MODE_BPM;
const int encoderModeSize = 13; // <-- Update to match new mode count

// --- STRAIGHT/LOOP mode for pattern playback ---
enum PatternPlaybackMode
{
  STRAIGHT,
  LOOP
};
PatternPlaybackMode patternPlaybackMode = STRAIGHT;

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

// --- Custom pattern generators ---
// All patterns use 0-based indices for compatibility with C++ arrays

std::vector<uint8_t> patternUp(int n)
{
  std::vector<uint8_t> v(n);
  for (int i = 0; i < n; ++i)
    v[i] = i;
  return v;
}

std::vector<uint8_t> patternDown(int n)
{
  std::vector<uint8_t> v(n);
  for (int i = 0; i < n; ++i)
    v[i] = n - 1 - i;
  return v;
}

std::vector<uint8_t> patternUpDown(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
    v.push_back(i);
  for (int i = n - 2; i > 0; --i)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternDownUp(int n)
{
  std::vector<uint8_t> v;
  for (int i = n - 1; i >= 0; --i)
    v.push_back(i);
  for (int i = 1; i < n - 1; ++i)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternOuterIn(int n)
{
  std::vector<uint8_t> v;
  int left = 0, right = n - 1;
  while (left <= right)
  {
    v.push_back(left);
    if (left != right)
      v.push_back(right);
    ++left;
    --right;
  }
  return v;
}

std::vector<uint8_t> patternInwardBounce(int n)
{
  std::vector<uint8_t> v;
  int mid = (n - 1) / 2;
  v.push_back(mid);
  for (int offset = 1; mid - offset >= 0 || mid + offset < n; ++offset)
  {
    if (mid - offset >= 0)
      v.push_back(mid - offset);
    if (mid + offset < n)
      v.push_back(mid + offset);
  }
  return v;
}

std::vector<uint8_t> patternZigzag(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; i += 2)
    v.push_back(i);
  for (int i = 1; i < n; i += 2)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternSpiral(int n)
{
  std::vector<uint8_t> v;
  int left = 0, right = n - 1, i = 0;
  while (left <= right)
  {
    if (i % 2 == 0)
      v.push_back(left++);
    else
      v.push_back(right--);
    ++i;
  }
  return v;
}

std::vector<uint8_t> patternMirror(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
    v.push_back(i);
  for (int i = n - 2; i >= 0; --i)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternSaw(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
    v.push_back(i);
  v.push_back(0);
  return v;
}

std::vector<uint8_t> patternSawReverse(int n)
{
  std::vector<uint8_t> v;
  for (int i = n - 1; i >= 0; --i)
    v.push_back(i);
  v.push_back(n - 1);
  return v;
}

std::vector<uint8_t> patternBounce(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
  {
    v.push_back(0);
    v.push_back(n - 1 - i);
  }
  v.push_back(0);
  return v;
}

std::vector<uint8_t> patternReverseBounce(int n)
{
  std::vector<uint8_t> v;
  for (int i = n - 1; i >= 0; --i)
  {
    v.push_back(n - 1);
    v.push_back(i);
  }
  v.push_back(n - 1);
  return v;
}

std::vector<uint8_t> patternLadder(int n)
{
  std::vector<uint8_t> v;
  for (int i = 1; i <= n; ++i)
  {
    v.push_back(0);
    v.push_back(i - 1);
  }
  return v;
}

std::vector<uint8_t> patternSkipUp(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; i += 2)
    v.push_back(i);
  for (int i = 1; i < n; i += 2)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternJumpStep(int n)
{
  std::vector<uint8_t> v;
  int half = (n + 1) / 2;
  for (int i = 0; i < half; ++i)
  {
    v.push_back(i);
    if (i + half < n)
      v.push_back(i + half);
  }
  return v;
}

std::vector<uint8_t> patternCrossover(int n)
{
  std::vector<uint8_t> v;
  int left = 1, right = n - 2;
  v.push_back(1 % n);
  v.push_back((n - 2 + n) % n);
  v.push_back(0);
  v.push_back(n - 1);
  for (int i = 2; left < right; ++i, ++left, --right)
  {
    v.push_back(left % n);
    v.push_back((right + n) % n);
  }
  if (n % 2 == 1)
    v.push_back(n / 2);
  return v;
}

std::vector<uint8_t> patternRandom(int n)
{
  std::vector<uint8_t> v(n);
  for (int i = 0; i < n; ++i)
    v[i] = i;
  std::random_shuffle(v.begin(), v.end());
  return v;
}

std::vector<uint8_t> patternEvenOdd(int n)
{
  std::vector<uint8_t> v;
  for (int i = 1; i < n; i += 2)
    v.push_back(i);
  for (int i = 0; i < n; i += 2)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternOddEven(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; i += 2)
    v.push_back(i);
  for (int i = 1; i < n; i += 2)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternEdgeLoop(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
  {
    v.push_back(0);
    v.push_back(n - 1);
  }
  return v;
}

std::vector<uint8_t> patternCenterBounce(int n)
{
  std::vector<uint8_t> v;
  int mid = n / 2;
  for (int i = 0; i < n; ++i)
  {
    v.push_back(mid);
    v.push_back(i);
  }
  return v;
}

std::vector<uint8_t> patternUpDouble(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
  {
    v.push_back(i);
    v.push_back(i);
  }
  return v;
}

std::vector<uint8_t> patternSkipReverse(int n)
{
  std::vector<uint8_t> v;
  for (int i = n - 1; i >= 0; i -= 2)
    v.push_back(i);
  for (int i = n - 2; i >= 0; i -= 2)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternSnake(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n - 1; ++i)
  {
    v.push_back(i);
    v.push_back(i + 1);
  }
  v.push_back(n - 1);
  return v;
}

std::vector<uint8_t> patternPendulum(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
    v.push_back(i);
  for (int i = n - 2; i > 0; --i)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternAsymmetricLoop(int n)
{
  std::vector<uint8_t> v;
  v.push_back(0);
  for (int i = 2; i <= n; ++i)
  {
    v.push_back(i % 2 == 0 ? i - 1 : i - 2);
  }
  return v;
}

std::vector<uint8_t> patternShortLong(int n)
{
  std::vector<uint8_t> v;
  for (int i = 1; i <= n; ++i)
  {
    v.push_back(0);
    v.push_back(i - 1);
  }
  return v;
}

std::vector<uint8_t> patternBackwardJump(int n)
{
  std::vector<uint8_t> v;
  for (int i = n - 1; i >= 0; i -= 3)
    v.push_back(i);
  for (int i = n - 2; i >= 0; i -= 3)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternInsideBounce(int n)
{
  std::vector<uint8_t> v;
  int left = 1, right = n - 2;
  while (left <= right)
  {
    v.push_back(left);
    if (left != right)
      v.push_back(right);
    ++left;
    --right;
  }
  return v;
}

std::vector<uint8_t> patternStaggeredRise(int n)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; i += 2)
    v.push_back(i);
  for (int i = 1; i < n; i += 2)
    v.push_back(i);
  return v;
}

std::vector<uint8_t> patternAsPlayed(int n, const std::vector<uint8_t> &playedOrder)
{
  std::vector<uint8_t> v;
  for (int i = 0; i < n; ++i)
    v.push_back(i);
  return v;
}

// --- Pattern selection state for MODE_PATTERN ---
enum CustomPattern
{
  PAT_UP,
  PAT_DOWN,
  PAT_UPDOWN,
  PAT_DOWNUP,
  PAT_OUTERIN,
  PAT_INWARDBOUNCE,
  PAT_ZIGZAG,
  PAT_SPIRAL,
  PAT_MIRROR,
  PAT_SAW,
  PAT_SAWREVERSE,
  PAT_BOUNCE,
  PAT_REVERSEBOUNCE,
  PAT_LADDER,
  PAT_SKIPUP,
  PAT_JUMPSTEP,
  PAT_CROSSOVER,
  PAT_RANDOM,
  PAT_EVENODD,
  PAT_ODDEVEN,
  PAT_EDGELOOP,
  PAT_CENTERBOUNCE,
  PAT_UPDOUBLE,
  PAT_SKIPREVERSE,
  PAT_SNAKE,
  PAT_PENDULUM,
  PAT_ASYMMETRICLOOP,
  PAT_SHORTLONG,
  PAT_BACKWARDJUMP,
  PAT_INSIDEBOUNCE,
  PAT_STAGGEREDRISE,
  PAT_ASPLAYED,
  PAT_COUNT // must be last
};

const char *customPatternNames[PAT_COUNT] = {
    "Up", "Down", "Up-Down", "Down-Up", "Outer-In", "Inward Bounce", "Zigzag", "Spiral", "Mirror", "Saw", "Saw Reverse",
    "Bounce", "Reverse Bounce", "Ladder", "Skip Up", "Jump Step", "Crossover", "Random", "Even-Odd", "Odd-Even",
    "Edge Loop", "Center Bounce", "Up Double", "Skip Reverse", "Snake", "Pendulum", "Asymmetric Loop", "Short Long",
    "Backward Jump", "Inside Bounce", "Staggered Rise", "As Played"};

// Pattern generator function pointers (except AsPlayed, which is handled specially)
typedef std::vector<uint8_t> (*PatternGen)(int);
PatternGen customPatternFuncs[PAT_COUNT - 1] = {
    patternUp, patternDown, patternUpDown, patternDownUp, patternOuterIn, patternInwardBounce, patternZigzag, patternSpiral,
    patternMirror, patternSaw, patternSawReverse, patternBounce, patternReverseBounce, patternLadder, patternSkipUp,
    patternJumpStep, patternCrossover, patternRandom, patternEvenOdd, patternOddEven, patternEdgeLoop, patternCenterBounce,
    patternUpDouble, patternSkipReverse, patternSnake, patternPendulum, patternAsymmetricLoop, patternShortLong,
    patternBackwardJump, patternInsideBounce, patternStaggeredRise};

// --- Pattern selection index for MODE_PATTERN ---
int selectedPatternIndex = 0;

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

  // --- Add encoder switch for STRAIGHT/LOOP toggle ---
  // Use encoderSW long press to toggle patternPlaybackMode
  static unsigned long encoderSWPressTime = 0;
  static bool encoderSWPrev = false;
  bool encoderSWNow = !digitalRead(encoderSW);
  if (encoderSWNow && !encoderSWPrev)
  {
    encoderSWPressTime = millis();
  }
  if (!encoderSWNow && encoderSWPrev)
  {
    if (millis() - encoderSWPressTime > 500) // long press
    {
      patternPlaybackMode = (patternPlaybackMode == STRAIGHT) ? LOOP : STRAIGHT;
      Serial.print("Pattern Playback Mode: ");
      Serial.println(patternPlaybackMode == STRAIGHT ? "STRAIGHT" : "LOOP");
    }
  }
  encoderSWPrev = encoderSWNow;

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
      selectedPatternIndex += delta;
      if (selectedPatternIndex < 0)
        selectedPatternIndex = PAT_COUNT - 1;
      if (selectedPatternIndex >= PAT_COUNT)
        selectedPatternIndex = 0;
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
  std::vector<uint8_t> playedChord = baseChord;

  // For other patterns, create a sorted, deduplicated chord
  std::vector<uint8_t> orderedChord = playedChord;
  std::sort(orderedChord.begin(), orderedChord.end());

  // --- Build the playingChord with octave shifts and no duplicates using selected pattern
  std::vector<uint8_t> patternIndices;
  if (encoderMode == MODE_PATTERN && selectedPatternIndex >= 0 && selectedPatternIndex < PAT_COUNT)
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

  std::vector<uint8_t> playingChord;
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
      chordNotesOn = true;
      noteOnActive = true;
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
    case MODE_PATTERN_PLAYBACK:
      Serial.println("Pattern Playback Mode");
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