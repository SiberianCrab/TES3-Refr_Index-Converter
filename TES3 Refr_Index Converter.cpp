#include <iostream>               // For standard input and output operations
#include <fstream>                // For file input and output operations
#include <sstream>                // For working with string streams
#include <string>                 // For string manipulation and handling
#include <regex>                  // For regular expressions
#include <limits>                 // For setting input limits and working with numeric limits
#include <regex>                  // For regular expressions (pattern matching)
#include <unordered_map>          // For efficient key-value pair storage (hash map)
#include <unordered_set>          // For efficient storage of unique elements (hash set)
#include <cctype>                 // For character handling functions
#include <optional>               // For optional values, used for error handling
#include <vector>                 // For dynamic arrays or lists
#include <filesystem>             // For interacting with the file system (directories, files)
#include <sqlite3.h>              // For interacting with SQLite databases
#include <json.hpp>               // For working with JSON data (nlohmann's JSON library)

// Define an alias for ordered JSON type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Define program metadata constants
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.3";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

// Sets to store valid indices and master indices from the database
std::unordered_set<int> validMastIndices;
std::unordered_set<int> validMastersDB;
std::unordered_map<int, int> replacements; // Map for replacement indices

// Structure to store mismatched entries with their respective data
struct MismatchEntry {
    int refrIndex;               // Reference index
    std::string id;              // ID of the mismatched entry
    std::string dbId;            // Database ID
    int dbRefrIndex;             // Opposite reference index
};

// Vector to store all mismatched entries
std::vector<MismatchEntry> mismatchedEntries;

// Function to log messages to both a log file and console
void logMessage(const std::string& message, const std::filesystem::path& logFilePath = "tes3_ric_log.txt") {
    std::ofstream logFile(logFilePath, std::ios_base::app);

    // Check if the file opened successfully and write the message
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

    // Close the SQLite database if it is open to avoid memory leaks
    if (db) {
        sqlite3_close(db);
    }

    // Prompt the user to press Enter to continue and clear any input buffer
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // Throw a runtime error to exit the program and propagate the error
    throw std::runtime_error(message);
}

// UP - Function to clear the log file if it exists, and log the status
void clearLogFile(const std::filesystem::path& logFileName = "tes3_ric_log.txt") {
    // Check if the log file exists before trying to remove it
    if (std::filesystem::exists(logFileName)) {
        try {
            std::filesystem::remove(logFileName);
            logMessage("Log cleared successfully...", logFileName);
        }
        catch (const std::filesystem::filesystem_error& e) {
            // Log any error that occurs during the file removal process
            logMessage("Error clearing log file: " + std::string(e.what()), logFileName);
        }
    }
}

