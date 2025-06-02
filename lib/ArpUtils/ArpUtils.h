#ifndef ARP_UTILS_H
#define ARP_UTILS_H

extern const char *modeNames[21];
extern const char *meterTypes[3];
extern const char *patternLoopOptions[2];
extern const char *patternReverseOptions[2];
extern const char *patternSmoothOptions[2];
extern const unsigned char ttable[6][4];
extern volatile unsigned char state;

template <typename T>
void printIfChanged(const char *label, T &lastValue, T currentValue, T printValue);

#endif // ARP_UTILS_H
