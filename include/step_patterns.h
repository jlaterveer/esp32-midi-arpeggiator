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
constexpr int EEPROM_PATTERN_ADDR = 0; // Start address

// Write all step patterns to EEPROM (call once to initialize, e.g. from setup)
inline void writeAllStepPatternsToEEPROM()
{
    int addr = EEPROM_PATTERN_ADDR;
    for (int n = MIN_STEPS; n <= MAX_STEPS; ++n)
    {
        std::vector<std::vector<int>> group;
        // Generate all permutations for n steps
        std::vector<int> perm(n);
        for (int i = 0; i < n; ++i)
            perm[i] = i;
        do
        {
            // Write each pattern to EEPROM
            for (int i = 0; i < n; ++i)
            {
                EEPROM.write(addr++, perm[i]);
            }
        } while (std::next_permutation(perm.begin(), perm.end()));
    }
    EEPROM.commit();
}

// Read all step patterns from EEPROM into a nested vector structure
inline void readAllStepPatternsFromEEPROM(std::vector<std::vector<std::vector<uint8_t>>> &allGroups)
{
    allGroups.clear();
    int addr = EEPROM_PATTERN_ADDR;
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

// "inline void" vs "void":
// - "inline" is a suggestion to the compiler to insert the function's code at each call site, instead of making a regular function call.
// - For functions defined in headers (especially templates or small utility functions), "inline" avoids multiple definition linker errors when included in multiple translation units.
// - "void" (without "inline") is a normal function declaration/definition; if defined in a header and included in multiple .cpp files, it can cause linker errors due to multiple definitions.
// - For non-template, non-static functions in headers, always use "inline" if you define them in the header.

//
// Example:
//   inline void foo() {}   // Safe in header
//   void foo() {}          // Not safe in header (unless static or in an anonymous namespace)
//

// Example usage:
// EEPROM.begin(EEPROM_SIZE); // Call in setup, EEPROM_SIZE must be large enough
// writeAllStepPatternsToEEPROM(); // Only once, to initialize EEPROM
// std::vector<std::vector<std::vector<uint8_t>>> allGroups;
// readAllStepPatternsFromEEPROM(allGroups);
// const std::vector<uint8_t>& pattern = allGroups[1][3]; // 3-step group (index 1), 4th pattern
