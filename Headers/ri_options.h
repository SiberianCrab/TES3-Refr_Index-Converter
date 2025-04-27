#pragma once
#include <filesystem>
#include <vector>

// Structure for storing program configuration options
struct ProgramOptions {
    bool batchMode = false;
    bool silentMode = false;
    std::vector<std::filesystem::path> inputFiles;
    int conversionType = 0;
};

// Function to parse command-line arguments
ProgramOptions parseArguments(int argc, char* argv[]);