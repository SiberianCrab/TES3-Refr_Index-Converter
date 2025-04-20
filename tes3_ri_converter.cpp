#include <fstream>
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
#include <functional>

#include <cctype>
#include <cstdlib>

#include <json.hpp>
#include "database.h"

// Define tes3conv for Windows|Linux
#ifdef _WIN32
const std::string TES3CONV_COMMAND = "tes3conv.exe";
#else
const std::string TES3CONV_COMMAND = "./tes3conv";
#endif

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.3.2";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";
const std::string PROGRAM_TESTER = "Beta testing by Pirate443";

// Define sets to store valid master indices and masters from the database
std::unordered_set<int> validMastersIn;
std::unordered_set<int> validMastersDb;

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
void logErrorAndExit(const std::string& message, std::ofstream& logFile) {
    logMessage(message, logFile);

    logFile.close();

    std::cout << "Press Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::exit(EXIT_FAILURE);
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
                << "  .\\tes3_ri_converter.exe [OPTIONS] [TARGETS]\n\n"
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
                << "    .\\Data\\  (relative path)\n\n";

        #ifndef __linux__
            std::cout << "\nPress Enter to exit...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        #endif

            exit(0);
        }
        else {
            options.inputFiles.emplace_back(arg);
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
        logMessage("\nBatch mode enabled - automatically replacing mismatched entries...\n", logFile);
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
                    logMessage("Processing directory: " + path.string(), logFile);
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
        };

    // Use input files passed via command line arguments
    if (!options.inputFiles.empty()) {
        logMessage("Using files from command line arguments:", logFile);
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

            logMessage("ERROR - input files not found: check their directory, names, and extensions!", logFile);
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
            logMessage("Input file found: " + filePath.string(), logFile);
            return { filePath };
        }

        logMessage("\nERROR - input file not found: check its directory, name, and extension!", logFile);
    }
}

// Function to check if file is a conversion output
bool hasConversionTag(const ordered_json& inputData, const std::filesystem::path& filePath, std::ofstream& logFile) {
    // Find the header section in the JSON data
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    // Check if header contains a description field
    if (headerIter != inputData.end() && headerIter->contains("description")) {
        std::string description = (*headerIter)["description"];
        // Check conversion markers in the description
        if (description.find("Converted (RU->EN) by TES3 Ref_Ind Converter") != std::string::npos ||
            description.find("Converted (EN->RU) by TES3 Ref_Ind Converter") != std::string::npos) {

            return true;
        }
    }
    return false;
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

    validMastersIn.clear();
    validMastersDb.clear();

    if (tPos.has_value() && bPos.has_value()) {
        if (*tPos > *mwPos && *bPos > *tPos) {
            logMessage("Valid order of Parent Master files found: M+T+B\n", logFile);
            validMastersIn = { 2, 3 };
            validMastersDb = { 1 };
            return { true, validMastersDb };
        }
        logMessage("ERROR - invalid order of Parent Master files found: M+B+T\n", logFile);
        return { false, {} };
    }

    if (tPos.has_value() && *tPos > *mwPos) {
        logMessage("Valid order of Parent Master files found: M+T\n", logFile);
        validMastersIn = { 2 };
        validMastersDb = { 2 };
        return { true, validMastersDb };
    }

    if (bPos.has_value() && *bPos > *mwPos) {
        logMessage("Valid order of Parent Master files found: M+B\n", logFile);
        validMastersIn = { 2 };
        validMastersDb = { 3 };
        return { true, validMastersDb };
    }

    return { false, {} };
}

// Function to fetch the refr_index from the database
std::optional<int> fetchRefIndex(const Database& db, const std::string& query, int refrIndexJson, const std::string& idJson) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_ptr(stmt, sqlite3_finalize);
    sqlite3_bind_int(stmt, 1, refrIndexJson);
    sqlite3_bind_text(stmt, 2, idJson.c_str(), idJson.length(), SQLITE_STATIC);

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
auto fetchID(const Database& db, int refrIndexJson, int mastIndex, const std::unordered_set<int>& validMastersDb, int conversionChoice) {
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
    if (validMastersDb.count(1)) {
        if (mastIndex == 2) query += " AND Master = 'Tribunal'";
        else if (mastIndex == 3) query += " AND Master = 'Bloodmoon'";
    }
    else if (validMastersDb.count(2)) query += " AND Master = 'Tribunal'";
    else if (validMastersDb.count(3)) query += " AND Master = 'Bloodmoon'";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if constexpr (mode == FETCH_DB_ID) return std::string();
        else return -1;
    }

    sqlite3_bind_int(stmt, 1, refrIndexJson);

    // Fetch the value based on the fetch mode
    if constexpr (mode == FETCH_DB_ID) {
        std::string idDb;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* idJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (idJson) idDb = idJson;
        }
        sqlite3_finalize(stmt);
        return idDb;
    }
    else {
        int refrIndexDb = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            refrIndexDb = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return refrIndexDb;
    }
}

