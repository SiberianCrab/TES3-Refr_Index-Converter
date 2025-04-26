#pragma once
#include <string>

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.4.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";
const std::string PROGRAM_TESTER = "Beta testing by Pirate443";

// Define tes3conv constants for Windows|Linux
#ifdef _WIN32
const std::string TES3CONV_COMMAND = "tes3conv.exe";
#else
const std::string TES3CONV_COMMAND = "./tes3conv";
#endif