#include <Arduino.h>
#include <vector>
// #include <digitalWriteFast.h>

// --- CONFIGURATION ---
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
const unsigned char ttable[6][4] = {
    {0x3, 0x2, 0x1, 0x0}, {0x23, 0x0, 0x1, 0x0}, {0x13, 0x2, 0x0, 0x0}, {0x3, 0x5, 0x4, 0x0}, {0x3, 0x3, 0x4, 0x10}, {0x3, 0x5, 0x3, 0x20}};
volatile unsigned char state = 0;

void rotary_init()
{
  pinMode(encoder0PinA, INPUT_PULLUP);
  pinMode(encoder0PinB, INPUT_PULLUP);
}
unsigned char rotary_process()
{
  unsigned char pinstate = (digitalRead(encoder0PinA) << 1) | digitalRead(encoder0PinB);
  state = ttable[state & 0xf][pinstate];
  return (state & 0x30);
}

// --- ENCODER MODES ---
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
  MODE_DYNAMICS
};
EncoderMode encoderMode = MODE_BPM;
const int encoderModeSize = 9;

// --- PARAMETERS ---
int bpm = 96;
int noteLengthPercent = 50;
int noteVelocity = 85;
int octaveRange = 0;
int transpose = 0;
bool useVelocityDynamics = false;

const int minOctave = -3, maxOctave = 3;
const int minTranspose = -3, maxTranspose = 3;

const int notesPerBeatOptions[] = {1, 2, 3, 4, 6, 8, 12, 16};
const int notesPerBeatOptionsSize = sizeof(notesPerBeatOptions) / sizeof(notesPerBeatOptions[0]);
int notesPerBeatIndex = 0;
int notesPerBeat = notesPerBeatOptions[notesPerBeatIndex];
int noteRepeat = 1;
int noteRepeatCounter = 0;
unsigned long arpInterval = 60000 / (bpm * notesPerBeat);

// --- PATTERNS ---
enum ArpPattern
{
  UP,
  DOWN,
  TRIANGLE,
  SINE,
  SQUARE,
  RANDOM,
  PLAYED // New pattern
};
ArpPattern currentPattern = UP;
const char *patternNames[] = {"UP", "DOWN", "TRIANGLE", "SINE", "SQUARE", "RANDOM", "PLAYED"};
bool ascending = true;
const uint8_t sineTable[16] = {0, 1, 2, 4, 6, 8, 10, 12, 15, 12, 10, 8, 6, 4, 2, 1};

// --- STATE ---
std::vector<uint8_t> currentChord;
std::vector<uint8_t> tempChord;
uint8_t leadNote = 0;
bool capturingChord = false;
bool chordLatched = false;
size_t currentNoteIndex = 0;
unsigned long lastNoteTime = 0;
bool noteOnActive = false;
unsigned long noteOnStartTime = 0;
uint8_t lastPlayedNote = 0;

// --- LED FLASH STATE ---
unsigned long ledFlashStart = 0;
bool ledFlashing = false;
const unsigned long ledFlashDuration = 100;

// --- MIDI I/O ---

void midiSendByte(uint8_t byte)
{
  Serial2.write(byte);
}
void sendNoteOn(uint8_t note, uint8_t velocity)
{
  midiSendByte(0x90);
  midiSendByte(note);
  midiSendByte(velocity);
}
void sendNoteOff(uint8_t note)
{
  midiSendByte(0x80);
  midiSendByte(note);
  midiSendByte(0);
}

// --- MIDI IN PARSING ---
enum MidiState
{
  WaitingStatus,
  WaitingData1,
  WaitingData2
};
MidiState midiState = WaitingStatus;
uint8_t midiStatus, midiData1;

void insertionSort(std::vector<uint8_t> &arr)
{
  for (size_t i = 1; i < arr.size(); ++i)
  {
    uint8_t key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key)
      arr[j + 1] = arr[j--];
    arr[j + 1] = key;
  }
}
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
    // insertionSort(tempChord);
  }
}
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

