#include <cctype>               // C Standard Library
#include <filesystem>           // C++ Standard Library
#include <fstream>              // C++ Standard Library
#include <iostream>             // C++ Standard Library
#include <limits>               // C++ Standard Library
#include <regex>                // C++ Standard Library
#include <sstream>              // C++ Standard Library
#include <string>               // C++ Standard Library
#include <unordered_map>        // C++ Standard Library
#include <unordered_set>        // C++ Standard Library
#include <vector>               // C++ Standard Library
#include <sqlite3.h>            // Third-party Library

// Program info
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

// Add a global variable for valid mast_index values
std::unordered_set<int> validMastIndices;

// Add a global variable for valid Masters for DB search
std::unordered_set<int> validMastersDB;

// Structure for storing information about mismatches
struct MismatchEntry {
    int refrIndex;      // Found refr_index
    std::string id;     // id from JSON
    std::string dbId;   // id from the database
    int oppositeRefrIndex; // Found opposite refr_index

    MismatchEntry(int refIdx, const std::string& jsonId, const std::string& dbIdStr, int oppositeRefIdx)
        : refrIndex(refIdx), id(jsonId), dbId(dbIdStr), oppositeRefrIndex(oppositeRefIdx) {}
};

// Container for storing all mismatches
std::vector<MismatchEntry> mismatchedEntries;

// Container for storing all replacements
std::unordered_map<int, int> replacements;

// Unified logging function
void logMessage(const std::string& message) {
    std::ofstream logFile("tes3ric_log.txt", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
    } else {
        std::cerr << "Failed to open log file." << std::endl;
    }
    std::cout << message << std::endl;
}

// Unified error handling
void logErrorAndExit(sqlite3* db, const std::string& message) {
    logMessage(message); // Log the error message
    if (db) {
        sqlite3_close(db); // Close the database if it's open
    }
    std::cout << "Press Enter to continue..."; // Prompt the user
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Wait for Enter key press
    throw std::runtime_error(message); // Throw an exception with the error message
}

// Function to clear the log file
void clearLogFile(const std::filesystem::path& logFileName) {
    if (std::filesystem::exists(logFileName)) {
        try {
            std::filesystem::remove(logFileName);
            logMessage("Log cleared successfully...");
        }
        catch (const std::filesystem::filesystem_error& e) {
            logMessage("Error clearing log file: " + std::string(e.what()));
        }
    }
}

// Function to get the user's choice for conversion direction
int getConversionChoice() {
    int ConversionChoice;
    while (true) {
        std::cout << "Convert a plugin or master file:\n1. Russian 1C to English GOTY\n2. English GOTY to Russian 1C\nChoice: ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "1" || input == "2") {
            ConversionChoice = input[0] - '0'; // Convert char to int
            break; // Exit loop on valid input
        }
        logMessage("Invalid choice. Enter 1 or 2."); // Log error for invalid input
    }
    return ConversionChoice;
}

// Function to get the file path from the user
std::filesystem::path getInputFilePath() {
    std::filesystem::path filePath;
    while (true) {
        std::cout << "Enter the ESP/ESM full path (including extension), or filename (with extension)\nif it's in the same folder as this converter: ";
        std::string input;
        std::getline(std::cin, input);
        filePath = input; // Convert input to path

        if (std::filesystem::exists(filePath) &&
            (filePath.extension() == ".esp" || filePath.extension() == ".esm")) {
            logMessage("File found: " + filePath.string());
            break; // Exit loop on valid file input
        }
        logMessage("File not found or incorrect extension."); // Log error for invalid input
    }
    return filePath;
}

