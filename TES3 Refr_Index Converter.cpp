﻿#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <filesystem>
#include <vector>
#include <sstream>
#include <format>
#include <memory>
#include <chrono>

#include <cctype>
#include <cstdlib>

#include <json.hpp>
#include <sqlite3.h>

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.3.1";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";
const std::string PROGRAM_TESTER = "Beta testing by Pirate443";

// Define sets to store valid master indices and masters from the database
std::unordered_set<int> validMastersIN;
std::unordered_set<int> validMastersDB;

// Function to clear log file
void logClear() {
    std::ofstream file("tes3_ri.log", std::ios::trunc);
}

// Function to log messages to both a log file and console
void logMessage(const std::string& message, std::ofstream& logFile) {
    logFile << message << std::endl;
    std::cout << message << std::endl;
}

// Function to log errors, close the database and terminate the program
void logErrorAndExit(sqlite3* db, const std::string& message, std::ofstream& logFile) {
    logMessage(message, logFile);

    if (db) sqlite3_close(db);
    logFile.close();

    std::cout << "Press Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::exit(EXIT_FAILURE);
}

// Function to check if file is a conversion output
bool hasConversionPrefix(const std::filesystem::path& filePath) {
    std::string filename = filePath.filename().string();
    return filename.find("CONV_RUtoEN_") == 0 ||
           filename.find("CONV_ENtoRU_") == 0;
}

// Function to parse arguments
struct ProgramOptions {
    bool batchMode = false;
    bool silentMode = false;
    std::vector<std::filesystem::path> inputFiles;
    int conversionType = 0;
};

