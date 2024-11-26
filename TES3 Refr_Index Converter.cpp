#include <cctype>                 // For character handling functions
#include <filesystem>             // For file and directory handling
#include <fstream>                // For file input and output
#include <iostream>               // For standard input and output
#include <limits>                 // For limits, used in input handling
#include <regex>                  // For regular expressions
#include <sstream>                // For string stream manipulation
#include <string>                 // For standard string handling
#include <unordered_map>          // For hash map (key-value storage)
#include <unordered_set>          // For hash set (unique value storage)
#include <vector>                 // For dynamic arrays
#include <sqlite3.h>              // For SQLite database handling
#include <optional>               // For optional values, used for error handling
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

// Function to clear the log file if it exists, and log the status
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
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const std::string& inputData) {
    size_t mwPos = inputData.find("Morrowind.esm");
    size_t tPos = inputData.find("Tribunal.esm");
    size_t bPos = inputData.find("Bloodmoon.esm");

    if (mwPos == std::string::npos) {
        logMessage("Morrowind.esm not found!");
        return { false, {} };
    }

    validMastIndices.clear();
    validMastersDB.clear();

    // Check order of Tribunal and Bloodmoon dependencies
    if (tPos != std::string::npos && bPos != std::string::npos) {
        if (tPos < bPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.\n");
            validMastIndices = { 2, 3 };
            validMastersDB.insert(1);
            return { true, validMastersDB };
        }
        else {
            logMessage("Invalid order of Parent Masters! Tribunal.esm should be before Bloodmoon.esm.\n");
            return { false, {} };
        }
    }

    if (tPos != std::string::npos) {
        logMessage("Valid order of Parent Masters found: M+T.\n");
        validMastIndices.insert(2);
        validMastersDB.insert(2);
        return { true, validMastersDB };
    }

    if (bPos != std::string::npos) {
        logMessage("Valid order of Parent Masters found: M+B.\n");
        validMastIndices.insert(2);
        validMastersDB.insert(3);
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

// Function to retrieve the current master index from a JSON object
int fetchCurrentMastIndex(const std::string& jsonObject) {
    std::regex mastIndexRegex(R"(\"mast_index\"\s*:\s*(\d+))");
    std::smatch mastIndexMatch;
    if (std::regex_search(jsonObject, mastIndexMatch, mastIndexRegex)) {
        return std::stoi(mastIndexMatch[1].str()); // Extract and convert to int
    }
    return -1; // Return -1 if not found
}

// Function to find "id" in a JSON object using a regex
std::optional<std::string> findId(const std::string& jsonObject) {
    std::regex idRegex(R"(\"id\"\s*:\s*\"([^\"]+)\")");  // Define regex to match "id" and capture its value
    std::smatch match;
    if (std::regex_search(jsonObject, match, idRegex)) {
        return match[1].str();  // Return the extracted ID as a string
    }
    return std::nullopt;  // Return nullopt if "id" is not found
}

// Function to find "refr_index" in a JSON object using a regex
std::optional<int> findRefrIndex(const std::string& jsonObject) {
    std::regex refrIndexRegex(R"(\"refr_index\"\s*:\s*(\d+))");  // Define regex to match "refr_index" and capture its value
    std::smatch match;
    if (std::regex_search(jsonObject, match, refrIndexRegex)) {
        return std::stoi(match[1].str());  // Return the extracted value as an integer
    }
    return std::nullopt;  // Return nullopt if "refr_index" is not found
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

// Function to escape special characters in a regex pattern
std::string regexEscape(const std::string& str) {
    static const std::regex specialChars(R"([-[\]{}()*+?.,\^$|#\s])");  // Define special characters to escape
    return std::regex_replace(str, specialChars, R"(\$&)");  // Escape each special character with '\'
}

// Function to process and handle mismatched "refr_index" values between JSON and DB
int processAndHandleMismatches(sqlite3* db, const std::string& query, const std::string& inputData,
    int conversionChoice, const std::unordered_set<int>& validMastersDB,
    std::unordered_map<int, int>& replacements,
    std::vector<MismatchEntry>& mismatchedEntries) {

    // Regex to match JSON objects that contain "mast_index" field
    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();

    // Loop through each JSON object found by the regex
    while (it != end) {
        std::string jsonObject = it->str();
        auto refrIndexOpt = findRefrIndex(jsonObject);  // Extract refr_index if available
        auto idOpt = findId(jsonObject);                // Extract id if available

        // Check if both refrIndex and id are present
        if (refrIndexOpt && idOpt) {
            int refrIndex = *refrIndexOpt;
            std::string id = *idOpt;
            int currentMastIndex = fetchCurrentMastIndex(jsonObject);
            int newRefrIndex = fetchRefIndex(db, query, refrIndex, id);  // Fetch corresponding DB refr_index

            // If DB refr_index is found, prepare to replace JSON refr_index with it
            if (newRefrIndex != -1) {
                replacements[refrIndex] = newRefrIndex;
                logMessage("Will replace JSON refr_index " + std::to_string(refrIndex) +
                    " with DB refr_index " + std::to_string(newRefrIndex) +
                    " for JSON id: " + id);
            }
            // If no match is found, add mismatch entry for further handling
            else if (currentMastIndex == 2 || currentMastIndex == 3) {
                int dbRefrIndex = fetchValue<FETCH_OPPOSITE_REFR_INDEX>(db, refrIndex, currentMastIndex, validMastersDB, conversionChoice);
                std::string dbId = fetchValue<FETCH_DB_ID>(db, refrIndex, currentMastIndex, validMastersDB, conversionChoice);
                mismatchedEntries.emplace_back(MismatchEntry{ refrIndex, id, dbId, dbRefrIndex });
                logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndex) +
                    " and JSON id: " + id + " with DB refr_index: " + std::to_string(dbRefrIndex) +
                    " and DB id: " + dbId);
            }
        }
        ++it;
    }

    // Check if there are any mismatches before prompting the user
    if (mismatchedEntries.empty()) {
        logMessage("No mismatches found. Skipping mismatch handling.");
        return 0;  // or an appropriate value indicating no mismatches
    }

    // Get user choice on how to handle mismatches
    int mismatchChoice = getUserMismatchChoice();

    // If user chooses to replace mismatched entries
    if (mismatchChoice == 1) {
        for (const auto& entry : mismatchedEntries) {
            int refrIndex = entry.refrIndex;
            int dbRefrIndex = entry.dbRefrIndex;

            // Replace mismatched JSON refr_index with the DB dbRefrIndex if available
            if (dbRefrIndex != -1) {
                replacements[refrIndex] = dbRefrIndex;
                logMessage("Replaced JSON refr_index " + std::to_string(refrIndex) +
                    " with DB refr_index: " + std::to_string(dbRefrIndex));
            }
        }
    }
    else {
        logMessage("\nMismatched entries will remain unchanged.");
    }

    return mismatchChoice;
}

// Ñòðóêòóðà äëÿ âîçâðàùàåìûõ çíà÷åíèé (ðåãóëÿðíûå âûðàæåíèÿ è SQL çàïðîñ)
struct RegexQueryResult {
    std::regex jsonObjectRegex;
    std::string sqlQuery;
};
// Ôóíêöèÿ äëÿ ïîëó÷åíèÿ ðåãóëÿðíîãî âûðàæåíèÿ è SQL çàïðîñà â çàâèñèìîñòè îò âûáîðà êîíâåðñèè
RegexQueryResult getJsonRegexAndQuery(int conversionChoice) {
    // Ðåãóëÿðíîå âûðàæåíèå äëÿ ïîèñêà îáúåêòîâ ñ ïîëåì "mast_index"
    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    // Ñòðîêà SQL çàïðîñà â çàâèñèìîñòè îò âûáîðà êîíâåðñèè
    std::string sqlQuery = (conversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ? AND id = ?;";
    return { jsonObjectRegex, sqlQuery };
}

// Optimizes replacement of JSON refr_index values based on replacements map
void optimizeJsonReplacement(std::ostringstream& outputStream, std::string_view inputData, const std::unordered_map<int, int>& replacements) {
    size_t pos = 0, lastPos = 0;
    const std::string mastKey = "\"mast_index\":";
    const std::string refrKey = "\"refr_index\":";
    const size_t mastKeyLen = mastKey.length();
    const size_t refrKeyLen = refrKey.length();

    // Find and replace "mast_index" and "refr_index" values in JSON data
    while ((pos = inputData.find(mastKey, lastPos)) != std::string::npos) {
        outputStream << inputData.substr(lastPos, pos - lastPos);

        size_t endPos = inputData.find_first_of(",}", pos);
        int currentMastIndex = std::stoi(std::string(inputData.substr(pos + mastKeyLen, endPos - pos - mastKeyLen)));

        size_t refrIndexPos = inputData.find(refrKey, endPos);
        if (refrIndexPos == std::string::npos) {
            outputStream << inputData.substr(lastPos);
            break;
        }

        size_t refrEndPos = inputData.find_first_of(",}", refrIndexPos);
        int currentIndex = std::stoi(std::string(inputData.substr(refrIndexPos + refrKeyLen, refrEndPos - refrIndexPos - refrKeyLen)));

        // Write mast_index and either replacement or original refr_index to output
        outputStream << mastKey << " " << currentMastIndex << ",\n        ";
        outputStream << refrKey << " " << (replacements.count(currentIndex) ? replacements.at(currentIndex) : currentIndex);

        lastPos = refrEndPos;
    }

    // Append remaining JSON data if any
    if (lastPos < inputData.size()) {
        outputStream << inputData.substr(lastPos);
    }
}

// Saves modified JSON data to file and logs success message
bool saveJsonToFile(const std::filesystem::path& jsonFilePath, const std::string& outputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile) {
        outputFile << outputData;
        logMessage("\nModified JSON saved as: " + jsonFilePath.string() + "\n");
        return true;
    }
    return false;
}

// Executes command to convert JSON file to ESP/ESM format and logs success or failure
bool convertJsonToEsp(const std::filesystem::path& jsonFilePath, const std::filesystem::path& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath.string() + "\" \"" + espFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        return false;
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath.string() + "\n");
    return true;
}