// Function to check the order of dependencies in the JSON header
std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const std::string& inputData) {
    // Find positions of the master files
    size_t mwPos = inputData.find("Morrowind.esm");
    size_t tPos = inputData.find("Tribunal.esm");
    size_t bPos = inputData.find("Bloodmoon.esm");

    // Check if Morrowind is found
    if (mwPos == std::string::npos) {
        logMessage("Morrowind.esm not found.");
        return { false, {} }; // Return false and empty set
    }

    validMastIndices.clear(); // Clearing previous values
    validMastersDB.clear(); // Clearing previous values

    // Verify the order of dependencies
    if (tPos != std::string::npos && bPos != std::string::npos) {
        // Valid combination: M+T+B
        if (tPos < bPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.");
            validMastIndices.insert(2); // Tribunal
            validMastIndices.insert(3); // Bloodmoon
            validMastersDB.insert(1); // Update for M+T+B
            return { true, validMastersDB }; // Return valid result and updated db
        }
        else {
            logMessage("Invalid order. Tribunal.esm should come before Bloodmoon.esm.");
            return { false, {} }; // Invalid order
        }
    }
    else if (tPos != std::string::npos) {
        // Valid combination: M+T
        logMessage("Valid order of Parent Masters found: M+T.");
        validMastIndices.insert(2); // Tribunal
        validMastersDB.insert(2); // Update for M+T
        return { true, validMastersDB }; // Return valid result and updated db
    }
    else if (bPos != std::string::npos) {
        // Valid combination: M+B
        logMessage("Valid order of Parent Masters found: M+B.");
        validMastIndices.insert(3); // Bloodmoon
        validMastersDB.insert(3); // Update for M+B
        return { true, validMastersDB }; // Return valid result and updated db
    }
    else {
        return { false, {} }; // No valid masters found
    }
}

// Function to retrieve mast_index from a JSON object
int fetchCurrentMastIndex(const std::string& jsonObject) {
    std::regex mastIndexRegex(R"(\"mast_index\"\s*:\s*(\d+))");
    std::smatch mastIndexMatch;

    if (std::regex_search(jsonObject, mastIndexMatch, mastIndexRegex)) {
        return std::stoi(mastIndexMatch[1].str());
    }
    return -1; // Return -1 if not found
}

// Function to fetch refr_index from the database
int fetchRefIndex(sqlite3* db, const std::string& query, int refrIndex, const std::string& id) {
    int result = -1;
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, refrIndex);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0); // Get the result from the query
        }
    }
    else {
        logMessage("Database query error: " + std::string(sqlite3_errmsg(db)));
    }

    sqlite3_finalize(stmt); // Clean up prepared statement
    return result; // Return the fetched index
}

enum FetchMode {
    FETCH_DB_ID,
    FETCH_OPPOSITE_REFR_INDEX
};

// Combined function to fetch either dbId or opposite refr_index based on FetchMode
template <FetchMode mode>
auto fetchValue(sqlite3* db, int refrIndex, int mastIndex, const std::unordered_set<int>& validMastersDB, int ConversionChoice) {
    std::string query;

    // Prepare SQL query based on ConversionChoice
    switch (ConversionChoice) {
    case 1: // Russian to English
        if constexpr (mode == FETCH_DB_ID) {
            query = "SELECT ID FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ?";
        }
        else { // FETCH_OPPOSITE_REFR_INDEX
            query = "SELECT refr_index_EN FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ?";
        }
        break;
    case 2: // English to Russian
        if constexpr (mode == FETCH_DB_ID) {
            query = "SELECT ID FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ?";
        }
        else { // FETCH_OPPOSITE_REFR_INDEX
            query = "SELECT refr_index_RU FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ?";
        }
        break;
    default:
        std::cerr << "Invalid conversion choice." << std::endl;
        if constexpr (mode == FETCH_DB_ID) return std::string(); // Return empty string if choice is invalid
        else return -1; // Return -1 if choice is invalid
    }

    // Add conditions based on validMastersDB and mastIndex
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
    // Prepare the statement
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        if constexpr (mode == FETCH_DB_ID) return std::string(); // Return an empty string in case of error
        else return -1; // Return -1 in case of error
    }

    // Bind the refrIndex to the query
    sqlite3_bind_int(stmt, 1, refrIndex);

    // Execute the statement and fetch the value
    if constexpr (mode == FETCH_DB_ID) {
        std::string dbId; // Variable to hold the fetched dbId
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (id) {
                dbId = id; // Directly assign the id to dbId
            }
        }
        else {
            std::cerr << "No matching id found for refr_index: " << refrIndex << std::endl;
        }

        // Finalize the statement to release resources
        sqlite3_finalize(stmt);
        return dbId; // Return the fetched dbId (empty string if not found)

    }
    else { // FETCH_OPPOSITE_REFR_INDEX
        int oppositeRefrIndex = -1; // Default value if not found
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            oppositeRefrIndex = sqlite3_column_int(stmt, 0); // Get the opposite refr_index
        }
        else {
            std::cerr << "No matching opposite refr_index found for refr_index: " << refrIndex << std::endl;
        }

        // Finalize the statement to release resources
        sqlite3_finalize(stmt);
        return oppositeRefrIndex; // Return the fetched opposite refr_index (or -1 if not found)
    }
}