ProgramOptions parseArguments(int argc, char* argv[]) {
    ProgramOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string argLower = arg;
        std::transform(argLower.begin(), argLower.end(), argLower.begin(), ::tolower);

        if (argLower == "--batch" || argLower == "-b") {
            options.batchMode = true;
        }
        else if (argLower == "--silent" || argLower == "-s") {
            options.silentMode = true;
        }
        else if (argLower == "--ru-to-en" || argLower == "-1") {
            options.conversionType = 1;
        }
        else if (argLower == "--en-to-ru" || argLower == "-2") {
            options.conversionType = 2;
        }
        else if (argLower == "--help" || argLower == "-h") {
            std::cout << "TES3 Refr_Index Converter - Help\n"
                      << "================================\n\n"
                      << "Usage:\n"
                      << "  .\\\"TES3 Refr_Index Converter.exe\" [OPTIONS] [TARGETS]\n\n"
                      << "Options:\n"
                      << "  -b, --batch      Enable batch mode (required when processing multiple files)\n"
                      << "  -s, --silent     Suppress non-critical messages (faster conversion)\n"
                      << "  -1, --ru-to-en   Convert Russian 1C -> English GOTY\n"
                      << "  -2, --en-to-ru   Convert English GOTY -> Russian 1C\n"
                      << "  -h, --help       Show this help message\n\n"
                      << "Target Formats:\n\n"
                      << "  Single File (works without batch mode):\n"
                      << "    mod-in-the-same-folder.esp\n"
                      << "    C:\\Morrowind\\Data Files\\mod.esm\n\n"
                      << "  Multiple Files (requires -b batch mode):\n"
                      << "    file1.esp;file2.esm;file 3.esp\n"
                      << "    D:\\Mods\\mod.esp;C:\\Morrowind\\Data Files\\Master mod.esm;Mod-in-the-same-folder.esp\n\n"
                      << "  Entire Directory (batch mode, recursive processing):\n"
                      << "    C:\\Morrowind\\Data Files\\\n"
                      << "    .\\Data\\  (relative path)\n\n"
                      << "Important Notes:\n\n"
                      << "  Supported:\n"
                      << "    - ASCII-only file paths (English letters, numbers, standard symbols)\n"
                      << "    - Both absolute (C:\\...) and relative (.\\Data\\...) paths\n\n"
                      << "  Not Supported:\n"
                      << "    - Paths containing non-ASCII characters (e.g., Cyrillic, Chinese, special symbols)\n"
                      << "    - Wildcards (*, ?) in CMD (works better in PowerShell)\n\n"
                      << "  Solution for Non-ASCII Paths:\n"
                      << "    If your files are in a folder with non-ASCII characters (e.g., C:\\Игры\\Morrowind\\),\n"
                      << "    move them to a folder with only English characters (C:\\Games\\Morrowind\\.\n\n"
                      << "Shell Compatibility:\n\n"
                      << "  PowerShell (Recommended):\n"
                      << "    - Fully supports batch processing, recursive search, and wildcards\n"
                      << "    - Example command:\n"
                      << "      & .\\\"TES3 Refr_Index Converter.exe\" -1 \"C:\\Morrowind\\Data Files\\mod.esp\"\n\n"
                      << "  CMD (Limited Support):\n"
                      << "    - Does not support recursive file selection with wildcards\n"
                      << "    - Example command:\n"
                      << "      .\\\"TES3 Refr_Index Converter.exe\" -1 \"C:\\Morrowind\\Data Files\\mod.esp\"\n\n"
                      << "Wildcard Support:\n\n"
                      << "  PowerShell (Recommended for Bulk Processing):\n"
                      << "    - Convert all .esp files recursively in the current folder:\n"
                      << "      & .\\\"TES3 Refr_Index Converter.exe\" -b (Get-ChildItem -Recurse -Filter \"*.esp\").FullName\n\n"
                      << "    - Convert all .esp files only in \"C:\\Mods\\\" (without subfolders):\n"
                      << "      & .\\\"TES3 Refr_Index Converter.exe\" -b (Get-ChildItem -Path \"C:\\Mods\\\" -Filter \"*.esp\").FullName\n\n"
                      << "  CMD (Limited Wildcard Support, No Recursion):\n"
                      << "    - Convert all .esp files in current folder:\n"
                      << "      for %f in (\"*.esp\") do \"TES3 Refr_Index Converter.exe\" -b -2 \"%~f\"\n\n"
                      << "    - Convert all .esp files in target folder:\n"
                      << "      for %f in (\"C:\\Mods\\*.esp\") do \"TES3 Refr_Index Converter.exe\" -b -2 \"%~f\"\n\n"
                      << "Example Commands:\n\n"
                      << "  Convert an entire folder:\n"
                      << "    & .\\\"TES3 Refr_Index Converter.exe\" -b -1 \"C:\\Morrowind\\Data Files\\\"\n\n"
                      << "  Convert multiple specific files:\n"
                      << "    & .\\\"TES3 Refr_Index Converter.exe\" -b -2 \"C:\\Mods\\mod.esp;Mod-in-the-same-folder.esp\"\n\n"
                      << "  Convert all files starting with 'RR_' in a folder:\n"
                      << "    & .\\\"TES3 Refr_Index Converter.exe\" -b (Get-ChildItem -Path \"C:\\Morrowind\\Data Files\\\" -Recurse -Filter \"RR_*.esp\").FullName\n";

            std::cout << "\nPress Enter to exit...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            exit(0);
        }
        else {
            std::string argStr = arg;
            std::istringstream iss(argStr);
            std::string pathStr;

            // Spliting argument by semicolons
            while (std::getline(iss, pathStr, ';')) {
                std::filesystem::path path(pathStr);
                if (std::filesystem::exists(path)) {
                    options.inputFiles.push_back(path);
                }
                else {
                    std::cerr << "Warning: File not found - " << pathStr << "\n\n";
                }
            }
        }
    }

    return options;
}

// Unified function for handling user choices
int getUserChoice(const std::string& prompt,
    const std::unordered_set<std::string>& validChoices,
    std::ofstream& logFile,
    const std::string& errorMessage = "\nInvalid choice: enter ")
{
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
        logMessage(errorMessage + validOptions, logFile);
    }
}

// Function for handling conversion choices
int getUserConversionChoice(std::ofstream& logFile) {
    return getUserChoice(
        "\nConvert refr_index values in a plugin or master file:\n"
        "1. From Russian 1C to English GOTY\n"
        "2. From English GOTY to Russian 1C\n"
        "Choice: ",
        { "1", "2" }, logFile
    );
}

// Function for handling mismatch choices
int getUserMismatchChoice(std::ofstream& logFile, const ProgramOptions& options) {
    if (options.batchMode) {
        logMessage("\nBatch mode enabled - automatically replacing mismatched entries...", logFile);
        return 1;
    }

    return getUserChoice(
        "\nMismatched entries found (usually occur if a Tribunal or Bloodmoon object was modified with\n"
        "'Edit -> Search & Replace' in TES3 CS). Would you like to replace their refr_index anyway?\n"
        "1. Yes\n"
        "2. No\n"
        "Choice: ",
        { "1", "2" }, logFile
    );
}

