#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>
#include <sqlite3.h>
#include <json.hpp>

// Define an alias for ordered JSON type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.3";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

// Define sets to store valid master indices and masters from the database
std::unordered_set<int> validMastIndices;
std::unordered_set<int> validMastersDB;

// Function to log messages to both a log file and console
void logMessage(const std::string& message, const std::filesystem::path& logFilePath = "tes3_ri_log.txt") {
    std::ofstream logFile(logFilePath, std::ios_base::app);

    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
    else {
        std::cerr << "Failed to open log file." << std::endl;
    }

    std::cout << message << std::endl;
}

// Function to log errors, close the database (if open), and terminate the program
void logErrorAndExit(sqlite3* db, const std::string& message) {
    logMessage(message);

    if (db) {
        sqlite3_close(db);
    }

    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    throw std::runtime_error(message);
}

// Function to clear the log file
void clearLogFile(const std::filesystem::path& logFileName = "tes3_ri_log.txt") {
    if (std::filesystem::exists(logFileName)) {
        try {
            std::filesystem::remove(logFileName);
            logMessage("Log cleared successfully...", logFileName);
        }
        catch (const std::filesystem::filesystem_error& e) {
            logMessage("Error clearing log file: " + std::string(e.what()), logFileName);
        }
    }
}

// Function to get the user's conversion choice
int getUserConversionChoice() {
    int ConversionChoice;
    while (true) {
        std::cout << "Convert refr_index data in a plugin or master file:\n"
            "1. From Russian 1C to English GOTY\n2. From English GOTY to Russian 1C\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            ConversionChoice = std::stoi(input);
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return ConversionChoice;
}

// Function to get the user's choice for handling mismatched entries
int getUserMismatchChoice() {
    int mismatchChoice;
    while (true) {
        std::cout << "\nMismatched entries found (usually occur if a Tribunal or Bloodmoon object was modified with\n"
            "'Edit -> Search & Replace' in TES3 CS). Would you like to replace their refr_index anyway?\n"
            "1.Yes\n2.No\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            mismatchChoice = std::stoi(input);
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return mismatchChoice;
}

// Function to get the input file path from the user
std::filesystem::path getInputFilePath() {
    std::filesystem::path filePath;
    while (true) {
        std::cout << "Enter full path to your .ESP or .ESM (including extension), or filename (with extension)\n"
            "if it's in the same directory with this program: ";
        std::string input;
        std::getline(std::cin, input);
        filePath = input;

        // Convert the file extension to lowercase for case-insensitive comparison
        std::string extension = filePath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (std::filesystem::exists(filePath) &&
            (extension == ".esp" || extension == ".esm")) {
            logMessage("Input file found: " + filePath.string());
            break;
        }
        logMessage("Input file not found or incorrect extension.");
    }
    return filePath;
}

// Function to check the dependency order of the masters in the input data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const ordered_json& inputData) {
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    if (headerIter == inputData.end() || !headerIter->contains("masters")) {
        logMessage("Error: Missing 'Header' section or 'masters' key.");
        return { false, {} };
    }

    const auto& masters = (*headerIter)["masters"];

    size_t mwPos = static_cast<size_t>(-1), tPos = static_cast<size_t>(-1), bPos = static_cast<size_t>(-1);

    for (size_t i = 0; i < masters.size(); ++i) {
        if (masters[i].is_array() && !masters[i].empty() && masters[i][0].is_string()) {
            std::string masterName = masters[i][0];
            if (masterName == "Morrowind.esm") mwPos = i;
            else if (masterName == "Tribunal.esm") tPos = i;
            else if (masterName == "Bloodmoon.esm") bPos = i;
        }
    }

    if (mwPos == static_cast<size_t>(-1)) {
        logMessage("Morrowind.esm not found!");
        return { false, {} };
    }

    if (tPos != static_cast<size_t>(-1) && bPos != static_cast<size_t>(-1)) {
        if (tPos > mwPos && bPos > tPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.\n");
            validMastIndices = { 2, 3 };
            validMastersDB = { 1 };
            return { true, validMastersDB };
        }
        else {
            logMessage("Invalid order of Parent Masters! Tribunal.esm should be before Bloodmoon.esm.\n");
            return { false, {} };
        }
    }

    if (tPos != static_cast<size_t>(-1) && tPos > mwPos) {
        logMessage("Valid order of Parent Masters found: M+T.\n");
        validMastIndices = { 2 };
        validMastersDB = { 2 };
        return { true, validMastersDB };
    }

    if (bPos != static_cast<size_t>(-1) && bPos > mwPos) {
        logMessage("Valid order of Parent Masters found: M+B.\n");
        validMastIndices = { 2 };
        validMastersDB = { 3 };
        return { true, validMastersDB };
    }

    return { false, {} };
}

// Function to fetch the refr_index from the database
int fetchRefIndex(sqlite3* db, const std::string& query, int refrIndex, const std::string& id) {
    int result = -1;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, refrIndex);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0);
        }
    }
    else {
        logMessage("Database query error: " + std::string(sqlite3_errmsg(db)));
    }

    sqlite3_finalize(stmt);
    return result;
}
// Define an enumeration for fetch modes
enum FetchMode {
    FETCH_DB_ID,
    FETCH_OPPOSITE_REFR_INDEX
};

