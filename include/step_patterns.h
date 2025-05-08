#pragma once

#include <vector>
#include <algorithm>
#include <Arduino.h>
#include <EEPROM.h>

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
    for (int n = 2; n <= 6; ++n)
    {
        std::vector<std::vector<int>> group;
        generateStepPermutations(n, group);
        groups.push_back(group);
    }
    return groups;
}

// Define the maximum number of steps and patterns per group (adjust as needed)
constexpr int MIN_STEPS = 2;
constexpr int MAX_STEPS = 6;

// Calculate the number of permutations for n steps (n!)
constexpr int factorial(int n)
{
    return (n <= 1) ? 1 : n * factorial(n - 1);
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

// Example usage:
// EEPROM.begin(EEPROM_SIZE); // Call in setup, EEPROM_SIZE must be large enough
// writeAllStepPatternsToEEPROM(); // Only once, to initialize EEPROM
// std::vector<std::vector<std::vector<uint8_t>>> allGroups;
// readAllStepPatternsFromEEPROM(allGroups);
// const std::vector<uint8_t>& pattern = allGroups[1][3]; // 3-step group (index 1), 4th pattern