// Escape special characters for regex matching
std::string regexEscape(const std::string& str) {
    static const std::regex specialChars(R"([-[\]{}()*+?.,\^$|#\s])");
    return std::regex_replace(str, specialChars, R"(\$&)"); // Escape special characters
}

// Optimized function for processing JSON replacements
void optimizeJsonReplacement(std::ostringstream& outputStream, const std::string& inputData, const std::unordered_map<int, int>& replacements) {
    size_t pos = 0;
    size_t lastPos = 0;

    while ((pos = inputData.find("\"mast_index\":", lastPos)) != std::string::npos) {
        // Write data before the found "mast_index"
        outputStream << inputData.substr(lastPos, pos - lastPos);

        // Extract current mast_index
        size_t endPos = inputData.find_first_of(",}", pos);
        int currentMastIndex = std::stoi(inputData.substr(pos + 14, endPos - pos - 14));

        // Move to the next "refr_index" entry
        size_t refrIndexPos = inputData.find("\"refr_index\":", endPos);
        if (refrIndexPos == std::string::npos) {
            outputStream << inputData.substr(lastPos); // Write remaining data if no "refr_index" found
            break;
        }

        // Extract current refr_index
        size_t refrEndPos = inputData.find_first_of(",}", refrIndexPos);
        int currentIndex = std::stoi(inputData.substr(refrIndexPos + 14, refrEndPos - refrIndexPos - 14));

        // Check for valid mast_index and perform replacements
        outputStream << "\"mast_index\": " << currentMastIndex << ",\n        ";
        if (validMastIndices.find(currentMastIndex) != validMastIndices.end()) {
            // Replace refr_index if needed
            auto it = replacements.find(currentIndex);
            outputStream << "\"refr_index\": " << (it != replacements.end() ? it->second : currentIndex);
        }
        else {
            // Write back original refr_index if mast_index is invalid
            outputStream << "\"refr_index\": " << currentIndex;
        }

        // Update lastPos to continue searching
        lastPos = refrEndPos;
    }

    // Append any remaining data after the last processed entry
    if (lastPos < inputData.size()) {
        outputStream << inputData.substr(lastPos);
    }
}