// Template function to fetch values from the database based on the fetch mode
template <FetchMode mode>
auto fetchValue(sqlite3* db, int refrIndex, int mastIndex, const std::unordered_set<int>& validMastersDB, int conversionChoice)
-> std::conditional_t<mode == FETCH_DB_ID, std::string, int> {
    std::string query;

    // Determine the query based on the conversion choice and fetch mode
    switch (conversionChoice) {
    case 1:
        query = (mode == FETCH_DB_ID) ? "SELECT ID FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ?"
            : "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ?";
        break;
    case 2:
        query = (mode == FETCH_DB_ID) ? "SELECT ID FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ?"
            : "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ?";
        break;
    default:
        std::cerr << "Invalid conversion choice." << std::endl;
        if constexpr (mode == FETCH_DB_ID) {
            return std::string();
        }
        else {
            return -1;
        }
    }

    // Append conditions to the query based on the valid masters
    if (validMastersDB.count(1)) {
        if (mastIndex == 2) {
            query += " AND Master = 'Tribunal'";
        }
        else if (mastIndex == 3) {
            query += " AND Master = 'Bloodmoon'";
        }
    }
    else if (validMastersDB.count(2)) {
        query += " AND Master = 'Tribunal'";
    }
    else if (validMastersDB.count(3)) {
        query += " AND Master = 'Bloodmoon'";
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        if constexpr (mode == FETCH_DB_ID) {
            return std::string();
        }
        else {
            return -1;
        }
    }

    sqlite3_bind_int(stmt, 1, refrIndex);

    // Fetch the value based on the fetch mode
    if constexpr (mode == FETCH_DB_ID) {
        std::string dbId;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (id) {
                dbId = id;
            }
        }
        else {
            std::cerr << "No matching id found for refr_index: " << refrIndex << std::endl;
        }
        sqlite3_finalize(stmt);
        return dbId;
    }
    else {
        int dbRefrIndex = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            dbRefrIndex = sqlite3_column_int(stmt, 0);
        }
        else {
            std::cerr << "No matching DB refr_index found for JSON refr_index: " << refrIndex << std::endl;
        }
        sqlite3_finalize(stmt);
        return dbRefrIndex;
    }
}

// Define a structure to hold mismatch entries
struct MismatchEntry {
    int refrIndex;
    std::string id;
    std::string dbId;
    int dbRefrIndex;
};

// Vector to store mismatched entries
std::vector<MismatchEntry> mismatchedEntries;

