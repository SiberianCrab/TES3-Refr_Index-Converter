#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <cctype>
#include <cstdlib>

#include <json.hpp>
#include <sqlite3.h>

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.2.2";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";
const std::string PROGRAM_TESTER = "Beta testing by Pirate443";

// Define sets to store valid master indices and masters from the database
std::unordered_set<int> validMastersIN;
std::unordered_set<int> validMastersDB;

// Function to clear log file
void logClear() {
    std::ofstream file("tes3_ri_log.txt", std::ios::trunc);
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

// Function for handling conversion choises
int getUserConversionChoice(std::ofstream& logFile) {
    return getUserChoice(
        "\nConvert refr_index values in a plugin or master file:\n"
        "1. From Russian 1C to English GOTY\n"
        "2. From English GOTY to Russian 1C\n"
        "Choice: ",
        { "1", "2" }, logFile
    );
}

// Function for handling mismatches
int getUserMismatchChoice(std::ofstream& logFile) {
    return getUserChoice(
        "\nMismatched entries found (usually occur if a Tribunal or Bloodmoon object was modified with\n"
        "'Edit -> Search & Replace' in TES3 CS). Would you like to replace their refr_index anyway?\n"
        "1. Yes\n"
        "2. No\n"
        "Choice: ",
        { "1", "2" }, logFile
    );
}

// Function for handling input file path from user
std::filesystem::path getInputFilePath(std::ofstream& logFile) {
    std::filesystem::path filePath;
    while (true) {
        std::cout << "\nEnter full path to your .ESP|ESM or just filename (with extension), if your .ESP|ESM is in the same directory\n"
                     "with this program: ";
        std::string input;
        std::getline(std::cin, input);
        filePath = input;

        // Convert the file extension to lowercase for case-insensitive comparison
        std::string extension = filePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (std::filesystem::exists(filePath) &&
            (extension == ".esp" || extension == ".esm")) {
            logMessage("Input file found: " + filePath.string(), logFile);
            break;
        }
        logMessage("\nERROR - input file not found: check its directory, name and extension!", logFile);
    }
    return filePath;
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

// Template function to fetch values from the database based on the fetch mode
template <FetchMode mode>
auto fetchValue(sqlite3* db, int refrIndexJSON, int mastIndex, const std::unordered_set<int>& validMastersDB, int conversionChoice) {
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
int processReplacementsAndMismatches(sqlite3* db, const std::string& query, ordered_json& inputData,
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

        // Quick validation check before deep processing
        const bool has_refs = !references.empty();
        const bool needs_processing = has_refs && references[0].contains("refr_index") && references[0].contains("id");

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
                logMessage("Replaced JSON refr_index " + std::to_string(refrIndexExtracted) +
                           " with DB refr_index " + std::to_string(*new_refrIndex) +
                           " for JSON id " + idExtracted, logFile);
                replacementsFlag = 1;
            }

            // Handle mismatches
            else {
                const int refrIndexDB = fetchValue<FETCH_OPPOSITE_REFR_INDEX>(db, refrIndexExtracted, mastIndexExtracted, validMastersDB, conversionChoice);
                const std::string idDB = fetchValue<FETCH_DB_ID>(db, refrIndexExtracted, mastIndexExtracted, validMastersDB, conversionChoice);
                logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndexExtracted) +
                           " and JSON id " + idExtracted + " with DB refr_index " + std::to_string(refrIndexDB) +
                           " and DB id " + idDB, logFile);

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
    int mismatchChoice = getUserMismatchChoice(logFile);

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
                        logMessage("Replaced mismatched JSON refr_index " + std::to_string(entry.refrIndexJSON) +
                                   " with DB refr_index " + std::to_string(entry.refrIndexDB) +
                                   " for JSON id " + entry.idJSON, logFile);
                        replacementsFlag = 1;
                    }
                }
            }
        }
    }
    else {
        logMessage("\nMismatched entries will remain unchanged...", logFile);
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
int main() {
    // Display program information
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n" << PROGRAM_TESTER << "\n\n";

    // Log file initialisation
    std::ofstream logFile("tes3_ri_log.txt", std::ios::app);
    if (!logFile) {
        std::cerr << "ERROR - failed to open log file!\n\n"
                     "Press Enter to exit...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::exit(EXIT_FAILURE);
    }

    // Clear log file
    logClear();
    logMessage("Log file cleared...", logFile);

    // Check if the database file exists
    if (!std::filesystem::exists("tes3_ri_en-ru_refr_index.db")) {
        logErrorAndExit(nullptr, "ERROR - database file 'tes3_ri_en-ru_refr_index.db' not found!\n", logFile);
    }

    // Open the database
    sqlite3* db = nullptr;
    if (sqlite3_open("tes3_ri_en-ru_refr_index.db", &db)) {
        logErrorAndExit(db, "ERROR - failed to open database: " + std::string(sqlite3_errmsg(db)) + "!\n", logFile);
    }
    logMessage("Database opened successfully...", logFile);

    // Check if the converter executable exists
    if (!std::filesystem::exists("tes3conv.exe")) {
        logErrorAndExit(db, "ERROR - tes3conv.exe not found! Please download the latest version from\n"
                            "github.com/Greatness7/tes3conv/releases and place it in the same directory\n"
                            "with this program.\n", logFile);
    }
    logMessage("tes3conv.exe found...\n"
               "Initialisation complete.", logFile);

    // Get the conversion choice from user
    int conversionChoice = getUserConversionChoice(logFile);

    // Get the input file path from user
    std::filesystem::path pluginImportPath = getInputFilePath(logFile);

    // Define the output file path
    std::filesystem::path jsonImportPath = pluginImportPath.parent_path() / (pluginImportPath.stem().string() + ".json");

    // Convert the input file to .JSON
    std::string command = "tes3conv.exe \"" + pluginImportPath.string() + "\" \"" + jsonImportPath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "ERROR - converting to .JSON failed!\n", logFile);
    }
    logMessage("Conversion to .JSON successful: " + jsonImportPath.string(), logFile);

    // Load the generated JSON file into a JSON object
    std::ifstream inputFile(jsonImportPath);
    ordered_json inputData;
    inputFile >> inputData;
    inputFile.close();

    // Check the dependency order of the Parent Master files in the input data
    auto [isValid, validMasters] = checkDependencyOrder(inputData, logFile);
    if (!isValid) {
        std::filesystem::remove(jsonImportPath);
        logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
        logErrorAndExit(db, "ERROR - required Parent Master files dependency not found, or theit order is invalid!\n", logFile);
    }

    // Initialize the replacements flag
    int replacementsFlag = 0;

    // Initialize the query based on user conversion choice
    std::string dbQuery = (conversionChoice == 1)
        ? "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ? AND id = ?;"
        : "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    // Process replacements
    if (processReplacementsAndMismatches(db, dbQuery, inputData, conversionChoice, replacementsFlag, validMasters, mismatchedEntries, logFile) == -1) {
        logErrorAndExit(db, "ERROR - processing failed!", logFile);
    }

    // Check if any replacements were made: if no replacements were found, cancel the conversion
    if (replacementsFlag == 0) {
        std::filesystem::remove(jsonImportPath);
        logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
        logErrorAndExit(db, "No replacements found: conversion canceled\n", logFile);
    }

    // Define conversion prefix
    std::string convPrefix = (conversionChoice == 1) ? "RUtoEN" : "ENtoRU";

    // Save the modified data to.JSON file
    auto newJsonName = std::format("CONV_{}_{}{}", convPrefix, pluginImportPath.stem().string(), ".json");

    std::filesystem::path jsonExportPath = pluginImportPath.parent_path() / newJsonName;
    if (!saveJsonToFile(jsonExportPath, inputData, logFile)) {
        logErrorAndExit(db, "ERROR - failed to save modified data to .JSON file!\n", logFile);
    }

    // Convert the .JSON file back to .ESP|ESM
    auto pluginExportName = std::format("CONV_{}_{}{}", convPrefix, pluginImportPath.stem().string(), pluginImportPath.extension().string());

    std::filesystem::path pluginExportPath = pluginImportPath.parent_path() / pluginExportName;
    if (!convertJsonToEsp(jsonExportPath, pluginExportPath, logFile)) {
        logErrorAndExit(db, "ERROR - failed to convert .JSON back to .ESP|ESM!\n", logFile);
    }

    // Clean up temporary .JSON files
    std::filesystem::remove(jsonImportPath);
    std::filesystem::remove(jsonExportPath);
    logMessage("Temporary .JSON files deleted: " + jsonImportPath.string() + "\n" +
               "                          and: " + jsonExportPath.string() + "\n", logFile);

    // Close the database
    sqlite3_close(db);
    logMessage("The ending of the words is ALMSIVI\n", logFile);
    logFile.close();

    // Wait for user input before exiting
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}