// Function to process JSON objects, fetch replacements from the database, and handle mismatched entries
// (mismatchs occurs if object in the game world from Tribunal or Bloodmoon was replaced using "Edit -> Search & Replace" in TES3 CS)
int processAndHandleMismatches(sqlite3* db, const std::string& query, const std::string& inputData, int ConversionChoice, const std::unordered_set<int>& validMastersDB, std::unordered_map<int, int>& replacements) {
    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})"); // Regex to match JSON objects
    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();

    // Process all JSON objects
    while (it != end) {
        std::string jsonObject = it->str();
        std::regex refrIndexRegex(R"(\"refr_index\"\s*:\s*(\d+))"); // Regex for refr_index
        std::regex idRegex(R"(\"id\"\s*:\s*\"([^\"]+)\")"); // Regex for ID

        std::smatch refrIndexMatch, idMatch;
        // Use std::regex_search to find indices and IDs in each JSON object
        if (std::regex_search(jsonObject, refrIndexMatch, refrIndexRegex) &&
            std::regex_search(jsonObject, idMatch, idRegex)) {

            int refrIndex = std::stoi(refrIndexMatch[1].str());
            std::string id = idMatch[1].str();
            int currentMastIndex = fetchCurrentMastIndex(jsonObject); // Function to retrieve the current mast_index from jsonObject
            int newRefrIndex = fetchRefIndex(db, query, refrIndex, id); // Fetch new refr_index

            if (newRefrIndex != -1) {
                replacements[refrIndex] = newRefrIndex; // Store the replacement
                logMessage("Will replace JSON refr_index " + std::to_string(refrIndex) + " with DB refr_index" + std::to_string(newRefrIndex) + " for JSON id: " + id);
            }
            else {
                // If refr_index exists in the database but id does not match and mast_index is equal to 2 or 3, add to mismatchedEntries container
                if (currentMastIndex == 2 || currentMastIndex == 3) {
                    // Fetch the opposite refr_index based on the opposite ConversionChoice
                    int oppositeRefrIndex = fetchValue<FETCH_OPPOSITE_REFR_INDEX>(db, refrIndex, currentMastIndex, validMastersDB, ConversionChoice);

                    // Retrieve dbId with required parameters based on mast_index and validMastersDB
                    std::string dbId = fetchValue<FETCH_DB_ID>(db, refrIndex, currentMastIndex, validMastersDB, ConversionChoice);
                    mismatchedEntries.emplace_back(refrIndex, id, dbId, oppositeRefrIndex);
                    logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndex) + " with JSON id: " + id + " with DB refr_index : " + std::to_string(oppositeRefrIndex) + " with DB id: " + dbId);
                }
            }
        }
        ++it;
    }

    // User selection prompt
    int mismatchChoice = 0;
    while (true) {
        std::cout << "Found mismatched entries. Would you like to replace their refr_index anyway?\n1. Yes\n2. No\nChoice: ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "1" || input == "2") {
            mismatchChoice = std::stoi(input);  // Convert string to int directly
            break; // Exit loop on valid input
        }
        logMessage("Invalid choice. Enter 1 or 2."); // Log error for invalid input
    }

    if (mismatchChoice == 1) { // If the user selected 1 (Yes)
        for (const auto& entry : mismatchedEntries) {
            int refrIndex = entry.refrIndex;
            std::string id = entry.id;
            int oppositeRefrIndex = entry.oppositeRefrIndex; // Use the already gathered value

            if (oppositeRefrIndex != -1) { // Check for validity
                replacements[refrIndex] = oppositeRefrIndex;
                logMessage("Replaced JSON refr_index " + std::to_string(refrIndex) + " with DB refr_index : " + std::to_string(oppositeRefrIndex));
            }
        }
    }
    else { // If the user selected 2 (No)
        logMessage("Mismatched entries will remain unchanged.");
    }

    return mismatchChoice; // Return the user's choice
}

// Function to save modified JSON to a file
bool saveJsonToFile(const std::filesystem::path& jsonFilePath, const std::string& outputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile.is_open()) {
        outputFile << outputData; // Write data to the file
        outputFile.close();
        logMessage("Modified JSON saved as: " + jsonFilePath.string());
        return true;
    }
    else {
        return false;
    }
}

// Function to convert JSON back to ESM/ESP
bool convertJsonToEsp(const std::filesystem::path& jsonFilePath, const std::filesystem::path& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath.string() + "\" \"" + espFilePath.string() + "\""; // Command to run
    if (std::system(command.c_str()) != 0) {
        return false; // Return false if conversion fails
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath.string());
    return true; // Conversion succeeded
}