// Function to get user input for conversion choice
int getUserConversionChoice() {
    int ConversionChoice;
    while (true) {
        std::cout << "Convert refr_index data in a plugin or master file:\n"
            "1. From Russian 1C to English GOTY\n2. From English GOTY to Russian 1C\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            ConversionChoice = input[0] - '0'; // Convert char to int
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return ConversionChoice;
}

// Function to get user input for handling mismatched entries
int getUserMismatchChoice() {
    int mismatchChoice;
    while (true) {
        std::cout << "\nMismatched entries found (usually occur if a Tribunal or Bloodmoon object was modified with\n"
            "'Edit -> Search & Replace' in TES3 CS). Would you like to replace their refr_index anyway?\n"
            "1.Yes\n2.No\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            mismatchChoice = std::stoi(input); // Convert string to int
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return mismatchChoice;
}

// Function to get the path of the input file from the user
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

// Function to check the order of dependencies in a file's data
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const nlohmann::ordered_json& inputData) {
    // Look for the "Header" section in the JSON
    auto headerIter = std::find_if(inputData.begin(), inputData.end(), [](const nlohmann::ordered_json& item) {
        return item.contains("type") && item["type"] == "Header";
        });

    // Check if we found the "Header" section
    if (headerIter == inputData.end() || !headerIter->contains("masters")) {
        logMessage("Error: Missing 'Header' section or 'masters' key.");
        return { false, {} };
    }

    // Extract the list of masters
    const auto& masters = (*headerIter)["masters"];
    if (!masters.is_array()) {
        logMessage("Error: 'masters' is not an array.");
        return { false, {} };
    }

    // Initialize positions as -1 to track the master files
    size_t mwPos = static_cast<size_t>(-1);
    size_t tPos = static_cast<size_t>(-1);
    size_t bPos = static_cast<size_t>(-1);

    // Iterate through the masters and determine their positions
    for (size_t i = 0; i < masters.size(); ++i) {
        if (masters[i].is_array() && !masters[i].empty() && masters[i][0].is_string()) {
            std::string masterName = masters[i][0];
            if (masterName == "Morrowind.esm") mwPos = i;
            else if (masterName == "Tribunal.esm") tPos = i;
            else if (masterName == "Bloodmoon.esm") bPos = i;
        }
    }

    // Check if "Morrowind.esm" is present
    if (mwPos == static_cast<size_t>(-1)) {
        logMessage("Morrowind.esm not found!");
        return { false, {} };
    }

    // Clear previous valid indices and database
    validMastIndices.clear();
    validMastersDB.clear();

    // Check the order of Tribunal and Bloodmoon
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

    // Check the combination M+T
    if (tPos != static_cast<size_t>(-1) && tPos > mwPos) {
        logMessage("Valid order of Parent Masters found: M+T.\n");
        validMastIndices = { 2 };
        validMastersDB = { 2 };
        return { true, validMastersDB };
    }

    // Check the combination M+B
    if (bPos != static_cast<size_t>(-1) && bPos > mwPos) {
        logMessage("Valid order of Parent Masters found: M+B.\n");
        validMastIndices = { 2 };
        validMastersDB = { 3 };
        return { true, validMastersDB };
    }

    return { false, {} };
}

// Function to execute a SQL query and fetch the refr_index
int fetchRefIndex(sqlite3* db, const std::string& query, int refrIndex, const std::string& id) {
    int result = -1;
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, refrIndex);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0); // Get the result from the first column
        }
    }
    else {
        logMessage("Database query error: " + std::string(sqlite3_errmsg(db)));
    }

    sqlite3_finalize(stmt); // Finalize the statement to free resources
    return result;
}

// Enumeration to specify fetch modes for database queries
enum FetchMode {
    FETCH_DB_ID,               // Fetch the ID from the database
    FETCH_OPPOSITE_REFR_INDEX   // Fetch the opposite refr_index
};

// Template function to fetch a value from the database based on the fetch mode
template <FetchMode mode>
auto fetchValue(sqlite3* db, int refrIndex, int mastIndex, const std::unordered_set<int>& validMastersDB, int conversionChoice)
-> std::conditional_t<mode == FETCH_DB_ID, std::string, int> {  // Conditional return type: string for DB_ID, int for refr_index
    std::string query;

    // Determine query based on conversion choice and fetch mode
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
            return std::string();  // Return empty string for DB_ID mode
        }
        else {
            return -1;             // Return -1 for refr_index mode in case of error
        }
    }

    // Adjust query based on master index, to restrict it to specific game versions
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

    // Prepare and execute the SQL statement
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

    sqlite3_bind_int(stmt, 1, refrIndex);  // Bind refrIndex to the query parameter

    // Fetch and return result based on the specified mode
    if constexpr (mode == FETCH_DB_ID) {
        std::string dbId;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (id) {
                dbId = id;  // If result found, assign it to dbId
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
            dbRefrIndex = sqlite3_column_int(stmt, 0);  // Assign found refr_index to dbRefrIndex
        }
        else {
            std::cerr << "No matching DB refr_index found for JSON refr_index: " << refrIndex << std::endl;
        }
        sqlite3_finalize(stmt);
        return dbRefrIndex;
    }
}