// Function for handling input file path from user with recursive directory search
std::vector<std::filesystem::path> getInputFilePaths(const ProgramOptions& options, std::ofstream& logFile) {
    std::vector<std::filesystem::path> result;

    // Helper function to process a single path
    auto processPath = [&](const std::filesystem::path& path) {
        try {
            if (std::filesystem::is_directory(path)) {
                logMessage("Processing directory: " + path.string(), logFile);
                for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        if ((ext == ".esp" || ext == ".esm") && !hasConversionPrefix(entry.path())) {
                            result.push_back(entry.path());
                        }
                    }
                }
            }
            else if (std::filesystem::exists(path)) {
                std::string ext = path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if ((ext == ".esp" || ext == ".esm") && !hasConversionPrefix(path)) {
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

    // Process command line arguments if any
    if (!options.inputFiles.empty()) {
        logMessage("Using files from command line arguments:", logFile);
        for (const auto& path : options.inputFiles) {
            processPath(path);
        }

        if (!options.silentMode && !result.empty()) {
            logMessage("Found " + std::to_string(result.size()) + " valid input files:", logFile);
            for (const auto& file : result) {
                logMessage("  " + file.string(), logFile);
            }
        }
        return result;
    }

    // Interactive mode
    if (options.batchMode) {
        while (true) {
            std::cout << "\nEnter:\n"
                         "- full path to your Mod folder\n"
                         "- full path to your .ESP|ESM file (with extension)\n"
                         "- file name of your .ESP|ESM file (with extension), if its in the same directory with this program\n"
                         "You can mix any combination of the above formats, separating them with semicolons ';'\n";
            std::string input;
            std::getline(std::cin, input);

            // Parse paths separated by semicolons
            std::vector<std::string> pathStrings;
            std::istringstream iss(input);
            std::string pathStr;

            while (std::getline(iss, pathStr, ';')) {
                // Trim whitespace from both ends
                pathStr.erase(pathStr.find_last_not_of(" \t") + 1);
                pathStr.erase(0, pathStr.find_first_not_of(" \t"));

                if (!pathStr.empty()) {
                    pathStrings.push_back(pathStr);
                }
            }

            // Process each path
            result.clear();
            for (const auto& pathStr : pathStrings) {
                processPath(pathStr);
            }

            if (!result.empty()) {
                logMessage("Input files found (" + std::to_string(result.size()) + "):", logFile);
                for (const auto& path : result) {
                    logMessage("  " + path.string(), logFile);
                }
                return result;
            }

            logMessage("ERROR - input files not found: check their directory, names and extensions!", logFile);
        }
    }
    else {
        // Single file mode
        std::vector<std::filesystem::path> singleResult;
        std::filesystem::path filePath;

        while (true) {
            std::cout << "\nEnter full path to your .ESP|ESM or just filename (with extension), if your file is in the same directory\n"
                         "with this program: ";
            std::string input;
            std::getline(std::cin, input);

            // Remove start/end spaces
            input.erase(input.find_last_not_of(" \t") + 1);
            input.erase(0, input.find_first_not_of(" \t"));

            filePath = input;

            std::string extension = filePath.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            if (std::filesystem::exists(filePath) && (extension == ".esp" || extension == ".esm")) {
                if (!options.silentMode) {
                    logMessage("Input file found: " + filePath.string(), logFile);
                }
                singleResult.push_back(filePath);
                break;
            }

            if (!options.silentMode) {
                logMessage("\nERROR - input file not found: check its directory, name and extension!", logFile);
            }
        }

        return singleResult;
    }
}

// Function to check the dependency order of Parent Master files in the input .ESP|ESM data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const ordered_json& inputData, std::ofstream& logFile) {
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    if (headerIter == inputData.end() || !headerIter->contains("masters")) {
        logMessage("ERROR - missing 'header' section or 'masters' key!", logFile);
        return { false, {} };
    }

    const auto& masters = (*headerIter)["masters"];
    std::optional<size_t> mwPos, tPos, bPos;

    for (size_t i = 0; i < masters.size(); ++i) {
        if (masters[i].is_array() && !masters[i].empty() && masters[i][0].is_string()) {
            const std::string masterName = masters[i][0];
            if (masterName == "Morrowind.esm") mwPos.emplace(i);
            else if (masterName == "Tribunal.esm") tPos.emplace(i);
            else if (masterName == "Bloodmoon.esm") bPos.emplace(i);
        }
    }

    if (!mwPos.has_value()) {
        logMessage("ERROR - Morrowind.esm dependency not found!", logFile);
        return { false, {} };
    }

    validMastersIN.clear();
    validMastersDB.clear();

    if (tPos.has_value() && bPos.has_value()) {
        if (*tPos > *mwPos && *bPos > *tPos) {
            logMessage("Valid order of Parent Master files found: M+T+B\n", logFile);
            validMastersIN = { 2, 3 };
            validMastersDB = { 1 };
            return { true, validMastersDB };
        }
        logMessage("ERROR - invalid order of Parent Master files found: M+B+T\n", logFile);
        return { false, {} };
    }

    if (tPos.has_value() && *tPos > *mwPos) {
        logMessage("Valid order of Parent Master files found: M+T\n", logFile);
        validMastersIN = { 2 };
        validMastersDB = { 2 };
        return { true, validMastersDB };
    }

    if (bPos.has_value() && *bPos > *mwPos) {
        logMessage("Valid order of Parent Master files found: M+B\n", logFile);
        validMastersIN = { 2 };
        validMastersDB = { 3 };
        return { true, validMastersDB };
    }

    return { false, {} };
}

// Function to fetch the refr_index from the database
std::optional<int> fetchRefIndex(sqlite3* db, const std::string& query, int refrIndexJSON, const std::string& idJSON) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_ptr(stmt, sqlite3_finalize);
    sqlite3_bind_int(stmt, 1, refrIndexJSON);
    sqlite3_bind_text(stmt, 2, idJSON.c_str(), idJSON.length(), SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return sqlite3_column_int(stmt, 0);
    }
    return std::nullopt;
}

