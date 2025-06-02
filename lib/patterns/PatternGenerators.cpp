#include "PatternGenerators.h"
#include <algorithm>
#include <cstdlib>

const char *customPatternNames[PAT_COUNT] = {
    "Up", "Down", "Up-Down", "Down-Up", "Outer-In", "Inward Bounce", "Zigzag", "Spiral", "Mirror", "Saw", "Saw Reverse",
    "Bounce", "Reverse Bounce", "Ladder", "Skip Up", "Jump Step", "Crossover", "Random", "Even-Odd", "Odd-Even",
    "Edge Loop", "Center Bounce", "Up Double", "Skip Reverse", "Snake", "Pendulum", "Asymmetric Loop", "Short Long",
    "Backward Jump", "Inside Bounce", "Staggered Rise", "patternUpDownHalf", "As Played"};

std::vector<uint8_t> patternUp(int n) { std::vector<uint8_t> v(n); for (int i = 0; i < n; ++i) v[i] = i; return v; }
std::vector<uint8_t> patternDown(int n) { std::vector<uint8_t> v(n); for (int i = 0; i < n; ++i) v[i] = n - 1 - i; return v; }
std::vector<uint8_t> patternUpDown(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) v.push_back(i); for (int i = n - 2; i > 0; --i) v.push_back(i); return v; }
std::vector<uint8_t> patternDownUp(int n) { std::vector<uint8_t> v; for (int i = n - 1; i >= 0; --i) v.push_back(i); for (int i = 1; i < n - 1; ++i) v.push_back(i); return v; }
std::vector<uint8_t> patternOuterIn(int n) { std::vector<uint8_t> v; int left = 0, right = n - 1; while (left <= right) { v.push_back(left); if (left != right) v.push_back(right); ++left; --right; } return v; }
std::vector<uint8_t> patternInwardBounce(int n) { std::vector<uint8_t> v; int mid = (n - 1) / 2; v.push_back(mid); for (int offset = 1; mid - offset >= 0 || mid + offset < n; ++offset) { if (mid - offset >= 0) v.push_back(mid - offset); if (mid + offset < n) v.push_back(mid + offset); } return v; }
std::vector<uint8_t> patternZigzag(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; i += 2) v.push_back(i); for (int i = 1; i < n; i += 2) v.push_back(i); return v; }
std::vector<uint8_t> patternSpiral(int n) { std::vector<uint8_t> v; int left = 0, right = n - 1, i = 0; while (left <= right) { if (i % 2 == 0) v.push_back(left++); else v.push_back(right--); ++i; } return v; }
std::vector<uint8_t> patternMirror(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) v.push_back(i); for (int i = n - 2; i >= 0; --i) v.push_back(i); return v; }
std::vector<uint8_t> patternSaw(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) v.push_back(i); v.push_back(0); return v; }
std::vector<uint8_t> patternSawReverse(int n) { std::vector<uint8_t> v; for (int i = n - 1; i >= 0; --i) v.push_back(i); v.push_back(n - 1); return v; }
std::vector<uint8_t> patternBounce(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) { v.push_back(0); v.push_back(n - 1 - i); } v.push_back(0); return v; }
std::vector<uint8_t> patternReverseBounce(int n) { std::vector<uint8_t> v; for (int i = n - 1; i >= 0; --i) { v.push_back(n - 1); v.push_back(i); } v.push_back(n - 1); return v; }
std::vector<uint8_t> patternLadder(int n) { std::vector<uint8_t> v; for (int i = 1; i <= n; ++i) { v.push_back(0); v.push_back(i - 1); } return v; }
std::vector<uint8_t> patternSkipUp(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; i += 2) v.push_back(i); for (int i = 1; i < n; i += 2) v.push_back(i); return v; }
std::vector<uint8_t> patternJumpStep(int n) { std::vector<uint8_t> v; int half = (n + 1) / 2; for (int i = 0; i < half; ++i) { v.push_back(i); if (i + half < n) v.push_back(i + half); } return v; }
std::vector<uint8_t> patternCrossover(int n) { std::vector<uint8_t> v; int left = 1, right = n - 2; v.push_back(1 % n); v.push_back((n - 2 + n) % n); v.push_back(0); v.push_back(n - 1); for (int i = 2; left < right; ++i, ++left, --right) { v.push_back(left % n); v.push_back((right + n) % n); } if (n % 2 == 1) v.push_back(n / 2); return v; }
std::vector<uint8_t> patternRandom(int n) { std::vector<uint8_t> v(n); for (int i = 0; i < n; ++i) v[i] = i; std::random_shuffle(v.begin(), v.end()); return v; }
std::vector<uint8_t> patternEvenOdd(int n) { std::vector<uint8_t> v; for (int i = 1; i < n; i += 2) v.push_back(i); for (int i = 0; i < n; i += 2) v.push_back(i); return v; }
std::vector<uint8_t> patternOddEven(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; i += 2) v.push_back(i); for (int i = 1; i < n; i += 2) v.push_back(i); return v; }
std::vector<uint8_t> patternEdgeLoop(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) { v.push_back(0); v.push_back(n - 1); } return v; }
std::vector<uint8_t> patternCenterBounce(int n) { std::vector<uint8_t> v; int mid = n / 2; for (int i = 0; i < n; ++i) { v.push_back(mid); v.push_back(i); } return v; }
std::vector<uint8_t> patternUpDouble(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) { v.push_back(i); v.push_back(i); } return v; }
std::vector<uint8_t> patternSkipReverse(int n) { std::vector<uint8_t> v; for (int i = n - 1; i >= 0; i -= 2) v.push_back(i); for (int i = n - 2; i >= 0; i -= 2) v.push_back(i); return v; }
std::vector<uint8_t> patternSnake(int n) { std::vector<uint8_t> v; for (int i = 0; i < n - 1; ++i) { v.push_back(i); v.push_back(i + 1); } v.push_back(n - 1); return v; }
std::vector<uint8_t> patternPendulum(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) v.push_back(i); for (int i = n - 2; i > 0; --i) v.push_back(i); return v; }
std::vector<uint8_t> patternAsymmetricLoop(int n) { std::vector<uint8_t> v; v.push_back(0); for (int i = 2; i <= n; ++i) { v.push_back(i % 2 == 0 ? i - 1 : i - 2); } return v; }
std::vector<uint8_t> patternShortLong(int n) { std::vector<uint8_t> v; for (int i = 1; i <= n; ++i) { v.push_back(0); v.push_back(i - 1); } return v; }
std::vector<uint8_t> patternBackwardJump(int n) { std::vector<uint8_t> v; for (int i = n - 1; i >= 0; i -= 3) v.push_back(i); for (int i = n - 2; i >= 0; i -= 3) v.push_back(i); return v; }
std::vector<uint8_t> patternInsideBounce(int n) { std::vector<uint8_t> v; int left = 1, right = n - 2; while (left <= right) { v.push_back(left); if (left != right) v.push_back(right); ++left; --right; } return v; }
std::vector<uint8_t> patternStaggeredRise(int n) { std::vector<uint8_t> v; for (int i = 0; i < n; i += 2) v.push_back(i); for (int i = 1; i < n; i += 2) v.push_back(i); return v; }
std::vector<uint8_t> patternUpDownHalf(int n) {std::vector<uint8_t> v; int half = n / 2; for (int i = 0; i < half; ++i) v.push_back(i); for (int i = n - 1; i >= half; --i) v.push_back(i); return v; }
std::vector<uint8_t> patternAsPlayed(int n, const std::vector<uint8_t> &playedOrder) { std::vector<uint8_t> v; for (int i = 0; i < n; ++i) v.push_back(i); return v; }

PatternGen customPatternFuncs[PAT_COUNT - 1] = {
    patternUp, patternDown, patternUpDown, patternDownUp, patternOuterIn, patternInwardBounce, patternZigzag, patternSpiral,
    patternMirror, patternSaw, patternSawReverse, patternBounce, patternReverseBounce, patternLadder, patternSkipUp,
    patternJumpStep, patternCrossover, patternRandom, patternEvenOdd, patternOddEven, patternEdgeLoop, patternCenterBounce,
    patternUpDouble, patternSkipReverse, patternSnake, patternPendulum, patternAsymmetricLoop, patternShortLong,
    patternBackwardJump, patternInsideBounce, patternStaggeredRise, patternUpDownHalf};
