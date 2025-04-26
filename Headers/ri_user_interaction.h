#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include <filesystem>
#include <fstream>

#include "ri_arguments.h"

// Unified function for handling user choices
int getUserChoice(const std::string& prompt, const std::unordered_set<std::string>& validChoices, std::ofstream& logFile);

// Function for handling user conversion choices
int getUserConversionChoice(std::ofstream& logFile);

// Function for handling user mismatch choices
int getUserMismatchChoice(std::ofstream& logFile, const ProgramOptions& options);

// Function for handling input file paths from user with recursive directory search
std::vector<std::filesystem::path> getInputFilePaths(const ProgramOptions& options, std::ofstream& logFile);