// Main program
int main() {
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Clear log file at the start of the program
    clearLogFile("tes3ric_log.txt");

    // Check if the database file exists
    std::filesystem::path dbFilePath = "tes3_ru-en_refr_index.db";
    if (!std::filesystem::exists(dbFilePath)) {
        logErrorAndExit(nullptr, "Database file 'tes3_ru-en_refr_index.db' not found.\n");
    }

    sqlite3* db = nullptr;

    // Open the SQLite database
    if (sqlite3_open(dbFilePath.string().c_str(), &db)) {
        logErrorAndExit(db, "Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
    }
    logMessage("Database opened successfully...");

    // Check if the conversion tool executable exists
    std::filesystem::path converterPath = "tes3conv.exe";
    if (!std::filesystem::exists(converterPath)) {
        logErrorAndExit(db, "tes3conv.exe not found. Please download the latest version from\n"
            "https://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory as this program.\n");
    }
    logMessage("tes3conv.exe found.\n");

    // User choice for conversion direction
    int ConversionChoice = getConversionChoice();

    // User input for the file path of the plugin or master file
    std::filesystem::path inputFilePath = getInputFilePath(); // Use filesystem path directly
    std::filesystem::path inputPath(inputFilePath); // Declare inputPath

    // Converting all .esp or .esm objects to JSON
    std::filesystem::path outputDir = inputPath.parent_path(); // Get the directory of the input file
    std::filesystem::path jsonFilePath = outputDir / (inputPath.stem() += ".json"); // Construct the JSON file path
    std::string command = "tes3conv.exe \"" + inputPath.string() + "\" \"" + jsonFilePath.string() + "\""; // Command to convert to JSON
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "Error converting to JSON. Check tes3conv.exe and the input file.\n");
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath.string());

    // Read the JSON data from the file
    std::ifstream inputFile(jsonFilePath);
    if (!inputFile) {
        logErrorAndExit(db, "Failed to open JSON file for reading: " + jsonFilePath.string() + "\n");
    }
    std::string inputData((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close(); // Close the input file

    // Regex to find JSON objects containing mast_index
    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    std::string outputData = inputData; // Copy input data for modification

    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex); // Start regex search
    auto end = std::sregex_iterator();

    // Prepare SQL query based on conversion choice
    std::string query = (ConversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    // Check dependencies in the JSON header
    auto [isValid, validMastersDB] = checkDependencyOrder(inputData);
    if (!isValid) {
        logErrorAndExit(db, "Required Parent Masters not found or in the wrong order.\n");
    }

    // Processing all JSON objects and managing mismatches
    int mismatchChoice = processAndHandleMismatches(db, query, inputData, ConversionChoice, validMastersDB, replacements);

    // Check if any replacements were made
    if (replacements.empty()) {
        logErrorAndExit(db, "No replacements found. Conversion canceled.\n");
    }

    // Use the optimized replacement function to modify the JSON
    std::ostringstream outputStream; // Prepare an output stream for the modified JSON
    optimizeJsonReplacement(outputStream, inputData, replacements); // Perform replacements
    outputData = outputStream.str(); // Get the modified data

    // Save the modified JSON data to a new file
    std::filesystem::path newJsonFilePath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json");
    if (!saveJsonToFile(newJsonFilePath, outputData)) {
        logErrorAndExit(db, "Error saving modified JSON file.\n");
    }

    // Converting all JSON objects back to .esp or .esm
    std::filesystem::path outputExtension = inputPath.extension(); // Get the original extension for output
    std::filesystem::path newEspPath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension.string()); // Construct output file path

    // Perform conversion from JSON back to ESP/ESM
    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        logErrorAndExit(db, "Error converting JSON back to ESM/ESP.\n");
    }

    sqlite3_close(db); // Close the database connection
    logMessage("Conversion complete.\n"); // Log completion
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0; // Return success
}