// Function to search and replace data
int processAndOptimizeJsonReplacement(
    sqlite3* db,
    const std::string& query,
    ordered_json& inputData,
    int conversionChoice,
    int& replacementsFlag,
    const std::unordered_set<int>& validMastersDB,
    std::vector<MismatchEntry>& mismatchedEntries) {

    // Caching query results
    std::unordered_map<std::string, int> refrIndexCache;

    // If inputData is an array, process it as an array of objects
    if (inputData.is_array()) {
        for (auto& cell : inputData) {
            // Check if the object is of type 'Cell'
            if (cell.contains("type") && cell["type"] == "Cell") {
                if (cell.contains("references") && cell["references"].is_array()) {
                    for (auto& reference : cell["references"]) {
                        // Check if the reference contains necessary fields
                        if (!reference.contains("refr_index") || !reference.contains("id") || !reference.contains("mast_index")) {
                            logMessage("Skipping invalid reference entry.");
                            continue;  // Skip invalid entries
                        }

                        int refrIndex = reference["refr_index"];
                        std::string id = reference["id"];
                        int mastIndex = reference["mast_index"];

                        // Check if this refr_index has already been processed
                        std::string cacheKey = std::to_string(refrIndex) + "_" + id;
                        if (refrIndexCache.find(cacheKey) == refrIndexCache.end()) {
                            // Execute database query to get the corresponding refr_index
                            int newRefrIndex = fetchRefIndex(db, query, refrIndex, id);
                            refrIndexCache[cacheKey] = newRefrIndex; // Cache the result

                            // If a match is found, replace refr_index directly in the JSON
                            if (newRefrIndex != -1) {
                                reference["refr_index"] = newRefrIndex;
                                logMessage("Replaced JSON refr_index " + std::to_string(refrIndex) +
                                    " with DB refr_index " + std::to_string(newRefrIndex) +
                                    " for JSON id: " + id);

                                // Mark that a replacement has been made
                                replacementsFlag = 1;
                            }
                            else if (mastIndex == 2 || mastIndex == 3) {
                                // Handle mismatches
                                int dbRefrIndex = fetchValue<FETCH_OPPOSITE_REFR_INDEX>(db, refrIndex, mastIndex, validMastersDB, conversionChoice);
                                std::string dbId = fetchValue<FETCH_DB_ID>(db, refrIndex, mastIndex, validMastersDB, conversionChoice);

                                mismatchedEntries.emplace_back(MismatchEntry{ refrIndex, id, dbId, dbRefrIndex });
                                logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndex) +
                                    " and JSON id: " + id + " with DB refr_index: " + std::to_string(dbRefrIndex) +
                                    " and DB id: " + dbId);
                            }
                        }
                        else {
                            // Use the cached value
                            int newRefrIndex = refrIndexCache[cacheKey];
                            if (newRefrIndex != -1) {
                                reference["refr_index"] = newRefrIndex;
                                logMessage("Replaced JSON refr_index " + std::to_string(refrIndex) +
                                    " with DB refr_index " + std::to_string(newRefrIndex) +
                                    " for JSON id: " + id);

                                // Mark that a replacement has been made
                                replacementsFlag = 1;
                            }
                        }
                    }
                }
            }
        }
    }

    // Handling mismatches
    if (mismatchedEntries.empty()) {
        logMessage("No mismatches found. Skipping mismatch handling.");
    }

    // User's choice for mismatch handling
    int mismatchChoice = getUserMismatchChoice();

    if (mismatchChoice == 1) { // User chose to handle mismatches
        for (const auto& entry : mismatchedEntries) {
            int refrIndex = entry.refrIndex;     // Reference index from JSON
            int dbRefrIndex = entry.dbRefrIndex; // Reference index from the database

            if (dbRefrIndex != -1) { // Ensure DB index is valid
                for (auto& cell : inputData) { // Iterate over all "Cell" objects in JSON
                    if (cell.contains("type") && cell["type"] == "Cell" &&
                        cell.contains("references") && cell["references"].is_array()) {

                        for (auto& reference : cell["references"]) { // Iterate over references
                            if (reference.contains("refr_index") && reference["refr_index"] == refrIndex) {
                                // If a match is found, replace `refr_index`
                                reference["refr_index"] = dbRefrIndex;
                                logMessage("Replaced mismatched refr_index " + std::to_string(refrIndex) +
                                    " with DB refr_index: " + std::to_string(dbRefrIndex));
                                replacementsFlag = 1; // Mark that a replacement occurred
                            }
                        }
                    }
                }
            }
            else {
                logMessage("Skipping mismatched entry with invalid DB refr_index: " + std::to_string(dbRefrIndex));
            }
        }
    }
    else {
        logMessage("\nMismatched entries will remain unchanged.");
    }

    return mismatchChoice;
}

// Saves modified JSON data to file and logs success message
bool saveJsonToFile(const std::filesystem::path& jsonFilePath, const ordered_json& inputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile) {
        outputFile << std::setw(2) << inputData;
        logMessage("\nModified JSON saved as: " + jsonFilePath.string() + "\n");
        return true;
    }
    return false;
}