// Function to process replacements and mismatches
int processReplacementsAndMismatches(sqlite3* db, const std::string& query, ordered_json& inputData,
    int conversionChoice, int& replacementsFlag,
    const std::unordered_set<int>& validMastersDB,
    std::vector<MismatchEntry>& mismatchedEntries) {

    if (inputData.is_array()) {
        for (auto& cell : inputData) {
            if (cell.contains("type") && cell["type"] == "Cell") {
                if (cell.contains("references") && cell["references"].is_array()) {
                    for (auto& reference : cell["references"]) {
                        if (!reference.contains("refr_index") || !reference["refr_index"].is_number_integer() ||
                            !reference.contains("id") || !reference["id"].is_string()) {
                            logMessage("Skipping invalid reference: missing 'refr_index' or 'id'.");
                            continue;
                        }

                        int refrIndex = reference["refr_index"];
                        std::string id = reference["id"];

                        int currentMastIndex = reference.contains("mast_index") && reference["mast_index"].is_number_integer()
                            ? reference["mast_index"].get<int>()
                            : -1;

                        int newRefrIndex = fetchRefIndex(db, query, refrIndex, id);

                        if (newRefrIndex != -1) {
                            reference["refr_index"] = newRefrIndex;

                            logMessage("Replaced JSON refr_index " + std::to_string(refrIndex) +
                                " with DB refr_index " + std::to_string(newRefrIndex) +
                                " for JSON id: " + id);

                            replacementsFlag = 1;
                        }
                        else if (currentMastIndex == 2 || currentMastIndex == 3) {
                            int dbRefrIndex = fetchValue<FETCH_OPPOSITE_REFR_INDEX>(
                                db, refrIndex, currentMastIndex, validMastersDB, conversionChoice);
                            std::string dbId = fetchValue<FETCH_DB_ID>(
                                db, refrIndex, currentMastIndex, validMastersDB, conversionChoice);

                            mismatchedEntries.emplace_back(MismatchEntry{ refrIndex, id, dbId, dbRefrIndex });

                            logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndex) +
                                " and JSON id: " + id + " with DB refr_index: " + std::to_string(dbRefrIndex) +
                                " and DB id: " + dbId);
                        }
                    }
                }
                else {
                    logMessage("Skipping 'Cell' object as it does not contain valid 'references' array.");
                }
            }
        }
    }
    else {
        logMessage("\nInput JSON is not an array, unable to process.");
        return -1;
    }

    if (mismatchedEntries.empty()) {
        logMessage("\nNo mismatches found. Skipping mismatch handling.");
        return 0;
    }

    // Handle mismatched entries
    int mismatchChoice = getUserMismatchChoice();

    if (mismatchChoice == 1) {
        for (const auto& entry : mismatchedEntries) {
            int refrIndex = entry.refrIndex;
            int dbRefrIndex = entry.dbRefrIndex;

            if (dbRefrIndex != -1) {

                for (auto& cell : inputData) {
                    if (cell.contains("references") && cell["references"].is_array()) {
                        for (auto& reference : cell["references"]) {
                            if (reference["refr_index"] == refrIndex) {
                                reference["refr_index"] = dbRefrIndex;
                                break;
                            }
                        }
                    }
                }

                logMessage("Replaced mismatched JSON refr_index " + std::to_string(refrIndex) +
                    " with DB refr_index: " + std::to_string(dbRefrIndex));

                replacementsFlag = 1;
            }
        }
    }
    else {
        logMessage("\nMismatched entries will remain unchanged.");
    }

    return 0;
}

// Function to save the modified JSON to a file
bool saveJsonToFile(const std::filesystem::path& jsonFilePath, const ordered_json& inputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile) {
        outputFile << std::setw(2) << inputData;
        logMessage("\nModified JSON saved as: " + jsonFilePath.string() + "\n");
        return true;
    }
    return false;
}

// Function to convert the JSON file to an ESP file
bool convertJsonToEsp(const std::filesystem::path& jsonFilePath, const std::filesystem::path& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath.string() + "\" \"" + espFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        return false;
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath.string() + "\n");
    return true;
}

