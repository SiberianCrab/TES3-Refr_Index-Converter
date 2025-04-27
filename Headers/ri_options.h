#pragma once
#include <json.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_set>

#include "ri_mismatches.h"

// Define program metadata constants
constexpr const char* PROGRAM_NAME = "TES3 Refr_Index Converter";
constexpr const char* PROGRAM_VERSION = "V 1.4.0";
constexpr const char* PROGRAM_AUTHOR = "by SiberianCrab";
constexpr const char* PROGRAM_TESTER = "Beta testing by Pirate443";

// Define tes3conv constants for Windows|Linux
#ifdef _WIN32
constexpr const char* TES3CONV_COMMAND = "tes3conv.exe";
#else
constexpr const char* TES3CONV_COMMAND = "./tes3conv";
#endif

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Structure for storing program configuration options
struct ProgramOptions {
    bool batchMode = false;
    bool silentMode = false;
    std::vector<std::filesystem::path> inputFiles;
    int conversionType = 0;
};

// Function to parse command-line arguments
ProgramOptions parseArguments(int argc, char* argv[]);

// Global data structures for validation and mismatch tracking:
extern std::unordered_set<int> validMastersIn;
extern std::unordered_set<int> validMastersDb;
extern std::unordered_set<MismatchEntry> mismatchedEntries;