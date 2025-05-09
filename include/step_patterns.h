#pragma once

#include <vector>
#include <algorithm>
#include <Arduino.h>
#include <EEPROM.h>

/*
// --- STEP PATTERN CONSTANTS ---
// These must be defined before including this header in your main.cpp
#ifndef MIN_STEPS
#define MIN_STEPS 2
#endif
#ifndef MAX_STEPS
#define MAX_STEPS 6
#endif
*/

// Calculate the number of permutations for n steps (n!)
constexpr int factorial(int n)
{
    return (n <= 1) ? 1 : n * factorial(n - 1);
}

// --- Helper: Calculate required EEPROM size for all step patterns ---
void printEEPROMUsageForStepPatterns()
{
    int totalBytes = 0;
    for (int n = MIN_STEPS; n <= MAX_STEPS; ++n)
    {
        int numPatterns = factorial(n);
        int bytesForGroup = numPatterns * n;
        Serial.printf("Patterns for %d steps: %d patterns, %d bytes\n", n, numPatterns, bytesForGroup);
        totalBytes += bytesForGroup;
    }
    Serial.printf("Total EEPROM bytes required for all step patterns: %d\n", totalBytes);
    Serial.printf("Configured EEPROM_SIZE: %d\n", EEPROM_SIZE);
    if (totalBytes + 1 > EEPROM_SIZE)
    {
        Serial.println("WARNING: EEPROM_SIZE is NOT large enough for all step patterns!");
    }
    else
    {
        Serial.println("EEPROM_SIZE is sufficient for all step patterns.");
    }
}

// Utility function to generate all permutations for n steps
inline void generateStepPermutations(int n, std::vector<std::vector<int>> &out)
{
    out.clear();
    std::vector<int> perm(n);
    for (int i = 0; i < n; ++i)
        perm[i] = i;
    do
    {
        out.push_back(perm);
    } while (std::next_permutation(perm.begin(), perm.end()));
}

// Build all groups for 2 to 6 steps as a vector of vector of vector<int>
inline std::vector<std::vector<std::vector<int>>> buildAllStepPatternGroups()
{
    std::vector<std::vector<std::vector<int>>> groups;
    for (int n = MIN_STEPS; n <= MAX_STEPS; ++n)
    {
        std::vector<std::vector<int>> group;
        generateStepPermutations(n, group);
        groups.push_back(group);
    }
    return groups;
}

// EEPROM layout: patterns are stored sequentially for each group (2..6 steps)
// We add an address table at the start of EEPROM to store the start address of each pattern group.
// The address table is written once during pattern generation and used for fast lookup.

constexpr int EEPROM_ADDR_TABLE_START = 0; // Start address for address table
constexpr int MAX_GROUPS = MAX_STEPS - MIN_STEPS + 1;

// Helper to compute factorial at runtime
inline int factorial_rt(int n)
{
    int r = 1;
    for (int i = 2; i <= n; ++i)
        r *= i;
    return r;
}

// Write all step patterns and address table to EEPROM
inline void writeAllStepPatternsToEEPROM()
{
    int addrTable[MAX_GROUPS];
    int addr = EEPROM_ADDR_TABLE_START + MAX_GROUPS * sizeof(int); // Reserve space for address table

    // Write patterns and record group start addresses
    for (int group = 0, n = MIN_STEPS; n <= MAX_STEPS; ++n, ++group)
    {
        addrTable[group] = addr;
        std::vector<int> perm(n);
        for (int i = 0; i < n; ++i)
            perm[i] = i;
        do
        {
            for (int i = 0; i < n; ++i)
            {
                EEPROM.write(addr++, perm[i]);
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
    }

    // Write address table at the start of EEPROM
    int tableAddr = EEPROM_ADDR_TABLE_START;
    for (int group = 0; group < MAX_GROUPS; ++group)
    {
        int val = addrTable[group];
        for (int b = 0; b < 4; ++b)
        {
            EEPROM.write(tableAddr++, (val >> (b * 8)) & 0xFF);
        }
    }
    EEPROM.commit();
}

// Helper to read group start address from EEPROM address table
inline int readPatternGroupStartAddr(int chordSize)
{
    int group = chordSize - MIN_STEPS;
    int addr = EEPROM_ADDR_TABLE_START + group * sizeof(int);
    int val = 0;
    for (int b = 0; b < 4; ++b)
    {
        val |= (EEPROM.read(addr + b) << (b * 8));
    }
    return val;
}

// Read a single pattern directly from EEPROM using the address table
inline void readPatternFromEEPROM(int chordSize, int patternIndex, std::vector<uint8_t> &pattern)
{
    if (chordSize < MIN_STEPS || chordSize > MAX_STEPS)
    {
        pattern.clear();
        return;
    }
    int numPatterns = factorial_rt(chordSize);
    if (patternIndex < 0 || patternIndex >= numPatterns)
    {
        pattern.clear();
        return;
    }
    int groupStart = readPatternGroupStartAddr(chordSize);
    int addr = groupStart + patternIndex * chordSize;
    pattern.clear();
    for (int i = 0; i < chordSize; ++i)
    {
        pattern.push_back(EEPROM.read(addr + i));
    }
}

// Read all step patterns from EEPROM into a nested vector structure
inline void readAllStepPatternsFromEEPROM(std::vector<std::vector<std::vector<uint8_t>>> &allGroups)
{
    allGroups.clear();
    int addr = EEPROM_ADDR_TABLE_START + MAX_GROUPS * sizeof(int); // Skip address table
    for (int n = MIN_STEPS; n <= MAX_STEPS; ++n)
    {
        int numPatterns = factorial(n);
        std::vector<std::vector<uint8_t>> group;
        for (int p = 0; p < numPatterns; ++p)
        {
            std::vector<uint8_t> pattern;
            for (int i = 0; i < n; ++i)
            {
                pattern.push_back(EEPROM.read(addr++));
            }
            group.push_back(pattern);
        }
        allGroups.push_back(group);
    }
}

// Example usage:
// EEPROM.begin(EEPROM_SIZE); // Call in setup, EEPROM_SIZE must be large enough
// writeAllStepPatternsToEEPROM(); // Only once, to initialize EEPROM
// std::vector<std::vector<std::vector<uint8_t>>> allGroups;
// readAllStepPatternsFromEEPROM(allGroups);
// const std::vector<uint8_t>& pattern = allGroups[1][3]; // 3-step group (index 1), 4th pattern