// Main function
int main() {
    // Display program information
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Clear the log file
    clearLogFile("tes3_ri_log.txt");

    // Check if the database file exists
    std::filesystem::path dbFilePath = "tes3_ri_en-ru_refr_index.db";
    if (!std::filesystem::exists(dbFilePath)) {
        logErrorAndExit(nullptr, "Database file 'tes3_ri_en-ru_refr_index.db' not found.\n");
    }

    // Open the database
    sqlite3* db = nullptr;
    if (sqlite3_open(dbFilePath.string().c_str(), &db)) {
        logErrorAndExit(db, "Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
    }
    logMessage("Database opened successfully...");

    // Check if the converter executable exists
    std::filesystem::path converterPath = "tes3conv.exe";
    if (!std::filesystem::exists(converterPath)) {
        logErrorAndExit(db, "tes3conv.exe not found. Please download the latest version from\n"
            "https://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory with this program.\n");
    }
    logMessage("tes3conv.exe found...\nInitialisation complete.\n");

    // Get the user's conversion choice
    int ConversionChoice = getUserConversionChoice();

    // Get the input file path from the user
    std::filesystem::path inputFilePath = getInputFilePath();
    std::filesystem::path inputPath(inputFilePath);

    // Define the output directory and JSON file path
    std::filesystem::path outputDir = inputPath.parent_path();
    std::filesystem::path jsonFilePath = outputDir / (inputPath.stem() += ".json");

    // Convert the input file to JSON
    std::string command = "tes3conv.exe \"" + inputPath.string() + "\" \"" + jsonFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "Error converting to JSON. Check tes3conv.exe and the input file.\n");
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath.string());

    // Read the JSON file
    std::ifstream inputFile(jsonFilePath);
    if (!inputFile) {
        logErrorAndExit(db, "Failed to open JSON file for reading: " + jsonFilePath.string() + "\n");
    }
    ordered_json inputData;
    inputFile >> inputData;
    inputFile.close();

    // Initialize the replacements flag
    int replacementsFlag = 0;

    // Check the dependency order of the masters in the input data
    auto [isValid, validMastersDB] = checkDependencyOrder(inputData);
    if (!isValid) {
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }
        logErrorAndExit(db, "Required Parent Masters not found or are in the wrong order.\n");
    }

    // Define the query based on the conversion choice
    std::string query = (ConversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    // Process replacements and mismatches
    processReplacementsAndMismatches(db, query, inputData, ConversionChoice, replacementsFlag, validMastersDB, mismatchedEntries);

    // Check if any replacements were made
    if (replacementsFlag == 0) {
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }
        logErrorAndExit(db, "No replacements found. Conversion canceled.\n");
    }

    // Define the new JSON file path
    std::filesystem::path newJsonFilePath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json");

    // Save the modified JSON to a file
    if (!saveJsonToFile(newJsonFilePath, inputData)) {
        logErrorAndExit(db, "Error saving modified JSON file.\n");
    }

    // Define the output file path for the converted ESP/ESM
    std::filesystem::path outputExtension = inputPath.extension();
    std::filesystem::path newEspPath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension.string());

    // Convert the JSON file back to ESP/ESM
    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        logErrorAndExit(db, "Error converting JSON back to ESM/ESP.\n");
    }

    // Clean up temporary JSON files
    if (std::filesystem::exists(jsonFilePath)) std::filesystem::remove(jsonFilePath);
    if (std::filesystem::exists(newJsonFilePath)) std::filesystem::remove(newJsonFilePath);
    logMessage("Temporary JSON files deleted: " + jsonFilePath.string() + "\n" + 
               "                         and: " + newJsonFilePath.string() + "\n");

    // Close the database
    sqlite3_close(db);
    logMessage("The ending of the words is ALMSIVI.\n");

    // Wait for user input before exiting
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}