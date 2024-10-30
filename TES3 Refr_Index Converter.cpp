#include <iostream>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <sstream>
#include <unordered_map> 
#include <unordered_set> 
#include <sqlite3.h> 

// Program info
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

// Add a global variable for valid mast_index values
std::unordered_set<int> validMastIndices;

// Unified logging function
void logMessage(const std::string& message) {
    std::ofstream logFile("tes3ric_log.txt", std::ios_base::app);
    logFile << message << std::endl;
    std::cout << message << std::endl;
}

// Function to clear the log file
void clearLogFile(const std::string& logFileName) {
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
    int choice;
    while (true) {
        std::cout << "Convert a plugin or master file:\n1. Russian 1C to English GOTY\n2. English GOTY to Russian 1C\nChoice: ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "1" || input == "2") {
            choice = input[0] - '0'; // Convert char to int
            break; // Exit loop on valid input
        }
        logMessage("Invalid choice. Enter 1 or 2."); // Log error for invalid input
    }
    return choice;
}

// Function to get the file path from the user
std::string getInputFilePath() {
    std::string filePath;
    while (true) {
        std::cout << "Enter the ESP/ESM full path (including extension), or filename (with extension)\nif it's in the same folder as this converter: ";
        std::getline(std::cin, filePath);

        if (std::filesystem::exists(filePath) &&
            (filePath.ends_with(".esp") || filePath.ends_with(".esm"))) {
            logMessage("File found: " + filePath);
            break; // Exit loop on valid file input
        }
        logMessage("File not found or incorrect extension."); // Log error for invalid input
    }
    return filePath;
}

// Function to check the order of dependencies in the JSON header
bool checkDependencyOrder(const std::string& inputData) {
    // Find positions of the master files
    size_t mwPos = inputData.find("Morrowind.esm");
    size_t tPos = inputData.find("Tribunal.esm");
    size_t bPos = inputData.find("Bloodmoon.esm");

    // Check if Morrowind is found
    if (mwPos == std::string::npos) {
        logMessage("Error: Morrowind.esm not found.");
        return false;
    }

    validMastIndices.clear(); // Clearing previous values

    // Verify the order of dependencies
    if (tPos != std::string::npos && bPos != std::string::npos) {
        // Valid combination: M+T+B
        if (tPos < bPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.");
            validMastIndices.insert(2); // Tribunal
            validMastIndices.insert(3); // Bloodmoon
        }
        else {
            logMessage("Invalid order. Tribunal.esm should come before Bloodmoon.esm!");
            return false;
        }
    }
    else if (tPos != std::string::npos) {
        // Valid combination: M+T
        logMessage("Valid order of Parent Masters found: M+T.");
        validMastIndices.insert(2); // Tribunal
    }
    else if (bPos != std::string::npos) {
        // Valid combination: M+B
        logMessage("Valid order of Parent Masters found: M+B.");
        validMastIndices.insert(2); // Bloodmoon
    }
    else {
        return false; // No valid masters found
    }

    return true;
}

// Helper to fetch refr_index from the database
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

// Function to process JSON objects and fetch replacements from the database
std::unordered_map<int, int> processJsonObjects(sqlite3* db, const std::string& query, const std::string& inputData) {
    std::unordered_map<int, int> replacements; // Map to store replacements
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
            int newRefrIndex = fetchRefIndex(db, query, refrIndex, id); // Fetch new refr_index

            if (newRefrIndex != -1) {
                replacements[refrIndex] = newRefrIndex; // Store the replacement
                logMessage("Will replace refr_index " + std::to_string(refrIndex) + " with " + std::to_string(newRefrIndex) + " for id: " + id);
            }
        }
        ++it;
    }

    return replacements; // Return all replacements found
}

// Function to save modified JSON to a file
bool saveJsonToFile(const std::string& jsonFilePath, const std::string& outputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile.is_open()) {
        outputFile << outputData; // Write data to the file
        outputFile.close();
        logMessage("Modified JSON saved as: " + jsonFilePath);
        return true;
    }
    else {
        logMessage("Error saving JSON file.");
        return false;
    }
}

// Function to convert JSON back to ESM/ESP
bool convertJsonToEsp(const std::string& jsonFilePath, const std::string& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath + "\" \"" + espFilePath + "\""; // Command to run
    if (std::system(command.c_str()) != 0) {
        logMessage("Error converting JSON back to ESM/ESP.");
        return false; // Return false if conversion fails
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath);
    return true; // Conversion succeeded
}