// Define an enumeration for fetch modes
enum FetchMode {
    FETCH_DB_ID,
    FETCH_OPPOSITE_REFR_INDEX
};

// Template function to fetch ID from the database based on the fetch mode
template <FetchMode mode>
auto fetchID(sqlite3* db, int refrIndexJSON, int mastIndex, const std::unordered_set<int>& validMastersDB, int conversionChoice) {
    std::string query;

    // Determine the query based on the conversion choice and fetch mode
    switch (conversionChoice) {
    case 1:
        query = (mode == FETCH_DB_ID)
            ? "SELECT ID FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ?"
            : "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ?";
        break;
    case 2:
        query = (mode == FETCH_DB_ID)
            ? "SELECT ID FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ?"
            : "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ?";
        break;
    default:
        if constexpr (mode == FETCH_DB_ID) return std::string();
        else return -1;
    }

    // Append conditions to the query based on the valid masters
    if (validMastersDB.count(1)) {
        if (mastIndex == 2) query += " AND Master = 'Tribunal'";
        else if (mastIndex == 3) query += " AND Master = 'Bloodmoon'";
    }
    else if (validMastersDB.count(2)) query += " AND Master = 'Tribunal'";
    else if (validMastersDB.count(3)) query += " AND Master = 'Bloodmoon'";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if constexpr (mode == FETCH_DB_ID) return std::string();
        else return -1;
    }

    sqlite3_bind_int(stmt, 1, refrIndexJSON);

    // Fetch the value based on the fetch mode
    if constexpr (mode == FETCH_DB_ID) {
        std::string idDB;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* idJSON = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (idJSON) idDB = idJSON;
        }
        sqlite3_finalize(stmt);
        return idDB;
    }
    else {
        int refrIndexDB = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            refrIndexDB = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return refrIndexDB;
    }
}

// Mismatch entry structure to store reference data discrepancies
struct MismatchEntry {
    int refrIndexJSON;    // Reference index from JSON
    std::string idJSON;   // Object ID from JSON
    std::string idDB;     // Expected ID from database
    int refrIndexDB;      // Expected reference index from database

    bool operator==(const MismatchEntry& other) const noexcept {
        return refrIndexJSON == other.refrIndexJSON && idJSON == other.idJSON;
    }
};

//Hash function specialization for MismatchEntry
namespace std {
    template<> struct hash<MismatchEntry> {
        size_t operator()(const MismatchEntry& e) const {
            size_t h1 = hash<int>{}(e.refrIndexJSON);
            size_t h2 = hash<string>{}(e.idJSON);
            return h1 ^ (h2 << 1);
        }
    };
}