// Mismatch entry structure to store reference data discrepancies
struct MismatchEntry {
    int refrIndexJson;    // Reference index from JSON
    std::string idJson;   // Object ID from JSON
    std::string idDb;     // Expected ID from database
    int refrIndexDb;      // Expected reference index from database

    bool operator==(const MismatchEntry& other) const noexcept {
        return refrIndexJson == other.refrIndexJson && idJson == other.idJson;
    }
};

// Hash function specialization for MismatchEntry
namespace std {
    template<> struct hash<MismatchEntry> {
        size_t operator()(const MismatchEntry& e) const {
            size_t h1 = hash<int>{}(e.refrIndexJson);
            size_t h2 = hash<string>{}(e.idJson);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };
}

// Container for tracking unique mismatch entries using unordered_set
std::unordered_set<MismatchEntry> mismatchedEntries;

// Function to process replacements and mismatches
int processReplacementsAndMismatches(const Database& db, const ProgramOptions& options, const std::string& query, ordered_json& inputData,
    int conversionChoice, int& replacementsFlag,
    const std::unordered_set<int>& validMastersDb,
    std::unordered_set<MismatchEntry>& mismatchedEntries,
    std::ofstream& logFile) {

    // Validate root JSON structure
    if (!inputData.is_array()) {
        logMessage("\nERROR - input JSON is not an array, unable to process!", logFile);
        return -1;
    }

    // Process each cell in the JSON array
    for (auto cellIter = inputData.begin(); cellIter != inputData.end(); ++cellIter) {

        // Skip non-cell entries or cells without proper type
        if (!cellIter->contains("type") || (*cellIter)["type"] != "Cell") continue;

        // Extract references array from cell
        auto& cellReferences = (*cellIter)["references"];
        if (!cellReferences.is_array()) continue;

        // Process individual references in cell
        for (auto refIter = cellReferences.begin(); refIter != cellReferences.end(); ++refIter) {
            auto& referenceData = *refIter;

            // Validate reference structure
            if (!referenceData.contains("refr_index") || !referenceData["refr_index"].is_number_integer() ||
                !referenceData.contains("id") || !referenceData["id"].is_string()) {
                continue;
            }

            // Extract reference data
            int inputRefIndex = referenceData["refr_index"];
            std::string inputId = referenceData["id"];
            int inputMastIndex = referenceData.value("mast_index", -1);

            // Valid Parent Master files check
            if (!validMastersIn.count(inputMastIndex)) {
                //if (!options.silentMode) {
                    //logMessage("Skipping object (invalid master index): " + inputId, logFile);
                //}
                continue;
            }

            // Handle replacements
            if (auto foundRefIndex = fetchRefIndex(db, query, inputRefIndex, inputId)) {
                referenceData["refr_index"] = *foundRefIndex;
                if (!options.silentMode) {
                    logMessage("Replaced JSON refr_index " + std::to_string(inputRefIndex) +
                               " with DB refr_index " + std::to_string(*foundRefIndex) +
                               " for JSON id " + inputId, logFile);
                }
                replacementsFlag = 1;
            }

            // Handle mismatches
            else {
                const int refrIndexDb = fetchID<FETCH_OPPOSITE_REFR_INDEX>(db, inputRefIndex, inputMastIndex, validMastersDb, conversionChoice);

                // Skip if no matching record found in DB
                if (refrIndexDb == -1) {
                    //if (!options.silentMode) {
                        //logMessage("Skipping object (no match in DB): JSON refr_index " + std::to_string(inputRefIndex) +
                        //           " and JSON id " + inputId, logFile);
                    //}
                    continue;
                }

                const std::string idDb = fetchID<FETCH_DB_ID>(db, inputRefIndex, inputMastIndex, validMastersDb, conversionChoice);

                // Only proceed with mismatch handling if we have valid DB data
                if (!options.silentMode) {
                    logMessage("Mismatch found for JSON refr_index " + std::to_string(inputRefIndex) +
                               " and JSON id " + inputId + " with DB refr_index " + std::to_string(refrIndexDb) +
                               " and DB id " + idDb, logFile);
                }

                // Handle duplicated mismatches
                if (auto [it, inserted] = mismatchedEntries.insert(
                    MismatchEntry{ inputRefIndex, inputId, idDb, refrIndexDb }); !inserted) {
                    logMessage("WARNING - skipped duplicate mismatch entry for JSON refr_index " + std::to_string(inputRefIndex) +
                               " and JSON id " + inputId, logFile);
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
                        if (reference["refr_index"] == entry.refrIndexJson &&
                            reference.value("id", "") == entry.idJson) {
                            reference["refr_index"] = entry.refrIndexDb;
                            if (!options.silentMode) {
                                logMessage("Replaced mismatched JSON refr_index " + std::to_string(entry.refrIndexJson) +
                                           " with DB refr_index " + std::to_string(entry.refrIndexDb) +
                                           " for JSON id " + entry.idJson, logFile);
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

// Function to add conversion tag to the header description
bool addConversionTag(ordered_json& inputData, const std::string& convPrefix, std::ofstream& logFile) {
    // Find the Header block in JSON
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const auto& item) {
        return item.contains("type") && item["type"] == "Header";
    });

    if (headerIter != inputData.end() && headerIter->contains("description")) {
        // Get current description
        std::string currentDesc = (*headerIter)["description"];

        // Add conversion tag
        std::string conversionTag = "\r\n\r\nConverted (" + convPrefix + ") by TES3 Ref_Ind Converter";
        if (currentDesc.find(conversionTag) == std::string::npos) {
            (*headerIter)["description"] = currentDesc + conversionTag;
            logMessage("\nAdding conversion tag to the file header...", logFile);
        }
        return true;
    }

    return false;
}

// Function to create backup with automatic numbering
bool createBackup(const std::filesystem::path& filePath, std::ofstream& logFile) {
    std::filesystem::path backupPath;
    int counter = 0;
    const int maxBackups = 1000;

    try {
        // First try simple .bac extension
        backupPath = filePath;
        backupPath += ".bac";

        // If simple backup exists, find next available numbered version
        while (std::filesystem::exists(backupPath) && counter < maxBackups) {
            backupPath = filePath;
            backupPath += std::format(".{:03d}.bac", counter++);
        }

        // Safety check to prevent infinite loops
        if (counter >= maxBackups) {
            logMessage("ERROR - reached maximum backup count (" + std::to_string(maxBackups) +
                       ") for file: " + filePath.string(), logFile);
            return false;
        }

        // Perform the actual backup by renaming the file
        std::filesystem::rename(filePath, backupPath);
        logMessage("Original file backed up as: " + backupPath.string(), logFile);
        return true;
    }
    catch (const std::exception& e) {
        // Log any errors that occur during backup process
        logMessage("ERROR - failed to create backup: " + filePath.string() +
                   ": " + e.what(), logFile);
        return false;
    }
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
    std::ostringstream command;
    command << TES3CONV_COMMAND << " "
            << std::quoted(jsonImportPath.string()) << " "
            << std::quoted(espFilePath.string());

    if (std::system(command.str().c_str()) != 0) {
        return false;
    }

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
        logFile.close();
        return EXIT_FAILURE;
    }

    // Clear log file
    logClear();
    if (!options.silentMode) {
        logMessage("Log file cleared...", logFile);
    }

    // Check if the database file exists
    if (!std::filesystem::exists("tes3_ri_en-ru_refr_index.db")) {
        logErrorAndExit("ERROR - database file 'tes3_ri_en-ru_refr_index.db' not found!\n", logFile);
    }

    Database db("tes3_ri_en-ru_refr_index.db");

    // Log successful connection if not in silent mode
    if (!options.silentMode) {
        logMessage("Database opened successfully...", logFile);
    }

    // Check if the converter executable exists
    if (!std::filesystem::exists(TES3CONV_COMMAND)) {
        logErrorAndExit("ERROR - tes3conv not found! Please download the latest version from\n"
                        "github.com/Greatness7/tes3conv/releases and place it in the same directory\n"
                        "with this program.\n", logFile);
    }

    if (!options.silentMode) {
        logMessage("tes3conv found...\n"
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
        validMastersIn.clear();
        validMastersDb.clear();
        mismatchedEntries.clear();

        logMessage("\nProcessing file: " + pluginImportPath.string(), logFile);

        try {
            // Define the output file path
            std::filesystem::path jsonImportPath = pluginImportPath.parent_path() / (pluginImportPath.stem().string() + ".json");

            // Convert the input file to .JSON
            std::ostringstream convCmd;
            convCmd << TES3CONV_COMMAND << " "
                    << std::quoted(pluginImportPath.string()) << " "
                    << std::quoted(jsonImportPath.string());

            if (std::system(convCmd.str().c_str()) != 0) {
                logMessage("ERROR - converting to .JSON failed for file: " + pluginImportPath.string(), logFile);
                continue;
            }
            logMessage("Conversion to .JSON successful: " + jsonImportPath.string(), logFile);

            // Load the generated JSON file
            std::ifstream inputFile(jsonImportPath, std::ios::binary);
            if (!inputFile.is_open()) {
                logMessage("ERROR - failed to open JSON file: " + jsonImportPath.string(), logFile);
                continue;
            }

            inputFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

            ordered_json inputData;
            try {
                inputFile >> inputData;

                if (inputData.is_discarded()) {
                    logMessage("ERROR - parsed JSON is invalid or empty: " + jsonImportPath.string(), logFile);
                    continue;
                }
            }
            catch (const std::exception& e) {
                logMessage("ERROR - failed to parse JSON (" + jsonImportPath.string() + "): " + e.what(), logFile);
                continue;
            }

            inputFile.close();

            // Check if file was already converted
            if (hasConversionTag(inputData, pluginImportPath, logFile)) {
                std::filesystem::remove(jsonImportPath);
                logMessage("File " + pluginImportPath.string() + " was already converted - conversion skipped...", logFile);
                logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                continue;
            }

            // Check the dependency order
            auto [isValid, validMasters] = checkDependencyOrder(inputData, logFile);
            if (!isValid) {
                std::filesystem::remove(jsonImportPath);
                logMessage("ERROR - required Parent Master files dependency not found, or their order is invalid for file: " + pluginImportPath.string(), logFile);
                logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
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
                logMessage("No replacements found for file: " + pluginImportPath.string() + " - conversion skipped...\n", logFile);
                logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                continue;
            }

            // Define conversion prefix
            std::string convPrefix = (options.conversionType == 1) ? "RU->EN" : "EN->RU";

            // Add conversion tag to header
            if (!addConversionTag(inputData, convPrefix, logFile)) {
                logMessage("ERROR - could not find or modify header description", logFile);
                continue;
            }

            // Save the modified data to .JSON file
            auto newJsonName = std::format("TEMP_{}{}", pluginImportPath.stem().string(), ".json");
            std::filesystem::path jsonExportPath = pluginImportPath.parent_path() / newJsonName;

            if (!saveJsonToFile(jsonExportPath, inputData, logFile)) {
                logMessage("ERROR - failed to save modified data to .JSON file: " + jsonExportPath.string(), logFile);
                continue;
            }

            // Create backup before modifying original file
            if (!createBackup(pluginImportPath, logFile)) {
                std::filesystem::remove(jsonImportPath);
                logMessage("Temporary .JSON file deleted: " + jsonImportPath.string(), logFile);
                continue;
            }

            // Save converted file with original name
            if (!convertJsonToEsp(jsonExportPath, pluginImportPath, logFile)) {
                logMessage("ERROR - failed to convert .JSON back to .ESP|ESM: " + pluginImportPath.string(), logFile);
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
            validMastersIn.clear();
            validMastersDb.clear();
            mismatchedEntries.clear();
            continue;
        }
    }

    // Time total
    auto programEnd = std::chrono::high_resolution_clock::now();
    auto programDuration = std::chrono::duration_cast<std::chrono::milliseconds>(programEnd - programStart);

    logMessage("\nTotal processing time: " + std::to_string(programDuration.count() / 1000.0) + " seconds", logFile);

    // Close the database
    if (!options.silentMode) {
        logMessage("\nThe ending of the words is ALMSIVI", logFile);
        logFile.close();

        // Wait for user input before exiting (Windows)
        #ifndef __linux__
        std::cout << "\nPress Enter to continue...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        #endif
    }

    return EXIT_SUCCESS;
}