// --- SETUP ---
void setup()
{
  pinMode(ledBuiltIn, OUTPUT);
  pinMode(clearButtonPin, INPUT_PULLUP);
  pinMode(encoderSW, INPUT_PULLUP);
  rotary_init();

  Serial.begin(115200);                               // Debug
  Serial1.begin(31250, SERIAL_8N1, midiInRxPin, -1);  // MIDI IN
  Serial2.begin(31250, SERIAL_8N1, -1, midiOutTxPin); // MIDI OUT

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
void loop()
{
  unsigned long now = millis();

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
      currentPattern = static_cast<ArpPattern>((currentPattern + delta + 7) % 7);
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
      useVelocityDynamics = !useVelocityDynamics;
      break;
    }
    arpInterval = 60000 / (bpm * notesPerBeat);
  }

  while (Serial1.available())
    readMidiByte(Serial1.read());

  std::vector<uint8_t> baseChord = capturingChord ? tempChord : currentChord;
  std::vector<uint8_t> playingChord;
  for (int oct = -abs(octaveRange); oct <= abs(octaveRange); ++oct)
  {
    if ((octaveRange >= 0 && oct < 0) || (octaveRange < 0 && oct > 0))
      continue;
    for (uint8_t note : baseChord)
    {
      int shifted = note + 12 * oct;
      if (shifted >= 0 && shifted <= 127)
        playingChord.push_back(shifted);
    }
  }

  /*
  Serial.print("Base Chord: ");
  for (uint8_t note : baseChord)
    Serial.print(note), Serial.print(" ");
  Serial.println();

  Serial.print("Playing Chord: ");
  for (uint8_t note : playingChord)
    Serial.print(note), Serial.print(" ");
  Serial.println();
  */

  Serial.print("Current Chord: ");
  for (uint8_t note : currentChord)
    Serial.print(note), Serial.print(" ");
  Serial.println();

  if (!noteOnActive && !playingChord.empty() && now - lastNoteTime >= arpInterval)
  {
    uint8_t noteIndex = 0;
    size_t chordSize = playingChord.size();
    switch (currentPattern)
    {
    case UP:
      noteIndex = currentNoteIndex % chordSize;
      break;
    case DOWN:
      noteIndex = chordSize - 1 - (currentNoteIndex % chordSize);
      break;
    case TRIANGLE:
      if (ascending)
      {
        noteIndex = currentNoteIndex;
        if (noteIndex >= chordSize - 1)
          ascending = false;
      }
      else
      {
        noteIndex = chordSize - 1 - currentNoteIndex;
        if (noteIndex == 0)
          ascending = true;
      }
      break;
    case SINE:
      noteIndex = map(sineTable[currentNoteIndex % 16], 0, 15, 0, chordSize - 1);
      break;
    case SQUARE:
      noteIndex = (currentNoteIndex % 2 == 0) ? 0 : chordSize - 1;
      break;
    case RANDOM:
      noteIndex = random(chordSize);
      break;
    case PLAYED:
      noteIndex = currentNoteIndex % chordSize;
      break;
    }
    Serial.print("Current Note Index: ");
    Serial.println(currentNoteIndex);
    Serial.print("Note Index: ");
    Serial.println(noteIndex);
    int transposedNote = constrain(playingChord[noteIndex] + 12 * transpose, 0, 127);
    lastPlayedNote = transposedNote;
    uint8_t velocityToSend = useVelocityDynamics ? constrain(noteVelocity - random(0, 28), 1, 127) : noteVelocity;
    sendNoteOn(lastPlayedNote, velocityToSend);
    noteOnStartTime = now;
    noteOnActive = true;
    lastNoteTime = now;
  }

  if (noteOnActive && now - noteOnStartTime >= (arpInterval * noteLengthPercent / 100))
  {
    sendNoteOff(lastPlayedNote);
    noteOnActive = false;
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

  if (ledFlashing && now - ledFlashStart >= ledFlashDuration)
  {
    neopixelWrite(ledBuiltIn, 0, 0, 0);
    ledFlashing = false;
  }

  static int lastBPM = bpm, lastLength = noteLengthPercent, lastVelocity = noteVelocity, lastOctave = octaveRange;
  static int lastResolutionIndex = notesPerBeatIndex, lastNoteRepeat = noteRepeat, lastTranspose = transpose;
  static ArpPattern lastPattern = currentPattern;
  static EncoderMode lastMode = encoderMode;
  static bool lastUseVelocityDynamics = useVelocityDynamics;

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
    Serial.print("Pattern: ");
    Serial.println(patternNames[currentPattern]);
    lastPattern = currentPattern;
  }
  if (encoderMode == MODE_DYNAMICS && useVelocityDynamics != lastUseVelocityDynamics)
  {
    Serial.print("Velocity Dynamics: ");
    Serial.println(useVelocityDynamics ? "ON" : "OFF");
    lastUseVelocityDynamics = useVelocityDynamics;
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
      Serial.println("Velocity Dynamics");
      break;
    }
    lastMode = encoderMode;
  }
}