// Container for tracking unique mismatch entries using unordered_set
std::unordered_set<MismatchEntry> mismatchedEntries;

// Function to process replacements and mismatches
int processReplacementsAndMismatches(sqlite3* db, const ProgramOptions& options, const std::string& query, ordered_json& inputData,
    int conversionChoice, int& replacementsFlag,
    const std::unordered_set<int>& validMastersDB,
    std::unordered_set<MismatchEntry>& mismatchedEntries,
    std::ofstream& logFile) {

    // Validate root JSON structure
    if (!inputData.is_array()) {
        logMessage("\nERROR - input JSON is not an array, unable to process!", logFile);
        return -1;
    }

    // Process each cell in the JSON array
    for (auto cell_it = inputData.begin(); cell_it != inputData.end(); ++cell_it) {

        // Skip non-cell entries or cells without proper type
        if (!cell_it->contains("type") || (*cell_it)["type"] != "Cell") continue;

        // Extract references array from cell
        auto& references = (*cell_it)["references"];
        if (!references.is_array()) continue;

        // Process individual references in cell
        for (auto ref_it = references.begin(); ref_it != references.end(); ++ref_it) {
            auto& refr_index = *ref_it;

            // Validate reference structure
            if (!refr_index.contains("refr_index") || !refr_index["refr_index"].is_number_integer() ||
                !refr_index.contains("id") || !refr_index["id"].is_string()) {
                continue;
            }

            // Extract reference data
            int refrIndexExtracted = refr_index["refr_index"];
            std::string idExtracted = refr_index["id"];
            int mastIndexExtracted = refr_index.value("mast_index", -1);

            // Valid Parent Master files check
            if (!validMastersIN.count(mastIndexExtracted)) {
                //logMessage("Skipping object (invalid master index): " + idExtracted, logFile);
                continue;
            }

            // Handle replacements
            if (auto new_refrIndex = fetchRefIndex(db, query, refrIndexExtracted, idExtracted)) {
                refr_index["refr_index"] = *new_refrIndex;
                if (!options.silentMode) {
                    logMessage("Replaced JSON refr_index " + std::to_string(refrIndexExtracted) +
                               " with DB refr_index " + std::to_string(*new_refrIndex) +
                               " for JSON id " + idExtracted, logFile);
                }
                replacementsFlag = 1;
            }

            // Handle mismatches
            else {
                const int refrIndexDB = fetchID<FETCH_OPPOSITE_REFR_INDEX>(db, refrIndexExtracted, mastIndexExtracted, validMastersDB, conversionChoice);

                // Skip if no matching record found in DB
                if (refrIndexDB == -1) {
                    //if (!options.silentMode) {
                        //logMessage("Skipping object (no match in DB): JSON refr_index " + std::to_string(refrIndexExtracted) +
                        //           " and JSON id " + idExtracted, logFile);
                    //}
                    continue;
                }

                const std::string idDB = fetchID<FETCH_DB_ID>(db, refrIndexExtracted, mastIndexExtracted, validMastersDB, conversionChoice);

                // Only proceed with mismatch handling if we have valid DB data
                if (!options.silentMode) {
                    logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndexExtracted) +
                               " and JSON id " + idExtracted + " with DB refr_index " + std::to_string(refrIndexDB) +
                               " and DB id " + idDB, logFile);
                }

                // Handle duplicated mismatches
                if (auto [it, inserted] = mismatchedEntries.insert(
                    MismatchEntry{ refrIndexExtracted, idExtracted, idDB, refrIndexDB }); !inserted) {
                    logMessage("WARNING - skipped duplicate mismatch entry for JSON refr_index " + std::to_string(refrIndexExtracted) +
                               " and JSON id " + idExtracted, logFile);
                }
            }
        }
    }

    // Handle user choice for mismatched entries
    if (!mismatchedEntries.empty()) {
        int mismatchChoice = getUserMismatchChoice(logFile, options);

        if (mismatchChoice == 1) {
            // Apply replacements for all tracked mismatches
            for (const auto& entry : mismatchedEntries) {
                for (auto& cell : inputData) {
                    if (!cell.contains("references") || !cell["references"].is_array()) continue;

                    // Find and update matching references
                    for (auto& reference : cell["references"]) {
                        if (reference["refr_index"] == entry.refrIndexJSON &&
                            reference.value("id", "") == entry.idJSON) {
                            reference["refr_index"] = entry.refrIndexDB;
                            if (!options.silentMode) {
                                logMessage("Replaced mismatched JSON refr_index " + std::to_string(entry.refrIndexJSON) +
                                           " with DB refr_index " + std::to_string(entry.refrIndexDB) +
                                           " for JSON id " + entry.idJSON, logFile);
                            }
                            replacementsFlag = 1;
                        }
                    }
                }
            }
        }
        else {
            logMessage("\nMismatched entries will remain unchanged...", logFile);
        }
    }
    else {
        logMessage("\nNo mismatched entries found - skipping mismatch handling", logFile);
    }

    return 0;
}