// Main program
int main() {
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    // Clear log file at the start of the program
    clearLogFile("tes3ric_log.txt");

    // Check if the database file exists
    if (!std::filesystem::exists("tes3_ru-en_refr_index.db")) {
        logMessage("Database file 'tes3_ru-en_refr_index.db' not found.\n");
        std::system("pause");
        return 1; // Exit if the database file is not found
    }

    sqlite3* db;
    // Open the SQLite database
    if (sqlite3_open("tes3_ru-en_refr_index.db", &db)) {
        logMessage("Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
        sqlite3_close(db); // Close the database connection
        std::system("pause");
        return 1; // Exit if unable to open the database
    }
    logMessage("Database opened successfully...");

    // Check if the conversion tool executable exists
    if (!std::filesystem::exists("tes3conv.exe")) {
        logMessage("tes3conv.exe not found. Please download the latest version from\nhttps://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory as this program.\n");
        sqlite3_close(db); // Close the database connection
        std::system("pause");
        return 1; // Exit if the conversion tool is not found
    }
    logMessage("tes3conv.exe found.\n");

    // User choice for conversion direction
    int conversionChoice = getConversionChoice();

    // User input for the file path of the plugin or master file
    std::string inputFilePath = getInputFilePath();

    // Converting all .esp or .esm objects to JSON
    std::filesystem::path inputPath(inputFilePath);
    std::filesystem::path outputDir = inputPath.parent_path(); // Get the directory of the input file
    std::string jsonFilePath = (outputDir / inputPath.stem()).string() + ".json"; // Construct the JSON file path
    std::string command = "tes3conv.exe \"" + inputFilePath + "\" \"" + jsonFilePath + "\""; // Command to convert to JSON
    if (std::system(command.c_str()) != 0) {
        logMessage("Error converting to JSON. Check tes3conv.exe and the input file.");
        sqlite3_close(db); // Close the database connection
        std::system("pause");
        return 1; // Exit on conversion failure
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath);

    // Read the JSON data from the file
    std::ifstream inputFile(jsonFilePath);
    std::string inputData((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close(); // Close the input file

    // Check dependencies in the JSON header
    if (!checkDependencyOrder(inputData)) {
        logMessage("Required Parent Masters not found or in the wrong order. Aborting process...\n");
        sqlite3_close(db); // Close the database connection
        std::system("pause");
        return 1; // Exit if dependencies are invalid
    }

    // Regex to find JSON objects containing mast_index
    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    std::string outputData = inputData; // Copy input data for modification

    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex); // Start regex search
    auto end = std::sregex_iterator();
    // Prepare SQL query based on conversion choice
    std::string query = (conversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    // Process all JSON objects and store replacements
    std::unordered_map<int, int> replacements = processJsonObjects(db, query, inputData);

    // Check if any replacements were made
    if (replacements.empty()) {
        logMessage("No replacements found. Conversion canceled.\n");
        sqlite3_close(db); // Close the database connection
        std::system("pause");
        return 0; // Exit if no replacements were found
    }

    // Use the optimized replacement function to modify the JSON
    std::ostringstream outputStream; // Prepare an output stream for the modified JSON
    optimizeJsonReplacement(outputStream, inputData, replacements); // Perform replacements
    outputData = outputStream.str(); // Get the modified data

    // Save the modified JSON data to a new file
    std::string newJsonFilePath = (outputDir / ("CONV_" + std::string(conversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json")).string();
    if (!saveJsonToFile(newJsonFilePath, outputData)) {
        sqlite3_close(db); // Close the database connection on failure
        std::system("pause");
        return 1; // Exit if saving JSON fails
    }

    // Converting all JSON objects back to .esp or .esm
    std::string outputExtension = inputPath.extension() == ".esp" ? ".esp" : ".esm"; // Determine output extension
    std::string newEspPath = (outputDir / ("CONV_" + std::string(conversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension)).string(); // Construct output file path

    // Perform conversion from JSON back to ESP/ESM
    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        sqlite3_close(db); // Close the database connection on failure
        std::system("pause");
        return 1; // Exit if conversion fails
    }

    sqlite3_close(db); // Close the database connection
    logMessage("Conversion complete.\n"); // Log completion
    std::system("pause");
    return 0; // Return success
}