int main() {
    // Display program information
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Clear previous log entries
    clearLogFile("tes3_ric_log.txt");

    // Check if database file exists
    std::filesystem::path dbFilePath = "tes3_en-ru_refr_index.db";
    if (!std::filesystem::exists(dbFilePath)) {
        logErrorAndExit(nullptr, "Database file 'tes3_en-ru_refr_index.db' not found.\n");
    }

    sqlite3* db = nullptr;

    // Attempt to open database
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

    // Get conversion choice from the user
    int ConversionChoice = getUserConversionChoice();

    // Get input file path from user
    std::filesystem::path inputFilePath = getInputFilePath();
    std::filesystem::path inputPath(inputFilePath);

    // Prepare paths for output files
    std::filesystem::path outputDir = inputPath.parent_path();
    std::filesystem::path jsonFilePath = outputDir / (inputPath.stem() += ".json");

    // Convert the input file to JSON format using tes3conv.exe
    std::string command = "tes3conv.exe \"" + inputPath.string() + "\" \"" + jsonFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "Error converting to JSON. Check tes3conv.exe and the input file.\n");
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath.string());

    // Read the generated JSON file into a string
    std::ifstream inputFile(jsonFilePath);
    if (!inputFile) {
        logErrorAndExit(db, "Failed to open JSON file for reading: " + jsonFilePath.string() + "\n");
    }
    std::string inputData((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close();

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

    // Ïîëó÷àåì ðåãóëÿðíîå âûðàæåíèå è SQL çàïðîñ â çàâèñèìîñòè îò êîíâåðñèè
    auto [jsonObjectRegex, query] = getJsonRegexAndQuery(ConversionChoice);

    // Òåïåðü ìîæåì èñïîëüçîâàòü jsonObjectRegex è query
    std::string outputData = inputData;
    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();

    // Process mismatches between JSON and database refr_index values
    int mismatchChoice = processAndHandleMismatches(db, query, inputData, ConversionChoice, validMastersDB, replacements, mismatchedEntries);

    // If no replacements were identified, cancel the conversion
    if (replacements.empty()) {
        // Attempt to delete the created JSON file
        if (std::filesystem::exists(jsonFilePath)) {
            std::filesystem::remove(jsonFilePath);
            logMessage("Temporary JSON file deleted: " + jsonFilePath.string() + "\n");
        }

        logErrorAndExit(db, "No replacements found. Conversion canceled.\n");
    }

    // Prepare output JSON data with updated refr_index values
    std::ostringstream outputStream;
    optimizeJsonReplacement(outputStream, inputData, replacements);
    outputData = outputStream.str();

    // Save modified JSON file with a new name indicating conversion direction
    std::filesystem::path newJsonFilePath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json");
    if (!saveJsonToFile(newJsonFilePath, outputData)) {
        logErrorAndExit(db, "Error saving modified JSON file.\n");
    }

    // Convert the modified JSON back to ESP/ESM format
    std::filesystem::path outputExtension = inputPath.extension();
    std::filesystem::path newEspPath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension.string());

    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        logErrorAndExit(db, "Error converting JSON back to ESM/ESP.\n");
    }

    // Delete both JSON files if conversion succeeds
    if (std::filesystem::exists(jsonFilePath)) std::filesystem::remove(jsonFilePath);
    if (std::filesystem::exists(newJsonFilePath)) std::filesystem::remove(newJsonFilePath);
    logMessage("Temporary JSON files deleted: " + jsonFilePath.string() + "\n                         and: " + newJsonFilePath.string() + "\n");

    // Close the database and finish execution
    sqlite3_close(db);
    logMessage("The ending of the words is ALMSIVI.\n");

    // Prompt the user to press Enter before exiting
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}