// Executes command to convert JSON file to ESP/ESM format and logs success or failure
bool convertJsonToEsp(const std::filesystem::path& jsonFilePath, const std::filesystem::path& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath.string() + "\" \"" + espFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logMessage("Error: Failed to convert JSON to ESM/ESP. Check tes3conv.exe and JSON file format.\n");
        return false;
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath.string() + "\n");
    return true;
}

// Main function
int main() {
    // Program information
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Clear old logs
    clearLogFile("tes3_ric_log.txt");

    // Check if the database file exists
    std::filesystem::path dbFilePath = "tes3_en-ru_refr_index.db";
    if (!std::filesystem::exists(dbFilePath)) {
        logErrorAndExit(nullptr, "Database file 'tes3_en-ru_refr_index.db' not found.\n");
    }

    sqlite3* db = nullptr;

    // If the database file exists, attempt to load it
    if (sqlite3_open(dbFilePath.string().c_str(), &db)) {
        logErrorAndExit(db, "Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
    }
    logMessage("Database opened successfully...");

    // Check if tes3conv.exe exists
    std::filesystem::path converterPath = "tes3conv.exe";
    if (!std::filesystem::exists(converterPath)) {
        logErrorAndExit(db, "tes3conv.exe not found. Please download the latest version from\n"
            "https://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory with this program.\n");
    }
    logMessage("tes3conv.exe found...\nInitialisation complete.\n");

    // Get the conversion direction choice from the user
    int ConversionChoice = getUserConversionChoice();

    // Get the input file path from the user
    std::filesystem::path inputFilePath = getInputFilePath();
    std::filesystem::path inputPath(inputFilePath);

    // Define the output paths
    std::filesystem::path outputDir = inputPath.parent_path();
    std::filesystem::path jsonFilePath = outputDir / (inputPath.stem() += ".json");

    // Convert the input file to JSON using tes3conv.exe
    std::string command = "tes3conv.exe \"" + inputPath.string() + "\" \"" + jsonFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "Error converting to JSON. Check tes3conv.exe and the input file.\n");
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath.string());

    // Load the generated JSON file into a JSON object
    std::ifstream inputFile(jsonFilePath);
    if (!inputFile) {
        logErrorAndExit(db, "Failed to open JSON file for reading: " + jsonFilePath.string() + "\n");
    }

    // Use ordered_json to preserve the order of keys
    ordered_json inputData;
    inputFile >> inputData;
    inputFile.close();

    // Prepare replacements for possible undo of the conversion
    int replacementsFlag = 0;

    // Check if the required dependencies are ordered correctly in the input data
    auto [isValid, validMastersDB] = checkDependencyOrder(inputData);
    if (!isValid) {
        // Remove the temporary JSON file if it exists
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }
        logErrorAndExit(db, "Required Parent Masters not found or are in the wrong order.\n");
    }

    // SQL query based on the conversion choice
    std::string query = (ConversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    // Process mismatches between JSON and database refr_index values
    processAndOptimizeJsonReplacement(db, query, inputData, ConversionChoice, replacementsFlag, validMastersDB, mismatchedEntries);

    // If no replacements were found, cancel the conversion
    if (replacementsFlag == 0) {
        // Remove the temporary JSON file
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }
        logErrorAndExit(db, "No replacements found. Conversion canceled.\n");
    }

    // Save the modified JSON data to a new file using the saveJsonToFile function
    std::filesystem::path newJsonFilePath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json");
    if (!saveJsonToFile(newJsonFilePath, inputData)) {
        logErrorAndExit(db, "Error saving modified JSON file.\n");
    }

    // Convert the JSON back to ESP/ESM format
    std::filesystem::path outputExtension = inputPath.extension();
    std::filesystem::path newEspPath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension.string());

    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        logErrorAndExit(db, "Error converting JSON back to ESM/ESP.\n");
    }

    // Delete both JSON files if conversion succeeds
    //if (std::filesystem::exists(jsonFilePath)) std::filesystem::remove(jsonFilePath);
    //if (std::filesystem::exists(newJsonFilePath)) std::filesystem::remove(newJsonFilePath);
    //logMessage("Temporary JSON files deleted: " + jsonFilePath.string() + "\n                         and: " + newJsonFilePath.string() + "\n");

    // Close the database and finish execution
    sqlite3_close(db);
    logMessage("The ending of the words is ALMSIVI.\n");

    // Wait for the Enter key to finish
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}