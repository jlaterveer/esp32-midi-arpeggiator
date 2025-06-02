#pragma once
#include <vector>
#include <cstdint>

// Enum for all custom patterns
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
  PAT_UPDOWNHALF,
  PAT_ASPLAYED,
  PAT_COUNT // must be last
};

extern const char *customPatternNames[PAT_COUNT];

std::vector<uint8_t> patternUp(int n);
std::vector<uint8_t> patternDown(int n);
std::vector<uint8_t> patternUpDown(int n);
std::vector<uint8_t> patternDownUp(int n);
std::vector<uint8_t> patternOuterIn(int n);
std::vector<uint8_t> patternInwardBounce(int n);
std::vector<uint8_t> patternZigzag(int n);
std::vector<uint8_t> patternSpiral(int n);
std::vector<uint8_t> patternMirror(int n);
std::vector<uint8_t> patternSaw(int n);
std::vector<uint8_t> patternSawReverse(int n);
std::vector<uint8_t> patternBounce(int n);
std::vector<uint8_t> patternReverseBounce(int n);
std::vector<uint8_t> patternLadder(int n);
std::vector<uint8_t> patternSkipUp(int n);
std::vector<uint8_t> patternJumpStep(int n);
std::vector<uint8_t> patternCrossover(int n);
std::vector<uint8_t> patternRandom(int n);
std::vector<uint8_t> patternEvenOdd(int n);
std::vector<uint8_t> patternOddEven(int n);
std::vector<uint8_t> patternEdgeLoop(int n);
std::vector<uint8_t> patternCenterBounce(int n);
std::vector<uint8_t> patternUpDouble(int n);
std::vector<uint8_t> patternSkipReverse(int n);
std::vector<uint8_t> patternSnake(int n);
std::vector<uint8_t> patternPendulum(int n);
std::vector<uint8_t> patternAsymmetricLoop(int n);
std::vector<uint8_t> patternShortLong(int n);
std::vector<uint8_t> patternBackwardJump(int n);
std::vector<uint8_t> patternInsideBounce(int n);
std::vector<uint8_t> patternStaggeredRise(int n);
std::vector<uint8_t> patternUpDownHalf(int n);
std::vector<uint8_t> patternAsPlayed(int n, const std::vector<uint8_t> &playedOrder);

typedef std::vector<uint8_t> (*PatternGen)(int);
extern PatternGen customPatternFuncs[PAT_COUNT - 1];