// Function to save the modified JSON data to file
bool saveJsonToFile(const std::filesystem::path& jsonImportPath, const ordered_json& inputData, std::ofstream& logFile) {
    std::ofstream outputFile(jsonImportPath);
        if (!outputFile) return false;
        outputFile << std::setw(2) << inputData;
            logMessage("\nModified data saved as: " + jsonImportPath.string() + "\n", logFile);
    return true;
}

// Function to convert the .JSON file to .ESP|ESM
bool convertJsonToEsp(const std::filesystem::path& jsonImportPath, const std::filesystem::path& espFilePath, std::ofstream& logFile) {
    std::string command = "tes3conv.exe \"" + jsonImportPath.string() + "\" \"" + espFilePath.string() + "\"";
        if (std::system(command.c_str()) != 0) return false;
            logMessage("Conversion to .ESP|ESM successful: " + espFilePath.string() + "\n", logFile);
    return true;
}

// Main function
int main(int argc, char* argv[]) {
    // Parse command line arguments
    ProgramOptions options = parseArguments(argc, argv);

    // Display program information (if not in silent mode)
    if (!options.silentMode) {
        std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n"
                  << PROGRAM_AUTHOR << "\n\n" << PROGRAM_TESTER << "\n\n";
    }

    // Log file initialisation
    std::ofstream logFile("tes3_ri.log", std::ios::app);
    if (!logFile) {
        std::cerr << "ERROR - failed to open log file!\n\n"
                  << "Press Enter to exit...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return EXIT_FAILURE;
    }

    // Clear log file
    logClear();
    if (!options.silentMode) {
        logMessage("Log file cleared...", logFile);
    }

    // Check if the database file exists
    if (!std::filesystem::exists("tes3_ri_en-ru_refr_index.db")) {
        logErrorAndExit(nullptr, "ERROR - database file 'tes3_ri_en-ru_refr_index.db' not found!\n", logFile);
    }

    // Open the database
    sqlite3* db = nullptr;
    if (sqlite3_open("tes3_ri_en-ru_refr_index.db", &db)) {
        logErrorAndExit(db, "ERROR - failed to open database: " + std::string(sqlite3_errmsg(db)) + "!\n", logFile);
    }

    if (!options.silentMode) {
        logMessage("Database opened successfully...", logFile);
    }

    // Check if the converter executable exists
    if (!std::filesystem::exists("tes3conv.exe")) {
        logErrorAndExit(db, "ERROR - tes3conv.exe not found! Please download the latest version from\n"
                            "github.com/Greatness7/tes3conv/releases and place it in the same directory\n"
                            "with this program.\n", logFile);
    }

    if (!options.silentMode) {
        logMessage("tes3conv.exe found...\n"
                   "Initialisation complete.", logFile);
    }

    // Get the conversion choice
    if (options.conversionType == 0) {
        options.conversionType = getUserConversionChoice(logFile);
    }
    else if (!options.silentMode) {
        logMessage("\nConversion type set from arguments: " + std::string(options.conversionType == 1 ? "RU to EN" : "EN to RU"), logFile);
    }

    // Get the input file path(s)
    auto inputPaths = getInputFilePaths(options, logFile);

    // Time start
    auto programStart = std::chrono::high_resolution_clock::now();

    // Sequential processing of each file
    for (const auto& pluginImportPath : inputPaths) {
        // Time file start
        auto fileStart = std::chrono::high_resolution_clock::now();

        // Clear data
        validMastersIN.clear();
        validMastersDB.clear();
        mismatchedEntries.clear();

        logMessage("\nProcessing file: " + pluginImportPath.string(), logFile);

        try {
            // Define the output file path
            std::filesystem::path jsonImportPath = pluginImportPath.parent_path() / (pluginImportPath.stem().string() + ".json");

            // Convert the input file to .JSON
            std::string command = "tes3conv.exe \"" + pluginImportPath.string() + "\" \"" + jsonImportPath.string() + "\"";
            if (std::system(command.c_str()) != 0) {
                logMessage("ERROR - converting to .JSON failed for file: " + pluginImportPath.string(), logFile);
                continue;
            }
            logMessage("Conversion to .JSON successful: " + jsonImportPath.string(), logFile);

            // Load the generated JSON file
            std::ifstream inputFile(jsonImportPath);
            ordered_json inputData;
            inputFile >> inputData;
            inputFile.close();

            // Check the dependency order
            auto [isValid, validMasters] = checkDependencyOrder(inputData, logFile);
            if (!isValid) {
                std::filesystem::remove(jsonImportPath);
                logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                logMessage("ERROR - required Parent Master files dependency not found, or their order is invalid for file: " + pluginImportPath.string(), logFile);
                continue;
            }

            // Initialize the replacements flag
            int replacementsFlag = 0;

            // Initialize the query based on conversion choice
            std::string dbQuery = (options.conversionType == 1)
                ? "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ? AND id = ?;"
                : "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ? AND id = ?;";

            // Process replacements and mismatches
            if (processReplacementsAndMismatches(db, options, dbQuery, inputData, options.conversionType, replacementsFlag, validMasters, mismatchedEntries, logFile) == -1) {
                logMessage("ERROR - processing failed for file: " + pluginImportPath.string(), logFile);
                continue;
            }

            // Check if any replacements were made
            if (replacementsFlag == 0) {
                std::filesystem::remove(jsonImportPath);
                logMessage("No replacements found for file: " + pluginImportPath.string() + " - conversion skipped\n", logFile);
                logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                continue;
            }

            // Define conversion prefix
            std::string convPrefix = (options.conversionType == 1) ? "RUtoEN" : "ENtoRU";

            // Save the modified data to .JSON file
            auto newJsonName = std::format("CONV_{}_{}{}", convPrefix, pluginImportPath.stem().string(), ".json");
            std::filesystem::path jsonExportPath = pluginImportPath.parent_path() / newJsonName;

            if (!saveJsonToFile(jsonExportPath, inputData, logFile)) {
                logMessage("ERROR - failed to save modified data to .JSON file: " + jsonExportPath.string(), logFile);
                continue;
            }

            // Convert the .JSON file back to .ESP|ESM
            auto pluginExportName = std::format("CONV_{}_{}{}", convPrefix, pluginImportPath.stem().string(), pluginImportPath.extension().string());
            std::filesystem::path pluginExportPath = pluginImportPath.parent_path() / pluginExportName;

            if (!convertJsonToEsp(jsonExportPath, pluginExportPath, logFile)) {
                logMessage("ERROR - failed to convert .JSON back to .ESP|ESM: " + pluginExportPath.string(), logFile);
                continue;
            }

            // Clean up temporary .JSON files
            std::filesystem::remove(jsonImportPath);
            std::filesystem::remove(jsonExportPath);
            logMessage("Temporary .JSON files deleted: " + jsonImportPath.string() + "\n" +
                       "                          and: " + jsonExportPath.string() + "\n", logFile);

            // Time file total
            auto fileEnd = std::chrono::high_resolution_clock::now();
            auto fileDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fileEnd - fileStart);
            logMessage("File processed in " + std::to_string(fileDuration.count() / 1000.0) + " seconds", logFile);
        }
        catch (const std::exception& e) {
            // Time error
            auto fileEnd = std::chrono::high_resolution_clock::now();
            auto fileDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fileEnd - fileStart);

            logMessage("ERROR processing " + pluginImportPath.string() + ": " + e.what(), logFile);
            // Clear data in case of error
            validMastersIN.clear();
            validMastersDB.clear();
            mismatchedEntries.clear();
            continue;
        }
    }

    // Time total
    auto programEnd = std::chrono::high_resolution_clock::now();
    auto programDuration = std::chrono::duration_cast<std::chrono::milliseconds>(programEnd - programStart);

    logMessage("\nTotal processing time: " + std::to_string(programDuration.count() / 1000.0) + " seconds", logFile);

    // Close the database
    sqlite3_close(db);
    if (!options.silentMode) {
        logMessage("\nThe ending of the words is ALMSIVI", logFile);
        logFile.close();

        // Wait for user input before exiting
        std::cout << "\nPress Enter to continue...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    return EXIT_SUCCESS;
}