#include <filesystem>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>

#include "ri_logger.h"
#include "ri_options.h"
#include "ri_user_interaction.h"

// Unified function for handling user choices
int getUserChoice(const std::string& prompt,
    const std::unordered_set<std::string>& validChoices,
    std::ofstream& logFile)
{
    const std::string errorMessage = "\nInvalid choice: enter ";
    std::string input;
    while (true) {
        std::cout << prompt;
        std::getline(std::cin, input);

        if (validChoices.count(input)) {
            return std::stoi(input);
        }

        // List of valid options for the error message
        std::string validOptions;
        for (const auto& option : validChoices) {
            if (!validOptions.empty()) validOptions += " or ";
            validOptions += option;
        }
        std::cout << (errorMessage + validOptions) << '\n';
    }
}

// Function for handling user conversion choices
int getUserConversionChoice(std::ofstream& logFile) {
    return getUserChoice(
        "\nConvert refr_index values in a plugin or master file:\n"
        "1. From Russian 1C to English GOTY\n"
        "2. From English GOTY to Russian 1C\n"
        "Choice: ",
        { "1", "2" }, logFile
    );
}

// Function for handling user mismatch choices
int getUserMismatchChoice(std::ofstream& logFile, const ProgramOptions& options) {
    if (options.batchMode) {
        if (!options.silentMode) {
            logMessage("\nBatch mode enabled - automatically replacing mismatched entries...\n", logFile);
        }
        return 1;
    }

    int choice = getUserChoice(
        "\nMismatched entries found (usually occur if a Tribunal or Bloodmoon object was modified with\n"
        "'Edit -> Search & Replace' in TES3 CS). Would you like to replace their refr_index anyway?\n"
        "1. Yes (Recommended)\n"
        "2. No\n"
        "Choice: ",
        { "1", "2" }, logFile
    );

    logMessage("", logFile);

    return choice;
}

// Function for handling input file paths from user with recursive directory search
std::vector<std::filesystem::path> getInputFilePaths(const ProgramOptions& options, std::ofstream& logFile) {
    std::vector<std::filesystem::path> result;

    // Helper function to normalize a string path: remove quotes and trim whitespace
    auto normalizePathStr = [](std::string pathStr) {
        pathStr.erase(std::remove(pathStr.begin(), pathStr.end(), '\"'), pathStr.end());
        pathStr.erase(pathStr.find_last_not_of(" \t") + 1);
        pathStr.erase(0, pathStr.find_first_not_of(" \t"));
        return pathStr;
        };

    // Helper function to check if a path is a valid .esp or .esm file
    auto isValidModFile = [](const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == ".esp" || ext == ".esm";
        };

    // Helper function to process a single path (file or directory)
    auto tryAddFile = [&](const std::filesystem::path& path) {
        try {
            if (std::filesystem::exists(path)) {
                if (std::filesystem::is_directory(path)) {
                    logMessage("\nProcessing directory: " + path.string(), logFile);
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                        if (entry.is_regular_file() && isValidModFile(entry.path())) {
                            result.push_back(entry.path());
                        }
                    }
                }
                else if (isValidModFile(path)) {
                    result.push_back(path);
                }
                else if (!options.silentMode) {
                    logMessage("WARNING - input file has invalid extension: " + path.string(), logFile);
                }
            }
            else if (!options.silentMode) {
                logMessage("WARNING - input path not found: " + path.string(), logFile);
            }
        }
        catch (const std::exception& e) {
            logMessage("ERROR processing path " + path.string() + ": " + e.what(), logFile);
        }
        };

    // Helper function to log the list of successfully found files
    auto logResults = [&]() {
        if (!options.silentMode && !result.empty()) {
            logMessage("Found " + std::to_string(result.size()) + " valid input files:", logFile);
            for (const auto& file : result) {
                logMessage("  " + file.string(), logFile);
            }
        }

        logMessage("", logFile);

        };

    // Use input files passed via command line arguments
    if (!options.inputFiles.empty()) {
        logMessage("\nUsing files from command line arguments", logFile);
        for (const auto& path : options.inputFiles) {
            tryAddFile(path);
        }
        logResults();
        return result;
    }

    // Helper function to parse user input string into multiple paths
    auto parseUserInput = [&](const std::string& input) {
        std::vector<std::string> pathStrings;
        std::istringstream iss(input);
        std::string pathStr;
        while (std::getline(iss, pathStr, ';')) {
            pathStr = normalizePathStr(pathStr);
            if (!pathStr.empty()) {
                pathStrings.push_back(pathStr);
            }
        }
        return pathStrings;
        };

    // Batch (interactive multi-path) mode
    if (options.batchMode) {
        while (true) {
            std::cout << "\nEnter:\n"
                         "- full path to your Mod folder\n"
                         "- full path to your .ESP|ESM file (with extension)\n"
                         "- file name of your .ESP|ESM file (with extension), if it is in the same directory with this program\n"
                         "You can mix any combination of the above formats, separating them with semicolons ';'\n";
            std::string input;
            std::getline(std::cin, input);

            result.clear();
            for (const auto& pathStr : parseUserInput(input)) {
                tryAddFile(pathStr);
            }

            if (!result.empty()) {
                logResults();
                return result;
            }

            std::cout << "\nERROR - input files not found: check their directory, names, and extensions!\n";
        }
    }

    // Single file mode (one file input via prompt)
    while (true) {
        std::cout << "\nEnter full path to your .ESP|ESM or just filename (with extension), if your file is in the same directory\n"
                     "with this program: ";
        std::string input;
        std::getline(std::cin, input);

        std::filesystem::path filePath = normalizePathStr(input);

        if (std::filesystem::exists(filePath) && isValidModFile(filePath)) {
            logMessage("\nInput file found: " + filePath.string(), logFile);
            return { filePath };
        }

        std::cout << "\nERROR - input file not found: check its directory, name, and extension!\n";
    }
}